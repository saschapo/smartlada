// RevBStrobeTest -- minimise the panel strobe (rolling-shutter banding) via
// SSD1315 hardware panel parameters. Static, high-contrast view to film.
//
// The strobe is the passive-matrix row scan at the panel frame rate. Per the
// datasheet:  Ffrm = Fosc / (D * K * MUX)
//   Fosc, D : Set Display Clock (0xD5) -- high nibble = Fosc, low nibble = D-1
//   K       : grows with the pre-charge period (0xD9) = (phase2<<4)|phase1
//   MUX     : 0xA8, fixed at 63 (=64 rows) -- lowering it would crop the screen
// So to raise frame rate (finer/less visible banding): max Fosc (0xD5 hi=F),
// D=1 (0xD5 lo=0) => 0xF0, and a short pre-charge (small 0xD9). Trade-off: a very
// short pre-charge dims the panel / undercharges pixels -- visible on camera.
//
// Rev B pinout: SCL=GPIO18 SDA=GPIO19  OLED 0x3C (SSD1315).
//   K1(^)=GPIO20  K2(v)=GPIO21  K3(#)=GPIO22  K4(*)=GPIO23
// Controls:
//   K1 : next preset (frame-rate / pre-charge / VCOMH combo)
//   K2 : cycle test pattern (SOLID / HSTRIPES / CHECKER)
//   K3 / K4 : contrast (0x81) - / +
//
// Build: arduino-cli compile --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc sketches/RevBStrobeTest
// View is static: written only on a button change, so any strobe is the panel's,
// not ours.

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

static constexpr uint8_t PIN_SCL = 18;
static constexpr uint8_t PIN_SDA = 19;
static constexpr uint8_t OLED_ADDR = 0x3C;
static constexpr uint8_t W = 128, H = 64;

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

// name, 0xD5 (clock), 0xD9 (pre-charge), 0xDB (VCOMH deselect)
struct Preset { const char* name; uint8_t d5; uint8_t d9; uint8_t db; };
static const Preset PRESETS[] = {
  {"REF",     0x80, 0xF1, 0x20},   // Adafruit defaults (reference)
  {"CLK",     0xF0, 0xF1, 0x20},   // max osc only
  {"CLK+P22", 0xF0, 0x22, 0x20},   // max osc + datasheet pre-charge
  {"CLK+P11", 0xF0, 0x11, 0x20},   // max osc + shortest pre-charge = max Ffrm
  {"VCOMH0",  0xF0, 0x22, 0x00},   // + lowest VCOMH deselect
};
static constexpr uint8_t NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);
static uint8_t presetIdx = 0;

enum Pattern { SOLID = 0, HSTRIPES, CHECKER, NUM_PAT };
static uint8_t pattern = SOLID;
static const char* PAT_NAME[] = {"SOLID", "HSTR", "CHK"};

static uint8_t contrast = 0x7F;

static Adafruit_SSD1306 oled(W, H, &Wire, -1);

static void applyPreset() {
  const Preset& p = PRESETS[presetIdx];
  oled.ssd1306_command(0xD5); oled.ssd1306_command(p.d5);   // display clock
  oled.ssd1306_command(0xD9); oled.ssd1306_command(p.d9);   // pre-charge
  oled.ssd1306_command(0xDB); oled.ssd1306_command(p.db);   // VCOMH deselect
  oled.ssd1306_command(0x81); oled.ssd1306_command(contrast);
  Serial.printf("preset %s d5=0x%02X d9=0x%02X db=0x%02X contrast=0x%02X pat=%s\n",
                p.name, p.d5, p.d9, p.db, contrast, PAT_NAME[pattern]);
}

static void draw() {
  const Preset& p = PRESETS[presetIdx];
  oled.clearDisplay();

  // test pattern in y=18..63 (big area to reveal banding on camera)
  const int16_t y0 = 18;
  if (pattern == SOLID) {
    oled.fillRect(0, y0, W, H - y0, SSD1306_WHITE);
  } else if (pattern == HSTRIPES) {
    for (int16_t y = y0; y < H; y += 2) oled.drawFastHLine(0, y, W, SSD1306_WHITE);
  } else { // CHECKER 8x8
    for (int16_t by = y0; by < H; by += 8)
      for (int16_t bx = 0; bx < W; bx += 8)
        if (((bx / 8) + (by / 8)) & 1) oled.fillRect(bx, by, 8, 8, SSD1306_WHITE);
  }

  // info (top, on black)
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.printf("P%u/%u %s d5=%02X", presetIdx + 1, NUM_PRESETS, p.name, p.d5);
  oled.setCursor(0, 9);
  oled.printf("d9=%02X db=%02X c=%u %s", p.d9, p.db, contrast, PAT_NAME[pattern]);

  oled.display();
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  delay(50);
  Serial.println();
  Serial.println(F("RevBStrobeTest: K1 preset, K2 pattern, K3/K4 contrast"));

  for (uint8_t i = 0; i < 4; i++) pinMode(btn[i].pin, INPUT_PULLUP);

  Wire.begin(PIN_SDA, PIN_SCL);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("OLED not found at 0x3C -- check wiring"));
    while (true) { delay(1000); }
  }
  Wire.setClock(400000);
  applyPreset();
  draw();
}

void loop() {
  uint32_t now = millis();
  uint8_t hit = pollButtons(now);
  if (!hit) return;                       // static view: act only on a press

  if (hit & 0x01) presetIdx = (presetIdx + 1) % NUM_PRESETS;
  if (hit & 0x02) pattern   = (pattern + 1) % NUM_PAT;
  if (hit & 0x04) { contrast = (contrast >= 16) ? contrast - 16 : 0; }
  if (hit & 0x08) { contrast = (contrast <= 239) ? contrast + 16 : 255; }

  applyPreset();
  draw();
}
