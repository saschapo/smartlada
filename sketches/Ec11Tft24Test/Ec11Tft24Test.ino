// Ec11Tft24Test - standalone bench test for a 2.4" SPI TFT + EC11 encoder module.
//
// Module: 240x320 SPI TFT (labeled ST7789, may actually be ILI9341) with an
// on-board EC11 rotary encoder (A/B/PUSH) and a separate tactile key (K0).
// All encoder/button lines have on-board 10K pull-ups + 100nF RC debounce and
// are ACTIVE-LOW (idle HIGH). Backlight (BLK) is an ACTIVE-HIGH low-side BJT
// switch (SS8050) - drive HIGH to light, PWM for dimming.
//
// Target: ESP32-C6 dev board, powered from 3V3.
// Build (native USB CDC):
//   arduino-cli compile --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc sketches/Ec11Tft24Test
//   arduino-cli upload  --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc -p <port> sketches/Ec11Tft24Test
//
// UX: PUSH (short) = next test, K0 (short) = prev test, K0 (hold) = reset counters.
//     Encoder knob drives the value in the Encoder and Backlight tests.

#include <Arduino_GFX_Library.h>
#include <ESP32Encoder.h>
#include "checkmark_ok_64_64_28f.h"   // 64x64 1bpp, 28 frames (drawBitmap mask)
#include "earth_64_64_24f.h"          // 64x64 RGB565, 24 frames (draw16bitRGBBitmap)

#define ANIM_W 64
#define ANIM_H 64
#define ANIM_FRAMES 28

// Short color aliases (Arduino_GFX exposes RGB565_* names)
#define BLACK   RGB565_BLACK
#define WHITE   RGB565_WHITE
#define RED     RGB565_RED
#define GREEN   RGB565_GREEN
#define BLUE    RGB565_BLUE
#define YELLOW  RGB565_YELLOW
#define CYAN    RGB565_CYAN
#define MAGENTA RGB565_MAGENTA

// ---------------------------------------------------------------------------
// Wiring (change here if you soldered to different GPIOs)
// ---------------------------------------------------------------------------
#define PIN_TFT_SCLK 6   // SCL
#define PIN_TFT_MOSI 7   // SDA
#define PIN_TFT_CS   10
#define PIN_TFT_DC   11  // A0
#define PIN_TFT_RES  18
#define PIN_TFT_BLK  19  // active-HIGH, LEDC PWM
#define PIN_ENC_A    2   // PCNT
#define PIN_ENC_B    3   // PCNT
#define PIN_ENC_PUSH 4   // encoder shaft button, active-low
#define PIN_KEY0     5   // separate K0 key, active-low

// ---------------------------------------------------------------------------
// Controller select: try ST7789 first (as labeled). If the image is garbled,
// wrongly offset, or colors will not correct, set this to 1 for ILI9341.
// ---------------------------------------------------------------------------
#define USE_ILI9341 0
#define TFT_IPS     false         // this panel shows correct colors with INVOFF
#define TFT_ROTATION 1            // 1 or 3 -> landscape 320x240
#define TFT_SPI_HZ  40000000L     // start 40 MHz; try 80 MHz in Bench test

// Backlight PWM
#define BLK_PWM_FREQ 20000
#define BLK_PWM_BITS 8

Arduino_DataBus *bus = new Arduino_ESP32SPI(
    PIN_TFT_DC, PIN_TFT_CS, PIN_TFT_SCLK, PIN_TFT_MOSI, GFX_NOT_DEFINED);

#if USE_ILI9341
Arduino_GFX *gfx = new Arduino_ILI9341(bus, PIN_TFT_RES, TFT_ROTATION, TFT_IPS, 240, 320);
const char *kControllerName = "ILI9341";
#else
Arduino_GFX *gfx = new Arduino_ST7789(bus, PIN_TFT_RES, TFT_ROTATION, TFT_IPS, 240, 320);
const char *kControllerName = "ST7789";
#endif

ESP32Encoder encoder;

