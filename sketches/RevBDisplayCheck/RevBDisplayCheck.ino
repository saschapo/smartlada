// RevBDisplayCheck -- final display self-test for SmartLada Rev B: recommended
// init, live brightness (contrast), test patterns, instant inverse toggle.
//
// Recommended UI init (baked in here, reuse in the UI):
//   after oled.begin():  0xD5 -> 0xF0   (max oscillator: finest strobe, best to
//   the eye). Everything else left at Adafruit begin() defaults. The camera
//   strobe is intrinsic to the 1/64 passive-matrix scan and is NOT removable by
//   registers (see datasheet); to the eye it is flicker-free.
//
// Rev B pinout: SCL=GPIO18 SDA=GPIO19  OLED 0x3C (SSD1315).
//   K1(^)=GPIO20  K2(v)=GPIO21  K3(#)=GPIO22  K4(*)=GPIO23
// Controls:
//   K1 : next test pattern (SOLID/HSTR/VSTR/CHK8/DITHER/BORDER)
//   K2 : toggle inverse display (0xA6/0xA7, instant, no re-flush)
//   K3 / K4 : brightness (contrast 0x81)  -  / +
//
// Build: arduino-cli compile --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc sketches/RevBDisplayCheck

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

static constexpr uint8_t PIN_SCL = 18;
static constexpr uint8_t PIN_SDA = 19;
static constexpr uint8_t OLED_ADDR = 0x3C;
static constexpr uint8_t W = 128, H = 64;
static constexpr int16_t AREA_Y = 12;          // patterns live in y=12..63

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

enum Pattern { SOLID = 0, HSTR, VSTR, CHK8, DITHER, BORDER, NUM_PAT };
static const char* PAT_NAME[] = {"SOLID", "HSTR", "VSTR", "CHK8", "DITHER", "BORDER"};
static uint8_t pattern = SOLID;
static uint8_t contrast = 0x7F;
static bool    inverse = false;

static Adafruit_SSD1306 oled(W, H, &Wire, -1);

static void setContrast(uint8_t c) {
  oled.ssd1306_command(0x81); oled.ssd1306_command(c);
}

static void drawPattern() {
  const int16_t x0 = 0, y0 = AREA_Y, w = W, h = H - AREA_Y;
  switch (pattern) {
    case SOLID:
      oled.fillRect(x0, y0, w, h, SSD1306_WHITE);
      break;
    case HSTR:
      for (int16_t y = y0; y < H; y += 2) oled.drawFastHLine(x0, y, w, SSD1306_WHITE);
      break;
    case VSTR:
      for (int16_t x = 0; x < W; x += 2) oled.drawFastVLine(x, y0, h, SSD1306_WHITE);
      break;
    case CHK8:
      for (int16_t by = y0; by < H; by += 8)
        for (int16_t bx = 0; bx < W; bx += 8)
          if (((bx / 8) + (by / 8)) & 1) oled.fillRect(bx, by, 8, 8, SSD1306_WHITE);
      break;
    case DITHER:                              // 1px checker -> ~50% gray
      for (int16_t y = y0; y < H; y++)
        for (int16_t x = 0; x < W; x++)
          if ((x + y) & 1) oled.drawPixel(x, y, SSD1306_WHITE);
      break;
    case BORDER:                              // outline + center cross (alignment)
      oled.drawRect(x0, y0, w, h, SSD1306_WHITE);
      oled.drawFastHLine(x0, y0 + h / 2, w, SSD1306_WHITE);
      oled.drawFastVLine(W / 2, y0, h, SSD1306_WHITE);
      break;
  }
}

static void draw() {
  oled.clearDisplay();
  drawPattern();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.printf("%s  c=%u %s", PAT_NAME[pattern], contrast, inverse ? "INV" : "");
  oled.display();
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  delay(50);
  Serial.println();
  Serial.println(F("RevBDisplayCheck: K1 pattern, K2 inverse, K3/K4 brightness"));

  for (uint8_t i = 0; i < 4; i++) pinMode(btn[i].pin, INPUT_PULLUP);

  Wire.begin(PIN_SDA, PIN_SCL);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("OLED not found at 0x3C -- check wiring"));
    while (true) { delay(1000); }
  }
  Wire.setClock(400000);
  oled.ssd1306_command(0xD5); oled.ssd1306_command(0xF0);   // recommended: max osc
  setContrast(contrast);
  draw();
}

void loop() {
  uint32_t now = millis();
  uint8_t hit = pollButtons(now);
  if (!hit) return;                            // static view: act only on a press

  if (hit & 0x01) pattern = (pattern + 1) % NUM_PAT;
  if (hit & 0x02) { inverse = !inverse; oled.ssd1306_command(inverse ? 0xA7 : 0xA6); }
  if (hit & 0x04) { contrast = (contrast >= 16)  ? contrast - 16 : 0;   setContrast(contrast); }
  if (hit & 0x08) { contrast = (contrast <= 239) ? contrast + 16 : 255; setContrast(contrast); }

  Serial.printf("pattern=%s contrast=0x%02X inverse=%d\n",
                PAT_NAME[pattern], contrast, inverse);
  draw();
}
