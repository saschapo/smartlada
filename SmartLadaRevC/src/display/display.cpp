#include "display.h"
#include <Wire.h>
#include "../ui/ui_font.h"

namespace display {

static constexpr uint8_t PIN_SCL = 18, PIN_SDA = 19, ADDR = 0x3C;
static constexpr uint8_t W = 128, H = 64;

Adafruit_SSD1306 oled(W, H, &Wire, -1);

bool begin() {
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, ADDR)) return false;
  oled.ssd1306_command(0xD5); oled.ssd1306_command(0xF0);   // recommended clock
  return true;
}

void setBrightness(uint8_t c) { oled.ssd1306_command(0x81); oled.ssd1306_command(c); }
void setInverse(bool inv)     { oled.ssd1306_command(inv ? 0xA7 : 0xA6); }
void power(bool on)           { oled.ssd1306_command(on ? 0xAF : 0xAE); }

// XOR-invert a rectangle in the frame buffer (used for the brightness-fill trick:
// text over the filled part shows inverted). Call before display().
void invertRect(int16_t x, int16_t y, int16_t w, int16_t h) {
  uint8_t* b = oled.getBuffer();
  for (int16_t yy = y; yy < y + h; yy++) {
    if (yy < 0 || yy >= H) continue;
    for (int16_t xx = x; xx < x + w; xx++) {
      if (xx < 0 || xx >= W) continue;
      b[xx + (yy / 8) * W] ^= (1 << (yy & 7));
    }
  }
}

// Centred title (scale 2 if it fits, else scale 1) + tiny sub-line at the bottom.
void splash(const char* title, const char* sub) {
  oled.clearDisplay();

  oled.setFont(&orp_bold);
  int16_t bx, by; uint16_t bw, bh;
  uint8_t size = 2;
  oled.setTextSize(size);
  oled.getTextBounds(title, 0, 0, &bx, &by, &bw, &bh);
  if (bw > W) { size = 1; oled.setTextSize(size); oled.getTextBounds(title, 0, 0, &bx, &by, &bw, &bh); }
  int16_t tx = (W - (int16_t)bw) / 2 - bx;
  int16_t ty = 11 - by;                       // titles nudged 15px up
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(tx, ty);
  oled.print(title);

  oled.setTextSize(1);
  oled.setFont(&dweep);
  oled.getTextBounds(sub, 0, 0, &bx, &by, &bw, &bh);
  oled.setCursor((W - (int16_t)bw) / 2 - bx, 47);      // 15px up from the bottom
  oled.print(sub);

  oled.setFont(nullptr);                       // leave default for callers
  oled.display();
}

}  // namespace display
