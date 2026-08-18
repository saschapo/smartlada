#pragma once
#include <Adafruit_SSD1306.h>

// OLED layer: Adafruit_SSD1306 (drives the SSD1315) + thin wrappers for SSD1315
// features Adafruit does not expose (contrast, inverse, panel power). Recommended
// init sets display clock 0xD5=0xF0 (finest strobe). See ssd1315 command ref.
namespace display {

extern Adafruit_SSD1306 oled;

bool begin();                          // I2C + panel init (0xD5=0xF0); false if absent
void splash(const char* title, const char* sub);  // boot screen, no delay
void setBrightness(uint8_t c);         // 0x81 contrast, 0..255
void setInverse(bool inv);             // 0xA6 / 0xA7 (instant, no re-flush)
void power(bool on);                   // 0xAF / 0xAE (RAM retained when off)
void invertRect(int16_t x, int16_t y, int16_t w, int16_t h);  // XOR-flip buffer region

}  // namespace display