// ---------------------------------------------------------------------------
// Simple active-low button with debounce, edge and long-press detection
// ---------------------------------------------------------------------------
struct Button {
  uint8_t pin;
  bool stable = true;       // debounced level (true = released/HIGH)
  bool lastRead = true;
  uint32_t lastChangeMs = 0;
  uint32_t pressStartMs = 0;
  bool pressedEdge = false; // one-shot: went down this poll
  bool longFired = false;
  uint32_t pressCount = 0;

  void begin(uint8_t p) {
    pin = p;
    pinMode(pin, INPUT);    // external 10K pull-up on module
    stable = lastRead = digitalRead(pin);
  }
  // Returns after updating: pressedEdge (short-press start), and sets longFired.
  void poll(uint16_t debounceMs, uint16_t longMs) {
    pressedEdge = false;
    bool r = digitalRead(pin);
    uint32_t now = millis();
    if (r != lastRead) { lastRead = r; lastChangeMs = now; }
    if ((now - lastChangeMs) >= debounceMs && r != stable) {
      stable = r;
      if (stable == LOW) {          // pressed (active-low)
        pressedEdge = true;
        pressStartMs = now;
        longFired = false;
        pressCount++;
      }
    }
    if (stable == LOW && !longFired && (now - pressStartMs) >= longMs) {
      longFired = true;             // long-press latched (consume via flag)
    }
  }
  bool isDown() const { return stable == LOW; }
};

Button btnPush, btnKey0;

// ---------------------------------------------------------------------------
// Test modes
// ---------------------------------------------------------------------------
enum Mode : uint8_t {
  M_INFO, M_COLORS, M_BARS, M_BORDER, M_ENCODER, M_BUTTONS, M_BENCH, M_BACKLIGHT, M_ANIM, M_ANIM_GIF,
  M_COUNT
};
const char *kModeName[M_COUNT] = {
  "Info", "Colors", "Bars+Gradient", "Border", "Encoder", "Buttons", "Bench", "Backlight", "Anim OK", "Earth"};

uint8_t mode = M_INFO;
bool modeEntered = false;   // set false to force a full redraw of the mode
bool gRedraw = false;       // true for the first update after entering a mode

int W, H;                   // current screen size
uint8_t brightness = 255;   // backlight duty
uint16_t animDelayMs = 40;  // Anim view: ms per frame (knob-adjustable)

// encoder detent value (fullQuad -> 4 counts per detent)
long lastDetents = 0;
long encMax = 0, encMin = 0;      // range seen (jitter check)
uint32_t lastSpeedMs = 0;
long lastSpeedCount = 0;
int detentsPerSec = 0;

// ---------------------------------------------------------------------------
static long readDetents() { return (long)(encoder.getCount() / 4); }

static void setBrightness(uint8_t b) {
  brightness = b;
  ledcWrite(PIN_TFT_BLK, b);
}

static void header(const char *name) {
  gfx->fillRect(0, 0, W, 22, RGB565(0, 0, 40));
  gfx->setTextColor(YELLOW);
  gfx->setTextSize(2);
  gfx->setCursor(4, 4);
  gfx->print(name);
}

