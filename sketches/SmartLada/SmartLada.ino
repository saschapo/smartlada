// SmartLada — PWM calibration bench on ESP8266 (bring-up 4x D4184, low-side).
// NOT the final firmware: the channels/ and config/ layers are ported to ESP32-C6,
// platform_pwm/ and web/ are discarded. Wiring and warnings — README.md.
//
// [CRITICAL] Common ground ESP8266 <-> 12 V PSU. Fuse T6.3–8 A. See README.

#include <ESP8266WiFi.h>

#include "src/channels/channels.h"
#include "src/config/config.h"
#include "src/display/display.h"
#include "src/platform_pwm/platform_pwm.h"
#include "src/web/web_server.h"

static const char* AP_SSID = "SmartLada";
static const char* AP_PASS = "smartlada";

static config::Config cfg;
static channels::Channel chans[channels::NUM_CHANNELS];
static uint16_t lastDuty[channels::NUM_CHANNELS];

static void applyGlobals() { platform_pwm::setFreq(cfg.pwm_freq_hz); }

void setup() {
  // FORCE-SAFE: drive all 4 channels to 0 before any other init
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
    lastDuty[i] = 0xFFFF;  // force the first write
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

  if (display::begin(&cfg, chans))
    Serial.println(F("OLED ready (FLASH button switches pages)"));
  else
    Serial.println(F("OLED not found, continuing without display"));
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
  display::tick(now);
}
