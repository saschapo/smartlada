#include "display.h"

#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Wire.h>
#include <math.h>

#include "../log/log.h"
#include "../rtc/rtc.h"
#include "../stats/stats.h"
#include "../thermal/thermal.h"

namespace display {

// HW-364A pinout (verified against github.com/peff74/esp8266_OLED_HW-364A)
static constexpr uint8_t OLED_SDA = 14, OLED_SCL = 12, OLED_ADDR = 0x3C;
static constexpr uint8_t BTN_FLASH = 0;  // LOW = pressed
static constexpr uint8_t NUM_PAGES = 5;
static constexpr uint16_t REDRAW_MS = 500, DEBOUNCE_MS = 30;

static Adafruit_SSD1306 oled(128, 64, &Wire, -1);
static bool s_ok = false;

static uint8_t s_page = 0;
static uint32_t s_lastDraw = 0;
static bool s_btnStable = true, s_btnRead = true;  // pullup: true = released
static uint32_t s_btnChange = 0;

// Big value or "--" for a missing sensor.
static void fmtTemp(uint8_t i, char* out, size_t n) {
  if (thermal::present(i))
    snprintf(out, n, "%.1f", (double)thermal::celsius(i));
  else
    snprintf(out, n, "--");
}

// One-decimal value or "--" for NaN (stats/history cells).
static void fmt1(float v, char* out, size_t n) {
  if (isnan(v))
    snprintf(out, n, "--");
  else
    snprintf(out, n, "%.1f", (double)v);
}

static void pageTemps() {
  char clk[12];
  rtc::format(clk, sizeof(clk), false);
  // Yellow strip (top 16 px on this board): title + clock only.
  oled.setTextSize(1);
  oled.setCursor(0, 4);
  oled.printf("THERMAL  %s", clk);

  // Blue area (y >= 16): two big rows, clear of the yellow zone.
  oled.setTextSize(2);
  const uint8_t y[thermal::NUM_SENSORS] = {24, 46};
  const char* const tag[thermal::NUM_SENSORS] = {"PSU ", "BODY"};
  for (uint8_t i = 0; i < thermal::NUM_SENSORS; i++) {
    char v[10];
    fmtTemp(i, v, sizeof(v));
    oled.setCursor(0, y[i]);
    oled.printf("%s %s", tag[i], v);
  }
}

static void pageStatus() {
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println(F("Status          [2/5]"));
  oled.print(F("IP   "));
  oled.println(WiFi.softAPIP());
  oled.printf("sta  %u   heap %u\n", WiFi.softAPgetStationNum(), ESP.getFreeHeap());
  oled.printf("log  %u B\n", (unsigned)applog::size());
  oled.printf("clock %s\n", rtc::isSet() ? "set" : "unset");
}

static void pageSensors() {
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println(F("Sensors         [3/5]"));
  for (uint8_t i = 0; i < thermal::NUM_SENSORS; i++) {
    uint8_t rom[8];
    if (thermal::getRom(i, rom)) {
      // label + last 3 ROM bytes (enough to tell sensors apart) + present flag
      oled.printf("%-4s %02X%02X%02X %s\n", thermal::LABELS[i], rom[5], rom[6],
                  rom[7], thermal::present(i) ? "ok" : "--");
    } else {
      oled.printf("%-4s ------  none\n", thermal::LABELS[i]);
    }
  }
}

// Page 4: recent-sample table (30 s cadence, newest first).
static void pageHistory(uint32_t now_ms) {
  oled.setTextSize(1);
  oled.setCursor(0, 4);
  oled.print(F("HISTORY     [4/5]"));   // yellow strip
  oled.setCursor(0, 16);
  oled.print(F("age    psu  body"));    // column header (blue)

  uint8_t cnt = stats::historyCount();
  if (cnt == 0) {
    oled.setCursor(0, 32);
    oled.print(F("collecting..."));
    return;
  }
  for (uint8_t i = 0; i < cnt; i++) {
    uint32_t t;
    float v[thermal::NUM_SENSORS];
    if (!stats::history(i, t, v)) break;
    uint32_t age = (now_ms - t) / 1000;
    char a[8];
    if (age < 600)
      snprintf(a, sizeof(a), "%lus", (unsigned long)age);
    else
      snprintf(a, sizeof(a), "%lum", (unsigned long)(age / 60));
    char p[8], b[8];
    fmt1(v[0], p, sizeof(p));
    fmt1(v[1], b, sizeof(b));
    oled.setCursor(0, 24 + i * 8);
    oled.printf("%-5s %5s %5s", a, p, b);
  }
}

// Page 5: running min/max/avg per sensor + sample count and uptime.
static void pageStats() {
  oled.setTextSize(1);
  oled.setCursor(0, 4);
  oled.print(F("MIN/MAX/AVG [5/5]"));   // yellow strip
  oled.setCursor(0, 16);
  oled.print(F("      min  max  avg"));

  const char* const lab[thermal::NUM_SENSORS] = {"psu", "body"};
  for (uint8_t i = 0; i < thermal::NUM_SENSORS; i++) {
    char mn[7], mx[7], av[7];
    fmt1(stats::minC(i), mn, sizeof(mn));
    fmt1(stats::maxC(i), mx, sizeof(mx));
    fmt1(stats::avgC(i), av, sizeof(av));
    oled.setCursor(0, 28 + i * 10);
    oled.printf("%-4s %4s %4s %4s", lab[i], mn, mx, av);
  }
  uint32_t up = millis() / 1000UL;
  oled.setCursor(0, 54);
  oled.printf("n=%lu  up %lu:%02lu", (unsigned long)stats::sampleCount(),
              (unsigned long)(up / 60), (unsigned long)(up % 60));
}

static void redraw(uint32_t now_ms) {
  s_lastDraw = now_ms;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  switch (s_page) {
    case 0: pageTemps(); break;
    case 1: pageStatus(); break;
    case 2: pageSensors(); break;
    case 3: pageHistory(now_ms); break;
    case 4: pageStats(); break;
  }
  oled.display();
}

static void handleButton(uint32_t now_ms) {
  bool r = digitalRead(BTN_FLASH);
  if (r != s_btnRead) {
    s_btnRead = r;
    s_btnChange = now_ms;
  }
  if ((now_ms - s_btnChange) >= DEBOUNCE_MS && r != s_btnStable) {
    s_btnStable = r;
    if (!r) {  // press
      s_page = (s_page + 1) % NUM_PAGES;
      redraw(now_ms);
    }
  }
}

bool begin() {
  pinMode(BTN_FLASH, INPUT_PULLUP);
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(400000);
  s_ok = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (s_ok) redraw(millis());
  return s_ok;
}

void tick(uint32_t now_ms) {
  if (!s_ok) return;
  handleButton(now_ms);
  if (now_ms - s_lastDraw >= REDRAW_MS) redraw(now_ms);
}

}  // namespace display