// ---------------------------------------------------------------------------
static void enterMode() {
  gfx->fillScreen(BLACK);
  header(kModeName[mode]);
  gfx->setTextSize(2);
  switch (mode) {
    case M_INFO: {
      gfx->setTextColor(WHITE);
      gfx->setTextSize(1);
      int y = 30;
      gfx->setCursor(4, y); gfx->printf("Controller: %s  IPS=%d\n", kControllerName, (int)TFT_IPS);
      y += 12; gfx->setCursor(4, y); gfx->printf("Res: %dx%d  rot=%d  SPI=%ldMHz", W, H, TFT_ROTATION, TFT_SPI_HZ / 1000000);
      y += 16; gfx->setCursor(4, y); gfx->print("SCLK=6 MOSI=7 CS=10 DC=11");
      y += 12; gfx->setCursor(4, y); gfx->print("RES=18 BLK=19");
      y += 12; gfx->setCursor(4, y); gfx->print("ENC A=2 B=3 PUSH=4  K0=5");
      y += 18; gfx->setTextColor(CYAN); gfx->setCursor(4, y); gfx->print("PUSH: next test");
      y += 12; gfx->setCursor(4, y); gfx->print("K0:   prev test");
      y += 12; gfx->setCursor(4, y); gfx->print("K0 hold: reset counters");
      y += 12; gfx->setCursor(4, y); gfx->print("Knob: value in Encoder/Backlight");
      break;
    }
    case M_BARS: {
      int bw = W / 4;
      gfx->fillRect(0 * bw, 24, bw, H - 24, RED);
      gfx->fillRect(1 * bw, 24, bw, H - 24, GREEN);
      gfx->fillRect(2 * bw, 24, bw, H - 24, BLUE);
      gfx->fillRect(3 * bw, 24, W - 3 * bw, H - 24, WHITE);
      // grayscale + color gradient across the bottom strip
      for (int x = 0; x < W; x++) {
        uint8_t v = (uint8_t)(x * 255 / (W - 1));
        gfx->drawFastVLine(x, H - 20, 10, RGB565(v, v, v));
        gfx->drawFastVLine(x, H - 10, 10, RGB565(v, 0, 255 - v));
      }
      break;
    }
    case M_BORDER: {
      gfx->drawRect(0, 0, W, H, WHITE);
      gfx->drawRect(1, 1, W - 2, H - 2, RED);
      // corner ticks to spot cropped/offset edges
      const int L = 14;
      gfx->drawFastHLine(0, 0, L, GREEN);       gfx->drawFastVLine(0, 0, L, GREEN);
      gfx->drawFastHLine(W - L, H - 1, L, GREEN); gfx->drawFastVLine(W - 1, H - L, L, GREEN);
      gfx->drawLine(W / 2 - 8, H / 2, W / 2 + 8, H / 2, CYAN);
      gfx->drawLine(W / 2, H / 2 - 8, W / 2, H / 2 + 8, CYAN);
      break;
    }
    case M_ANIM: {
      int ix = (W - ANIM_W) / 2, iy = 96;
      gfx->drawRect(ix - 6, iy - 6, ANIM_W + 12, ANIM_H + 12, RGB565(40, 40, 40));
      gfx->setTextColor(WHITE); gfx->setTextSize(1);
      gfx->setCursor(4, 28); gfx->print("64x64 1bpp x28f, drawBitmap mask");
      break;
    }
    case M_ANIM_GIF: {
      int ix = (W - EARTH_64_64_24F_W) / 2, iy = 96;
      gfx->drawRect(ix - 6, iy - 6, EARTH_64_64_24F_W + 12, EARTH_64_64_24F_H + 12, RGB565(40, 40, 40));
      gfx->setTextColor(WHITE); gfx->setTextSize(1);
      gfx->setCursor(4, 28); gfx->print("64x64 RGB565 x24f, draw16bitRGBBitmap");
      break;
    }
    case M_ENCODER:
    case M_BUTTONS:
    case M_BACKLIGHT:
      // dynamic content drawn in updateMode()
      break;
    case M_BENCH:
      break;
    default: break;
  }
  modeEntered = true;
  gRedraw = true;   // force dynamic content to draw once on entry
}

// ---------------------------------------------------------------------------
static void updateColors() {
  static uint32_t last = 0;
  static uint8_t idx = 0;
  const uint16_t pal[] = {RED, GREEN, BLUE, WHITE, BLACK, RGB565(128, 128, 128)};
  const char *nm[] = {"RED", "GREEN", "BLUE", "WHITE", "BLACK", "GRAY50"};
  if (gRedraw) { idx = 0; last = 0; }
  if (millis() - last >= 900) {
    last = millis();
    gfx->fillScreen(pal[idx]);
    gfx->setTextColor(idx == 4 ? WHITE : BLACK);
    gfx->setTextSize(2);
    gfx->setCursor(6, 6);
    gfx->print(nm[idx]);
    idx = (idx + 1) % 6;
  }
}

