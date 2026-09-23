#pragma once
#include <Adafruit_SSD1306.h>

// OLED layer: Adafruit_SSD1306 (drives the SSD1315) + thin wrappers for SSD1315
// features Adafruit does not expose (contrast, inverse, panel power). Recommended
// init sets display clock 0xD5=0xF0 (finest strobe). See ssd1315 command ref.
// UI_TFT=1: bench variant with the 2.4" ST7789 + EC11 module on J4 (+ CS/RES on J8) instead of
// the SSD1315 + 4 keys. The menu still draws into the 128x64 SSD1306 frame buffer; display() pushes it 1:1
// to the centre of the TFT. Build: --build-property compiler.cpp.extra_flags=-DUI_TFT=1
#ifndef UI_TFT
#define UI_TFT 0
#endif

namespace display {

#if UI_TFT
// Adafruit_SSD1306 kept only for its frame buffer + GFX drawing. begin/display/ssd1306_command
// are non-virtual and hidden by name, which works because callers use this static type.
class TftOled : public Adafruit_SSD1306 {
 public:
  TftOled() : Adafruit_SSD1306(128, 64) {}
  bool begin();
  void display();
  void ssd1306_command(uint8_t c);   // emulates contrast / inverse / panel on-off
};
extern TftOled oled;
#else
extern Adafruit_SSD1306 oled;
#endif

bool begin();                          // I2C + panel init (0xD5=0xF0); false if absent
void splash(const char* title, const char* sub);  // boot screen, no delay
void setBrightness(uint8_t c);         // 0x81 contrast, 0..255
void setInverse(bool inv);             // 0xA6 / 0xA7 (instant, no re-flush)
void power(bool on);                   // 0xAF / 0xAE (RAM retained when off)
void invertRect(int16_t x, int16_t y, int16_t w, int16_t h);  // XOR-flip buffer region

}  // namespace display
