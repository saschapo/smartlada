// RevBDisplayBench -- OLED refresh / frame-flush / tearing test bench (Rev B).
//
// Compares how the panel looks (eye + camera) under different panel frame-clock
// (SSD1306/SSD1315 cmd 0xD5) and I2C-speed presets, and lets you A/B a FULL vs
// PARTIAL screen flush to see the tearing on a moving 15x15 square.
//
// Tearing note: SSD1306/SSD1315 over I2C has NO TE (frame-sync) line, so true
// vsync is impossible. The panel scans GDDRAM to the glass asynchronously while
// we push the framebuffer, so a moving object gets sheared by 1-2 px. The only
// levers are (a) faster bus (shorter flush window) and (b) PARTIAL update -- flush
// only the 2 pages of the square's band (~40 B) instead of the full 1 KB, which
// shrinks the tear window from ~14-28 ms to well under 1 ms.
//
// Rev B pinout: SCL=GPIO18 SDA=GPIO19  OLED 0x3C (SSD1315, SSD1306-compatible)
//   K1(^)=GPIO20  K2(v)=GPIO21  K3(#)=GPIO22  K4(*)=GPIO23
// Controls:
//   K1 : next preset (panel clock + I2C speed, wraps)
//   K2 : toggle flush mode FULL <-> PARTIAL
//   K3 / K4 : square speed - / + (0 = stopped ... max = full cycle in 5 s)
//
// Build with USB CDC for Serial on the native USB port:
//   arduino-cli compile --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc sketches/RevBDisplayBench

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

static constexpr uint8_t PIN_SCL = 18;
static constexpr uint8_t PIN_SDA = 19;
static constexpr uint8_t OLED_ADDR = 0x3C;
static constexpr uint8_t W = 128, H = 64;

// ---- buttons ----------------------------------------------------------------
struct Button { uint8_t pin; bool stable; bool lastRaw; uint32_t lastEdge; };
static Button btn[4] = {
  {20, HIGH, HIGH, 0},  // K1 ^  next preset
  {21, HIGH, HIGH, 0},  // K2 v  toggle FULL/PARTIAL
  {22, HIGH, HIGH, 0},  // K3 #  speed -
  {23, HIGH, HIGH, 0},  // K4 *  speed +
};
static constexpr uint32_t DEBOUNCE_MS = 25;

static uint8_t pollButtons(uint32_t now) {   // bitmask of new presses
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

// ---- presets: panel clock (0xD5) + I2C speed --------------------------------
struct Preset { const char* name; uint8_t clkdiv; uint32_t i2cHz; };
static const Preset PRESETS[] = {
  {"DEFAULT", 0x80, 400000},
  {"FASTCLK", 0xF0, 400000},
  {"I2C 800", 0x80, 800000},
  {"MAXBOTH", 0xF0, 800000},
  {"SLOWCLK", 0x00, 400000},
};
static constexpr uint8_t NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);
static uint8_t presetIdx = 0;

// ---- flush mode -------------------------------------------------------------
enum FlushMode { FULL = 0, PARTIAL = 1 };
static uint8_t flushMode = FULL;

// ---- animation --------------------------------------------------------------
static constexpr uint8_t SQ = 15;
static constexpr uint8_t SQ_Y = H - SQ;            // 49 -> band = pages 6..7
static constexpr uint8_t SQ_P0 = SQ_Y / 8;         // 6
static constexpr uint8_t SQ_P1 = (H - 1) / 8;      // 7
static constexpr float   TRAVEL = W - SQ;          // 0..113 px
static constexpr float   VMAX = (2.0f * TRAVEL) / 5.0f;  // full cycle in 5 s
static constexpr uint8_t SPEED_MAX = 10;
static uint8_t speedLvl = 5;
static float   sqX = 0.0f, sqDir = 1.0f;
static int16_t prevXi = 0;

static Adafruit_SSD1306 oled(W, H, &Wire, -1);
static float    flushEmaUs = 0;
static uint16_t fps = 0, frameCnt = 0;
static uint32_t fpsWindow = 0;
static bool     needFull = true;   // force a full compose+flush next frame

static void applyPreset() {
  const Preset& p = PRESETS[presetIdx];
  oled.ssd1306_command(SSD1306_SETDISPLAYCLOCKDIV);  // 0xD5
  oled.ssd1306_command(p.clkdiv);
  Wire.setClock(p.i2cHz);
  flushEmaUs = 0;
  needFull = true;
}

// Compose the whole frame (header + info + square) into the Adafruit buffer.
static void buildFrame(int16_t xi) {
  const Preset& p = PRESETS[presetIdx];
  oled.clearDisplay();

  oled.fillRect(0, 0, W, 9, SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK);
  oled.setTextSize(1);
  oled.setCursor(1, 1);
  oled.printf("P%u/%u %s", presetIdx + 1, NUM_PRESETS, p.name);

  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 11);
  oled.printf("d5=0x%02X i2c=%luk", p.clkdiv, (unsigned long)(p.i2cHz / 1000));
  oled.setCursor(0, 20);
  oled.printf("flush=%4u us", (unsigned)flushEmaUs);
  oled.setCursor(0, 29);
  oled.printf("fps=%-3u spd=%u/%u", fps, speedLvl, SPEED_MAX);
  oled.setCursor(0, 38);
  oled.printf("mode=%s", flushMode == FULL ? "FULL" : "PART");

  oled.fillRect(xi, SQ_Y, SQ, SQ, SSD1306_WHITE);
}