static void updateEncoder() {
  // Fixed-width field + opaque background: new text overwrites old in place,
  // no separate erase -> no flicker.
  static long shown = LONG_MIN;
  static uint32_t last = 0;
  if (gRedraw) { shown = LONG_MIN; last = 0; }
  long d = lastDetents;
  if (d != shown) {
    shown = d;
    gfx->setTextColor(WHITE, BLACK);
    gfx->setTextSize(7);
    gfx->setCursor(8, 46);
    gfx->printf("%6ld", d);                   // fixed 6-char field
  }
  if (millis() - last > 120) {
    last = millis();
    gfx->setTextColor(CYAN, BLACK);
    gfx->setTextSize(1);
    gfx->setCursor(4, H - 36); gfx->printf("raw=%6lld min=%5ld max=%5ld ", (long long)encoder.getCount(), encMin, encMax);
    gfx->setCursor(4, H - 24); gfx->printf("speed=%4d det/s  push=%s  ", detentsPerSec, btnPush.isDown() ? "DN" : "up");
    gfx->setCursor(4, H - 12); gfx->print("K0 hold = zero    ");
  }
}

static void updateButtons() {
  // Redraw a box only when its state or counter changes -> no constant flicker.
  auto box = [&](int x, const char *label, bool down, uint32_t cnt) {
    int bw = W / 2 - 8;
    gfx->fillRect(x, 40, bw, 80, down ? GREEN : RGB565(40, 40, 40));
    gfx->setTextColor(down ? BLACK : WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(x + 8, 52); gfx->print(label);
    gfx->setCursor(x + 8, 78); gfx->print(down ? "DOWN" : "up  ");
    gfx->setTextSize(1);
    gfx->setCursor(x + 8, 102); gfx->printf("count=%lu ", (unsigned long)cnt);
  };
  static int lp = -1, lk = -1;
  static uint32_t lpc = ~0u, lkc = ~0u;
  static uint32_t last = 0;
  if (gRedraw) { lp = lk = -1; lpc = lkc = ~0u; last = 0; }
  bool pd = btnPush.isDown(), kd = btnKey0.isDown();
  if ((int)pd != lp || btnPush.pressCount != lpc) { lp = pd; lpc = btnPush.pressCount; box(4, "PUSH", pd, btnPush.pressCount); }
  if ((int)kd != lk || btnKey0.pressCount != lkc) { lk = kd; lkc = btnKey0.pressCount; box(W / 2 + 4, "K0", kd, btnKey0.pressCount); }
  if (millis() - last > 150) {
    last = millis();
    gfx->setTextColor(YELLOW, BLACK); gfx->setTextSize(1);
    gfx->setCursor(4, H - 12);
    gfx->printf("longPUSH=%d  longK0=%d ", btnPush.longFired ? 1 : 0, btnKey0.longFired ? 1 : 0);
  }
}

static void updateBacklight() {
  static int shown = -1;
  if (gRedraw) shown = -1;
  if ((int)brightness == shown) return;
  shown = brightness;
  int pct = brightness * 100 / 255;
  gfx->setTextColor(WHITE, BLACK); gfx->setTextSize(4);
  gfx->setCursor(8, 46); gfx->printf("%3d%% ", pct);
  int bw = (W - 16) * brightness / 255;
  gfx->fillRect(8, 110, bw, 24, YELLOW);                 // filled part
  gfx->fillRect(8 + bw, 110, (W - 16) - bw, 24, RGB565(30, 30, 30)); // rest
  gfx->setTextColor(CYAN, BLACK); gfx->setTextSize(1);
  gfx->setCursor(4, H - 14); gfx->printf("duty=%3u  knob to adjust", brightness);
}

static void updateBench() {
  // Square glides (erase only its previous position) -> minimal flicker; only
  // tearing on the moving edges remains visible (no TE line on this module).
  // A full-screen fill is measured once per second for the fill-time metric.
  static int x = 0, dir = 4, px = -100;
  static uint32_t lastFull = 0, shownFull = 0, shownRect = 0;
  static float fps = 0;
  uint32_t loopStart = micros();

  if (gRedraw) { lastFull = 0; px = -100; }

  if (millis() - lastFull > 1000) {
    lastFull = millis();
    uint32_t t0 = micros();
    gfx->fillScreen(BLACK);                   // measured full-frame fill (brief flash)
    shownFull = micros() - t0;
    header(kModeName[mode]);                   // redraw static parts after wipe
    px = -100;                                 // previous square is gone
  }

  if (px >= 0) gfx->fillRect(px, 70, 40, 40, BLACK);  // erase old square
  uint32_t t1 = micros();
  gfx->fillRect(x, 70, 40, 40, RED);          // draw new square
  shownRect = micros() - t1;
  px = x; x += dir;
  if (x <= 0 || x >= W - 40) dir = -dir;

  float inst = 1000000.0f / (float)(micros() - loopStart);
  fps = fps * 0.9f + inst * 0.1f;

  gfx->setTextColor(WHITE, BLACK); gfx->setTextSize(1);
  gfx->setCursor(4, 28); gfx->printf("fullFill=%6lu us ", (unsigned long)shownFull);
  gfx->setCursor(4, 40); gfx->printf("fillRect40=%5lu us ", (unsigned long)shownRect);
  gfx->setCursor(4, H - 14); gfx->printf("~%4d loop/s ", (int)fps);
}

static void updateAnim() {
  // Play the 28-frame mask with drawBitmap(fg,bg): each frame fully overwrites
  // the 64x64 region -> no smear, no separate clear. Knob adjusts frame delay.
  static uint8_t frame = 0;
  static uint32_t lastTick = 0, holdStart = 0;
  static bool holding = false;
  static long knob0 = 0;
  static uint16_t shownFps = 0;
  const int ix = (W - ANIM_W) / 2, iy = 96;

  if (gRedraw) { frame = 0; lastTick = 0; holding = false; knob0 = lastDetents; shownFps = 0; }

  // knob -> speed (delta): CW = faster (shorter delay)
  long dk = lastDetents - knob0;
  knob0 = lastDetents;
  if (dk) {
    int v = (int)animDelayMs - (int)dk * 5;
    if (v < 10) v = 10; if (v > 200) v = 200;
    animDelayMs = (uint16_t)v;
  }

  uint32_t now = millis();
  if (!holding) {
    if (now - lastTick >= animDelayMs) {
      lastTick = now;
      gfx->drawBitmap(ix, iy, checkmark_ok_64_64_28f_frames[frame], ANIM_W, ANIM_H, GREEN, BLACK);
      if (frame + 1 < ANIM_FRAMES) frame++;
      else { holding = true; holdStart = now; }   // hold on last frame
    }
  } else if (now - holdStart >= 800) {
    holding = false;
    frame = 0;
    lastTick = now;
  }

  uint16_t fps = 1000 / (animDelayMs ? animDelayMs : 1);
  static uint32_t lastTxt = 0;
  if (gRedraw || fps != shownFps || now - lastTxt > 150) {
    lastTxt = now;
    shownFps = fps;
    gfx->setTextColor(CYAN, BLACK); gfx->setTextSize(1);
    gfx->setCursor(4, H - 24); gfx->printf("frame %2d/%d  %3u fps  ", frame + 1, ANIM_FRAMES, fps);
    gfx->setCursor(4, H - 12); gfx->print("knob = speed        ");
  }
}

static void updateAnimGif() {
  // Color animation via draw16bitRGBBitmap: each frame fully repaints the 64x64
  // region (opaque) -> no smear. Continuous loop (spinning earth). Knob = speed.
  static uint8_t frame = 0;
  static uint32_t lastTick = 0;
  static long knob0 = 0;
  static uint16_t shownFps = 0;
  const int ix = (W - EARTH_64_64_24F_W) / 2, iy = 96;

  if (gRedraw) { frame = 0; lastTick = 0; knob0 = lastDetents; shownFps = 0; }

  long dk = lastDetents - knob0;
  knob0 = lastDetents;
  if (dk) {
    int v = (int)animDelayMs - (int)dk * 5;
    if (v < 10) v = 10; if (v > 200) v = 200;
    animDelayMs = (uint16_t)v;
  }

  uint32_t now = millis();
  if (now - lastTick >= animDelayMs) {
    lastTick = now;
    gfx->draw16bitRGBBitmap(ix, iy, earth_64_64_24f_frames[frame],
                            EARTH_64_64_24F_W, EARTH_64_64_24F_H);
    frame = (frame + 1) % EARTH_64_64_24F_FRAMES;
  }

  uint16_t fps = 1000 / (animDelayMs ? animDelayMs : 1);
  static uint32_t lastTxt = 0;
  if (gRedraw || fps != shownFps || now - lastTxt > 150) {
    lastTxt = now;
    shownFps = fps;
    gfx->setTextColor(CYAN, BLACK); gfx->setTextSize(1);
    gfx->setCursor(4, H - 24); gfx->printf("frame %2d/%d  %3u fps  ", frame + 1, EARTH_64_64_24F_FRAMES, fps);
    gfx->setCursor(4, H - 12); gfx->print("knob = speed        ");
  }
}

// ---------------------------------------------------------------------------
static void gotoMode(int8_t delta) {
  mode = (uint8_t)((mode + M_COUNT + delta) % M_COUNT);
  modeEntered = false;
  Serial.printf("[mode] -> %s\n", kModeName[mode]);
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);     // do not block loop if USB host is not reading

  // Backlight on before anything else (active-HIGH); floating/LOW = dark.
  ledcAttach(PIN_TFT_BLK, BLK_PWM_FREQ, BLK_PWM_BITS);
  setBrightness(255);

  if (!gfx->begin(TFT_SPI_HZ)) {
    Serial.println("[gfx] begin() FAILED");
  }
  gfx->fillScreen(BLACK);
  W = gfx->width();
  H = gfx->height();
  Serial.printf("[gfx] %s %dx%d\n", kControllerName, W, H);

  btnPush.begin(PIN_ENC_PUSH);
  btnKey0.begin(PIN_KEY0);

  ESP32Encoder::useInternalWeakPullResistors = puType::none; // external pull-ups
  // B,A order (swapped) so CW increments; swap back if direction is wrong.
  encoder.attachFullQuad(PIN_ENC_B, PIN_ENC_A);
  encoder.setFilter(1023);      // glitch filter (APB cycles)
  encoder.clearCount();

  lastSpeedMs = millis();
}

