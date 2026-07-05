// SmartLada — калибровочный стенд PWM на ESP8266 (bring-up 4x D4184, low-side).
// НЕ финальная прошивка: слои channels/ и config/ переносятся на ESP32-C6,
// platform_pwm/ и web/ выбрасываются. Схема подключения и предупреждения — README.md.
//
// [КРИТИЧНО] Общая земля ESP8266 <-> БП 12 В. Предохранитель T6.3–8 А. См. README.

#include <ESP8266WiFi.h>

#include "src/channels/channels.h"
#include "src/config/config.h"
#include "src/platform_pwm/platform_pwm.h"
#include "src/web/web_server.h"

static const char* AP_SSID = "SmartLada";
static const char* AP_PASS = "smartlada";

static config::Config cfg;
static channels::Channel chans[channels::NUM_CHANNELS];
static uint16_t lastDuty[channels::NUM_CHANNELS];

static void applyGlobals() { platform_pwm::setFreq(cfg.pwm_freq_hz); }

void setup() {
  // FORCE-SAFE: все 4 канала в 0 до любой другой инициализации
  config::setDefaults(cfg);
  uint8_t pins[channels::NUM_CHANNELS];
  for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++) pins[i] = cfg.ch[i].gpio;
  platform_pwm::forceAllLow(pins, channels::NUM_CHANNELS);

  Serial.begin(115200);
  delay(50);
  Serial.println(F("\nSmartLada bench start"));

  platform_pwm::begin(cfg.pwm_freq_hz);
  const uint32_t now = millis();
  for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++) {
    chans[i].setCalib(cfg.ch[i].calib, now);
    chans[i].setCapPct(cfg.max_duty_cap_pct, now);
    lastDuty[i] = 0xFFFF;  // форсировать первую запись
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print(F("AP "));
  Serial.print(AP_SSID);
  Serial.print(F(", IP: "));
  Serial.println(WiFi.softAPIP());
  Serial.print(F("MAC: "));
  Serial.println(WiFi.softAPmacAddress());

  web::begin(&cfg, chans, applyGlobals);
  Serial.println(F("HTTP :80 ready"));
}

void loop() {
  web::handle();
  const uint32_t now = millis();
  for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++) {
    uint16_t d = chans[i].update(now);
    if (d != lastDuty[i]) {
      platform_pwm::write(cfg.ch[i].gpio, d);
      lastDuty[i] = d;
    }
  }
}