// Flush only pages 6..7 for columns c0..c1 (the square's band). ~40 B typical.
static void flushRegion(uint8_t c0, uint8_t c1) {
  uint8_t* buf = oled.getBuffer();
  // All addressing commands in ONE transaction (control 0x00 = command stream).
  // Sending them via ssd1306_command() would be 7 separate I2C transactions,
  // ~0.5-1 ms each on ESP32 -> dominates a tiny partial flush.
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x00);
  Wire.write(SSD1306_MEMORYMODE); Wire.write(0x00);       // horizontal
  Wire.write(SSD1306_COLUMNADDR); Wire.write(c0); Wire.write(c1);
  Wire.write(SSD1306_PAGEADDR);   Wire.write(SQ_P0); Wire.write(SQ_P1);
  Wire.endTransmission();

  const uint8_t MAXCHUNK = 120;  // ESP32 Wire TX buffer is 128; keep 1 data txn
                                 // for a typical band (~34 B) to cut txn overhead
  uint8_t n = 0;
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x40);              // data stream
  for (uint8_t p = SQ_P0; p <= SQ_P1; p++) {
    for (uint16_t c = c0; c <= c1; c++) {
      Wire.write(buf[(uint16_t)p * W + c]);
      if (++n >= MAXCHUNK) {      // pointer persists across transmissions
        Wire.endTransmission();
        Wire.beginTransmission(OLED_ADDR);
        Wire.write(0x40);
        n = 0;
      }
    }
  }
  Wire.endTransmission();
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  delay(50);
  Serial.println();
  Serial.println(F("RevBDisplayBench: K1 preset, K2 FULL/PARTIAL, K3/K4 speed"));

  for (uint8_t i = 0; i < 4; i++) pinMode(btn[i].pin, INPUT_PULLUP);

  Wire.begin(PIN_SDA, PIN_SCL);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("OLED not found at 0x3C -- check wiring"));
    while (true) { delay(1000); }
  }
  applyPreset();
  fpsWindow = millis();
}

void loop() {
  uint32_t now = millis();

  uint8_t hit = pollButtons(now);
  if (hit & 0x01) { presetIdx = (presetIdx + 1) % NUM_PRESETS; applyPreset();
                    Serial.printf("preset %s\n", PRESETS[presetIdx].name); }
  if (hit & 0x02) { flushMode ^= 1; needFull = true;
                    Serial.printf("mode %s\n", flushMode == FULL ? "FULL" : "PARTIAL"); }
  if (hit & 0x04) { if (speedLvl > 0)         speedLvl--; needFull = true; }
  if (hit & 0x08) { if (speedLvl < SPEED_MAX) speedLvl++; needFull = true; }

  // framerate-independent motion
  static uint32_t lastUs = micros();
  uint32_t us = micros();
  float dt = (us - lastUs) * 1e-6f;
  lastUs = us;
  float vel = VMAX * (speedLvl / (float)SPEED_MAX);
  sqX += sqDir * vel * dt;
  if (sqX >= TRAVEL) { sqX = TRAVEL; sqDir = -1.0f; }
  else if (sqX <= 0) { sqX = 0;      sqDir =  1.0f; }
  int16_t xi = (int16_t)(sqX + 0.5f);

  if (flushMode == FULL || needFull) {
    buildFrame(xi);
    uint32_t t0 = micros();
    oled.display();
    uint32_t d = micros() - t0;
    if (flushMode == FULL) flushEmaUs = (flushEmaUs == 0) ? d : flushEmaUs * 0.9f + d * 0.1f;
    prevXi = xi;
    needFull = false;
    frameCnt++;
  } else if (xi != prevXi) {                 // PARTIAL: only the square band moved
    oled.fillRect(prevXi, SQ_Y, SQ, SQ, SSD1306_BLACK);
    oled.fillRect(xi,     SQ_Y, SQ, SQ, SSD1306_WHITE);
    uint8_t c0 = min(prevXi, xi);
    int16_t c1 = max(prevXi, xi) + SQ - 1; if (c1 > W - 1) c1 = W - 1;
    uint32_t t0 = micros();
    flushRegion(c0, (uint8_t)c1);
    uint32_t d = micros() - t0;
    flushEmaUs = (flushEmaUs == 0) ? d : flushEmaUs * 0.9f + d * 0.1f;
    prevXi = xi;
    frameCnt++;
  }
  // frameCnt counts actual flushes (redraws), not idle loop spins -> fps is the
  // real update rate.
  if (now - fpsWindow >= 1000) {
    fps = frameCnt; frameCnt = 0; fpsWindow = now;
    // Live stats go to Serial. In PARTIAL we deliberately do NOT force a full
    // flush here -- that would add a 1/s tear blip and muddy the A/B. The
    // on-screen stats line stays frozen until a button triggers a full compose.
    Serial.printf("mode=%s preset=%s flush=%uus fps=%u spd=%u\n",
                  flushMode == FULL ? "FULL" : "PART", PRESETS[presetIdx].name,
                  (unsigned)flushEmaUs, fps, speedLvl);
  }
}
