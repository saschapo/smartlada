#include "display.h"
#include <Wire.h>
#include "../ui/ui_font.h"
#if UI_TFT
#include <Arduino_GFX_Library.h>
#endif

namespace display {

static constexpr uint8_t PIN_SCL = 18, PIN_SDA = 19, ADDR = 0x3C;
static constexpr uint8_t W = 128, H = 64;

#if UI_TFT
// J4 reused: SCL/IO18=SCLK, SDA/IO19=MOSI, KEY4/IO20=DC; CS and RES come from the J8 debug
// UART (RX/IO17, TX/IO16), which is therefore unavailable in this build. BLK is not driven.
// The 128x64 frame is fitted to the panel keeping its aspect (portrait: 240x120, x1.875),
// nearest-neighbour so pixels stay crisp. There is no TE line, so to keep tearing and bus time
// down display() streams only the rectangle that changed since the last frame, in one window.
static constexpr uint8_t PIN_DC = 20, PIN_CS = 17, PIN_RES = 16;
static constexpr int32_t SPI_HZ = 40000000;
static constexpr uint8_t ROTATION = 0;        // portrait
static constexpr uint8_t DIM_BELOW = 16;      // contrast under this = the menu's dim state
static Arduino_DataBus* bus = new Arduino_ESP32SPI(PIN_DC, PIN_CS, PIN_SCL, PIN_SDA, GFX_NOT_DEFINED);
static Arduino_TFT* tft = new Arduino_ST7789(bus, PIN_RES, ROTATION, false, 240, 320);

static int16_t  dw, dh, dx, dy;               // fitted output size and origin
static uint8_t  srcX[320], srcY[320];         // source pixel under each output column / row
static uint16_t line[320];
static uint8_t  prev[W * H / 8];              // last frame pushed, for the dirty rectangle
static uint8_t* fb;                           // = oled.buffer (protected)
static uint16_t fg = 0xFFFF;
static bool on = true, inv = false, argContrast = false, full = true;

TftOled oled;

bool TftOled::begin() {
  fb = buffer = (uint8_t*)calloc(W * H / 8, 1);
  if (!buffer || !tft->begin(SPI_HZ)) return false;
  int16_t tw = tft->width(), th = tft->height();
  if ((int32_t)tw * H <= (int32_t)th * W) { dw = tw; dh = (int32_t)tw * H / W; }
  else                                    { dh = th; dw = (int32_t)th * W / H; }
  dx = (tw - dw) / 2; dy = (th - dh) / 2;
  for (int16_t o = 0; o < dw; o++) srcX[o] = (uint8_t)((2 * o + 1) * W / (2 * dw));   // pixel centres
  for (int16_t o = 0; o < dh; o++) srcY[o] = (uint8_t)((2 * o + 1) * H / (2 * dh));
  tft->fillScreen(RGB565_BLACK);
  full = true;
  return true;
}

void TftOled::display() {
  if (!buffer) return;
  int16_t x0 = W, x1 = -1, p0 = H / 8, p1 = -1;               // dirty box: columns x pages
  for (int16_t p = 0; p < H / 8; p++)
    for (int16_t x = 0; x < W; x++) {
      int16_t i = p * W + x;
      if (!full && fb[i] == prev[i]) continue;
      if (x < x0) x0 = x;
      if (x > x1) x1 = x;
      if (p < p0) p0 = p;
      p1 = p;
    }
  if (x1 < 0) return;                                          // nothing changed
  memcpy(prev, fb, sizeof(prev));
  full = false;

  int16_t ox0 = 0, ox1 = dw - 1, oy0 = 0, oy1 = dh - 1;
  while (srcX[ox0] < x0) ox0++;
  while (srcX[ox1] > x1) ox1--;
  while (srcY[oy0] < p0 * 8) oy0++;
  while (srcY[oy1] > p1 * 8 + 7) oy1--;
  uint16_t w = ox1 - ox0 + 1;

  tft->startWrite();
  tft->writeAddrWindow(dx + ox0, dy + oy0, w, oy1 - oy0 + 1);
  int16_t built = -1;
  for (int16_t oy = oy0; oy <= oy1; oy++) {
    uint8_t y = srcY[oy];
    if (y != built) {                                          // consecutive rows often repeat
      const uint8_t* row = fb + (y / 8) * W;
      uint8_t bit = 1 << (y & 7);
      for (int16_t o = 0; o < w; o++)
        line[o] = (on && ((row[srcX[ox0 + o]] & bit) != 0) != inv) ? fg : 0;
      built = y;
    }
    bus->writePixels(line, w);
  }
  tft->endWrite();
}

// Only the dim state (menu sets contrast ~1%) greys the picture; any normal contrast shows pure
// white, since without BLK control a grey level only lowers contrast, it cannot dim the panel.
void TftOled::ssd1306_command(uint8_t c) {
  if (argContrast) { argContrast = false; fg = c < DIM_BELOW ? 0x4208 : 0xFFFF; }   // 0x4208 ~ 25% grey
  else if (c == 0x81) { argContrast = true; return; }
  else if (c == 0xA6 || c == 0xA7) inv = (c == 0xA7);
  else if (c == 0xAE || c == 0xAF) on = (c == 0xAF);
  else return;
  full = true;
  display();
}

bool begin() { return oled.begin(); }
#else
Adafruit_SSD1306 oled(W, H, &Wire, -1);

bool begin() {
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, ADDR)) return false;
  oled.ssd1306_command(0xD5); oled.ssd1306_command(0xF0);   // recommended clock
  return true;
}
#endif

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
