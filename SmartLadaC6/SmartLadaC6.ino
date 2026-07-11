// SmartLada C6 — PWM bring-up bench on ESP32-C6 (hardware LEDC, low-side D4184).
// Port of the ESP8266 bench: channels/ and config/ layers moved unchanged,
// platform_pwm/ rewritten on LEDC, display/ (OLED) replaced by status/ (NeoPixel),
// web/ — same UI on the arduino-esp32 core. Wiring/pins/warnings — README.md.
//
// [CRITICAL with lamps connected] Common ground C6 <-> 12 V PSU. Fuse T6.3–8 A.
// Channel pins are boot-safe (GPIO0..3), but the D4184 gate pulldown is mandatory.

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
  // FORCE-SAFE: drive all 4 channels to 0 before any other init
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
    lastDuty[i] = 0xFFFF;  // force the first write
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.setSleep(false);  // no modem power-save: removes the 1-2 s delay on the first
                         // request after idle (else the sync WebServer blocks loop)
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
  anim::tick(now);    // in animation mode drives channel percents; silent in MANUAL
  button::tick(now);  // BOOT button: mode switch / dimmer (may override percents)
  for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++) {
    uint16_t d = chans[i].update(now);
    if (d != lastDuty[i]) {
      platform_pwm::write(cfg.gpio[i], d);
      lastDuty[i] = d;
    }
  }
  status::tick(now);
}