void loop() {
  // --- inputs ---
  btnPush.poll(15, 700);
  btnKey0.poll(15, 700);

  lastDetents = readDetents();
  if (lastDetents > encMax) encMax = lastDetents;
  if (lastDetents < encMin) encMin = lastDetents;

  // rotation speed (detents/s) every 200 ms
  if (millis() - lastSpeedMs >= 200) {
    long c = lastDetents;
    detentsPerSec = (int)((c - lastSpeedCount) * 1000 / (long)(millis() - lastSpeedMs));
    if (detentsPerSec < 0) detentsPerSec = -detentsPerSec;
    lastSpeedCount = c;
    lastSpeedMs = millis();
  }

  // --- navigation ---
  if (btnPush.pressedEdge) gotoMode(+1);
  if (btnKey0.pressedEdge) gotoMode(-1);

  // K0 long-press: reset counters (consume by clearing the latch)
  if (btnKey0.longFired) {
    btnKey0.longFired = false;
    encoder.clearCount();
    encMax = encMin = 0;
    lastSpeedCount = 0;
    btnPush.pressCount = btnKey0.pressCount = 0;
    modeEntered = false;
    Serial.println("[reset] counters cleared");
  }

  // knob adjusts brightness in backlight mode (by delta, so entering never jumps)
  static uint8_t prevMode = 0xFF;
  static long blKnob = 0;
  if (mode == M_BACKLIGHT) {
    if (prevMode != M_BACKLIGHT) blKnob = lastDetents;   // capture on entry
    long delta = lastDetents - blKnob;
    blKnob = lastDetents;
    int v = (int)brightness + (int)(delta * 8);
    if (v < 0) v = 0; if (v > 255) v = 255;
    if ((uint8_t)v != brightness) setBrightness((uint8_t)v);
  }
  prevMode = mode;

  // --- draw ---
  if (!modeEntered) enterMode();
  switch (mode) {
    case M_COLORS:    updateColors();    break;
    case M_ENCODER:   updateEncoder();   break;
    case M_BUTTONS:   updateButtons();   break;
    case M_BACKLIGHT: updateBacklight(); break;
    case M_ANIM:      updateAnim();       break;
    case M_ANIM_GIF:  updateAnimGif();    break;
    case M_BENCH:     updateBench();      break;
    default: break;   // INFO/BARS/BORDER are static
  }
  gRedraw = false;

  if (mode != M_BENCH) delay(2);   // Bench runs flat out
}
