// RevBFontBrowser -- browse hand-drawn bitmap fonts on the SmartLada Rev B OLED
// to pick a UI/menu font. These are proper pixel fonts from Tecate/bitmap-fonts
// (github.com/Tecate/bitmap-fonts), full ASCII, designed on the grid:
//   ProggySquare, sgi-screen (Scr8/10/12, ScrB12 bold), dweep (narrow 5px),
//   Opti, orp (book/medium/bold), kakwa (n/b). Sources in fonts/tecate/.
// Header fonts_bmp.h is built by tools/bdf2gfx.py (also reads PCF; ASCII range).
//
// GFXfonts always render 1:1 (setTextSize stays 1) -- there is no runtime scale.
//
// The top status line always uses the built-in 5x7 font (font name + index).
// The area below renders a sample in the SELECTED font so you can judge it.
//
// Rev B pinout: SCL=GPIO18 SDA=GPIO19  OLED 0x3C (SSD1315).
//   K1(^)=GPIO20  K2(v)=GPIO21  K3(#)=GPIO22  K4(*)=GPIO23
// Controls:
//   K1 : previous font        K2 : next font
//   K3 : cycle sample (MENU / ALNUM / NUMBERS)
//   K4 : toggle inverse display (0xA6/0xA7, instant)
//
// Build: arduino-cli compile --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc sketches/RevBFontBrowser

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "fonts_bmp.h"      // Tecate bitmap fonts; fonts_pixel.h / fonts_all.h remain for the old local set

static constexpr uint8_t PIN_SCL = 18;
static constexpr uint8_t PIN_SDA = 19;
static constexpr uint8_t OLED_ADDR = 0x3C;
static constexpr uint8_t W = 128, H = 64;
static constexpr int16_t BODY_Y = 12;          // sample area starts below status bar
static constexpr uint8_t CONTRAST = 0x9F;

struct Button { uint8_t pin; bool stable; bool lastRaw; uint32_t lastEdge; };
static Button btn[4] = {
  {20, HIGH, HIGH, 0}, {21, HIGH, HIGH, 0}, {22, HIGH, HIGH, 0}, {23, HIGH, HIGH, 0},
};
static constexpr uint32_t DEBOUNCE_MS = 25;

static uint8_t pollButtons(uint32_t now) {
  uint8_t pressed = 0;
  for (uint8_t i = 0; i < 4; i++) {
    bool raw = digitalRead(btn[i].pin);
    if (raw != btn[i].lastRaw) { btn[i].lastRaw = raw; btn[i].lastEdge = now; }
    else if ((now - btn[i].lastEdge) >= DEBOUNCE_MS && raw != btn[i].stable) {
      btn[i].stable = raw;
      if (btn[i].stable == LOW) pressed |= (1 << i);
    }
  }
  return pressed;
}

enum Sample { S_MENU = 0, S_ALNUM, S_NUM, S_COUNT };
static const char* SAMPLE_NAME[] = {"MENU", "ALNUM", "NUM"};

// Menu mockup that mirrors the real UI: per-channel static + animation + timing.
static const char* MENU_LINES[] = {
  "> Static Ch1",
  "R255 G128 B64",
  "Anim: Rainbow",
  "Speed 1.5s 80%",
};
static const char* ALNUM_LINES[] = {
  "ABCDEFGHIJKLM",
  "NOPQRSTUVWXYZ",
  "abcdefghijklm",
  "nopqrstuvwxyz",
};
static const char* NUM_LINES[] = {
  "0123456789",
  "12:34  -5.6",
  "255 100% 1.5s",
  "+/- .,:%",
};

static uint8_t fontIdx = 0;
static uint8_t sample = S_MENU;
static bool    inverse = false;

static Adafruit_SSD1306 oled(W, H, &Wire, -1);

static uint8_t fontLineHeight(uint8_t idx) {
  uint8_t ya = pgm_read_byte(&FONT_TABLE[idx].font->yAdvance);
  if (ya < 8) ya = 8;                            // guard tiny/blank fonts
  return ya;
}

static const char* const* sampleLines() {
  switch (sample) {
    case S_ALNUM: return ALNUM_LINES;
    case S_NUM:   return NUM_LINES;
    default:      return MENU_LINES;
  }
}

static void draw() {
  oled.clearDisplay();

  // Status bar: built-in font, "idx/total  name".
  oled.setFont(nullptr);
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.printf("%u/%u %.10s", fontIdx + 1, FONT_COUNT, FONT_TABLE[fontIdx].name);
  oled.setCursor(104, 0);
  oled.print(SAMPLE_NAME[sample]);
  oled.drawFastHLine(0, 9, W, SSD1306_WHITE);

  // Sample: selected font. Cursor y in Adafruit custom fonts is the baseline.
  oled.setFont(FONT_TABLE[fontIdx].font);
  uint8_t lh = fontLineHeight(fontIdx);
  const char* const* lines = sampleLines();
  int16_t y = BODY_Y + lh;                       // first baseline
  for (uint8_t i = 0; i < 4 && y <= H; i++) {
    oled.setCursor(0, y);
    oled.print(lines[i]);
    y += lh;
  }

  oled.setFont(nullptr);                          // leave GFX in a known state
  oled.display();
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  delay(50);
  Serial.println();
  Serial.printf("RevBFontBrowser: %u fonts. K1/K2 font, K3 sample, K4 inverse\n", FONT_COUNT);

  for (uint8_t i = 0; i < 4; i++) pinMode(btn[i].pin, INPUT_PULLUP);

  Wire.begin(PIN_SDA, PIN_SCL);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("OLED not found at 0x3C -- check wiring"));
    while (true) { delay(1000); }
  }
  Wire.setClock(400000);
  oled.ssd1306_command(0xD5); oled.ssd1306_command(0xF0);   // recommended: max osc
  oled.ssd1306_command(0x81); oled.ssd1306_command(CONTRAST);
  draw();
}

void loop() {
  uint32_t now = millis();
  uint8_t hit = pollButtons(now);
  if (!hit) return;                              // static view: act only on a press

  if (hit & 0x01) fontIdx = (fontIdx == 0) ? FONT_COUNT - 1 : fontIdx - 1;
  if (hit & 0x02) fontIdx = (fontIdx + 1) % FONT_COUNT;
  if (hit & 0x04) sample = (sample + 1) % S_COUNT;
  if (hit & 0x08) { inverse = !inverse; oled.ssd1306_command(inverse ? 0xA7 : 0xA6); }

  Serial.printf("font=%u/%u %s sample=%s inverse=%d\n",
                fontIdx + 1, FONT_COUNT, FONT_TABLE[fontIdx].name,
                SAMPLE_NAME[sample], inverse);
  draw();
}
