// ThermalTest — standalone DS18B20 validation bench on ESP8266 (HW-364A).
// Validates 3x DS18B20 on one 1-Wire bus (GPIO13/D7) + phone time-sync + cineink-
// style event log + dark web view, BEFORE the thermal layer is ported into
// SmartLadaC6. Does not drive lamps/PWM. Wiring/spec — docs/TZ_thermal_test.md.
//
// Sensors: 3x DS18B20, one bus on GPIO13, single 4.7k pull-up to 3.3V, normal
// (non-parasite) power, common ground.

#include <ESP8266WiFi.h>
#include <math.h>

#include "src/display/display.h"
#include "src/log/log.h"
#include "src/rtc/rtc.h"
#include "src/stats/stats.h"
#include "src/thermal/thermal.h"
#include "src/web/web_server.h"

static const char* FW_VERSION = "0.1.0-thermaltest";
static const char* AP_SSID = "ThermalTest";
static const char* AP_PASS = "thermaltest";

// ── temperature logging policy ───────────────────────────────────────────────
// Live reading runs at 2 s (OLED/web). Persisted samples are sparser: a control
// stamp every 60 s, plus an out-of-band line whenever any sensor moves > 2 C from
// its last-logged value (tag "TEMP*"). One line carries all sensors.
static constexpr float DELTA_C = 2.0f;
static constexpr uint32_t STAMP_MS = 60000;
static float s_logged[thermal::NUM_SENSORS];   // last value written per sensor
static uint32_t s_lastStamp = 0;

static void tempLogInit() {
  for (uint8_t i = 0; i < thermal::NUM_SENSORS; i++) s_logged[i] = NAN;
  s_lastStamp = 0;   // force a stamp on the first tick
}

static void writeTempLine(bool isDelta) {
  char parts[thermal::NUM_SENSORS][20];
  for (uint8_t i = 0; i < thermal::NUM_SENSORS; i++) {
    if (thermal::present(i))
      snprintf(parts[i], sizeof(parts[i]), "%s=%.1f", thermal::LABELS[i],
               (double)thermal::celsius(i));
    else
      snprintf(parts[i], sizeof(parts[i]), "%s=--", thermal::LABELS[i]);
    s_logged[i] = thermal::celsius(i);   // NaN when absent -> re-triggers on return
  }
  // "TEMP*" marks a change-triggered line; plain "TEMP" is a periodic stamp.
  // ASCII only — the log must stay readable in any editor/encoding.
  applog::write('I', isDelta ? "TEMP*" : "TEMP", "%s %s", parts[0], parts[1]);
}

static void tempLogTick(uint32_t now_ms) {
  bool stampDue = (now_ms - s_lastStamp) >= STAMP_MS;

  bool delta = false;
  for (uint8_t i = 0; i < thermal::NUM_SENSORS; i++) {
    if (!thermal::present(i)) continue;
    float t = thermal::celsius(i);
    if (isnan(s_logged[i]) || fabsf(t - s_logged[i]) > DELTA_C) {
      delta = true;
      break;
    }
  }

  if (stampDue || delta) {
    writeTempLine(delta && !stampDue);
    if (stampDue) s_lastStamp = now_ms;
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.printf("\nThermalTest start  fw %s\n", FW_VERSION);

  applog::begin();          // mount LittleFS + boot anchor
  thermal::begin();
  thermal::dumpBus(Serial);  // print ROM addresses for the CONFIGURED[] table
  stats::begin();
  tempLogInit();

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print(F("AP "));
  Serial.print(AP_SSID);
  Serial.print(F(", IP: "));
  Serial.println(WiFi.softAPIP());

  web::begin();
  Serial.println(F("HTTP :80 ready"));

  if (display::begin())
    Serial.println(F("OLED ready (FLASH button switches pages)"));
  else
    Serial.println(F("OLED not found, continuing without display"));
}

void loop() {
  web::handle();
  const uint32_t now = millis();
  thermal::tick(now);
  stats::tick(now);
  tempLogTick(now);
  display::tick(now);
}
