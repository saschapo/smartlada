// SmartLada C6 — bring-up стенд PWM на ESP32-C6 (аппаратный LEDC, low-side D4184).
// Порт стенда ESP8266: слои channels/ и config/ перенесены без изменений,
// platform_pwm/ переписан на LEDC, display/ (OLED) заменён на status/ (NeoPixel),
// web/ — тот же UI на ядре arduino-esp32. Схема/пины/предупреждения — README.md.
//
// [КРИТИЧНО при подключённых лампах] Общая земля C6 <-> БП 12 В. Предохранитель
// T6.3–8 А. Пины каналов boot-safe (GPIO0..3), но затворный pulldown D4184 обязателен.

#include <WiFi.h>

#include "src/anim/anim.h"
#include "src/button/button.h"
#include "src/channels/channels.h"
#include "src/config/config.h"
#include "src/platform_pwm/platform_pwm.h"
#include "src/status/status.h"
#include "src/version.h"
#include "src/web/web_server.h"

static const char* AP_SSID = "SmartLada";
static const char* AP_PASS = "smartlada";

static config::Config cfg;
static channels::Channel chans[channels::NUM_CHANNELS];
static uint16_t lastDuty[channels::NUM_CHANNELS];
static uint8_t chanPins[channels::NUM_CHANNELS];

static void applyGlobals() {
  platform_pwm::setFreq(cfg.pwm_freq_hz);
  anim::setTimings(cfg.chase_ms, cfg.seq_ms, cfg.pulse_ms, cfg.alt_ms);
}

void setup() {
  // FORCE-SAFE: все 4 канала в 0 до любой другой инициализации
  config::setDefaults(cfg);
  for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++) chanPins[i] = cfg.gpio[i];
  platform_pwm::forceAllLow(chanPins, channels::NUM_CHANNELS);

  Serial.begin(115200);
  delay(50);
  Serial.printf("\nSmartLada C6 bench start  fw %s (%s)\n", FW_VERSION, FW_BUILD);

  platform_pwm::begin(chanPins, channels::NUM_CHANNELS, cfg.pwm_freq_hz);
  const uint32_t now = millis();
  for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++) {
    chans[i].setCalib(cfg.calib, now);
    chans[i].setCapPct(cfg.max_duty_cap_pct, now);
    lastDuty[i] = 0xFFFF;  // форсировать первую запись
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.setSleep(false);  // без модем-power-save: убирает задержку 1-2 с на первый
                         // запрос после простоя (иначе синхронный WebServer блокирует loop)
  Serial.print(F("AP "));
  Serial.print(AP_SSID);
  Serial.print(F(", IP: "));
  Serial.println(WiFi.softAPIP());
  Serial.print(F("MAC: "));
  Serial.println(WiFi.softAPmacAddress());

  web::begin(&cfg, chans, applyGlobals);
  anim::begin(chans);
  anim::setTimings(cfg.chase_ms, cfg.seq_ms, cfg.pulse_ms, cfg.alt_ms);
  status::begin(chans);
  button::begin(chans);
  Serial.println(F("HTTP :80 ready"));
}

void loop() {
  web::handle();
  const uint32_t now = millis();
  anim::tick(now);    // в режиме анимации ведёт проценты каналов; в MANUAL молчит
  button::tick(now);  // кнопка BOOT: смена режима / диммер (может перебить проценты)
  for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++) {
    uint16_t d = chans[i].update(now);
    if (d != lastDuty[i]) {
      platform_pwm::write(cfg.gpio[i], d);
      lastDuty[i] = d;
    }
  }
  status::tick(now);
}
