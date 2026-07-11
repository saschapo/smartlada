#include "display.h"

#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Wire.h>

namespace display {

// HW-364A pinout (verified against github.com/peff74/esp8266_OLED_HW-364A)
static constexpr uint8_t OLED_SDA = 14, OLED_SCL = 12, OLED_ADDR = 0x3C;
static constexpr uint8_t BTN_FLASH = 0;  // LOW = pressed
static constexpr uint8_t NUM_PAGES = 3;
static constexpr uint16_t REDRAW_MS = 500, DEBOUNCE_MS = 30;

// Short channel labels to fit the 21-char line (GFX font — ASCII only)
static const char* const SHORT_KEY[channels::NUM_CHANNELS] = {"stop", "rev",
                                                              "turn", "mark"};

static Adafruit_SSD1306 oled(128, 64, &Wire, -1);
static const config::Config* s_cfg = nullptr;
static const channels::Channel* s_chans = nullptr;
static bool s_ok = false;

static uint8_t s_page = 0;
static uint32_t s_lastDraw = 0;
static uint32_t s_loops = 0, s_lps = 0;
static bool s_btnStable = true, s_btnRead = true;  // pullup: true = released
static uint32_t s_btnChange = 0;

static void redraw(uint32_t now_ms) {
  if (now_ms != s_lastDraw) s_lps = s_loops * 1000 / (now_ms - s_lastDraw);
  s_loops = 0;
  s_lastDraw = now_ms;

  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);

  switch (s_page) {
    case 0:
      oled.println(F("SmartLada v0.1  [1/3]"));
      oled.print(F("IP   "));
      oled.println(WiFi.softAPIP());
      // no space: "MAC" + 17 address chars = 20, fits the 21-char line
      oled.print(F("MAC"));
      oled.println(WiFi.softAPmacAddress());
      oled.printf("sta  %u  heap %u\n", WiFi.softAPgetStationNum(),
                  ESP.getFreeHeap());
      oled.printf("PWM  %u Hz\n", s_cfg->pwm_freq_hz);
      if (s_cfg->max_duty_cap_pct < 100)
        oled.printf("CAP  %u%% ON\n", s_cfg->max_duty_cap_pct);
      else
        oled.println(F("CAP  OFF (100%)"));
      oled.printf("loop %u/s", s_lps);
      break;
    case 1:
      oled.println(F("Channels        [2/3]"));
      oled.println(F("ch   gpio  pct  duty"));
      for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++)
        oled.printf("%-4s %4u %3u%% %5u\n", SHORT_KEY[i], s_cfg->ch[i].gpio,
                    s_chans[i].percent(), s_chans[i].currentDuty());
      break;
    case 2:
      oled.println(F("Calib           [3/3]"));
      oled.println(F("ch  gamma range   ss"));
      for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++) {
        const channels::Calib& c = s_cfg->ch[i].calib;
        oled.printf("%-4s %.1f %u..%u %u\n", SHORT_KEY[i], (double)c.gamma,
                    c.min_duty, c.max_duty, c.soft_start_ms);
      }
      break;
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

bool begin(const config::Config* cfg, const channels::Channel* chans) {
  s_cfg = cfg;
  s_chans = chans;
  pinMode(BTN_FLASH, INPUT_PULLUP);
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(400000);
  s_ok = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (s_ok) redraw(millis());
  return s_ok;
}

void tick(uint32_t now_ms) {
  if (!s_ok) return;
  s_loops++;
  handleButton(now_ms);
  if (now_ms - s_lastDraw >= REDRAW_MS) redraw(now_ms);
}

}  // namespace display
