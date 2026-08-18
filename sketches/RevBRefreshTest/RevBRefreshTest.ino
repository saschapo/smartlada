// RevBRefreshTest -- "does the OLED stay lit on its own, and only blink when we
// push new data?" bench for SmartLada Rev B.
//
// The SSD1306/SSD1315 controller autonomously scans its GDDRAM to the glass at
// the panel frame rate, independent of the host. So once the buffer is written
// the panel stays lit indefinitely with NO further traffic. This test writes the
// buffer only on an interval (or on demand) and does NOTHING in between -- so any
// visible disturbance can only happen at the moment of the data update.
//
// Rev B pinout: SCL=GPIO18 SDA=GPIO19  OLED 0x3C (SSD1315, SSD1306-compatible)
//   K1(^)=GPIO20  K2(v)=GPIO21  K3(#)=GPIO22  K4(*)=GPIO23
// Controls:
//   K1 : update now (single shot -- good for catching the blink on camera)
//   K2 : change size  SMALL (counter only)  <->  BIG (whole-screen invert)
//   K3 / K4 : update interval  shorter / longer  (MANUAL/0.5/1/2/5/10 s)
//
// Build: arduino-cli compile --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc sketches/RevBRefreshTest

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

// interval presets (0 = manual only)
static const uint16_t INTERVALS[] = {0, 500, 1000, 2000, 5000, 10000};
static const char*     INT_NAME[]  = {"MANUAL", "0.5s", "1s", "2s", "5s", "10s"};
static constexpr uint8_t NUM_INT = sizeof(INTERVALS) / sizeof(INTERVALS[0]);
static uint8_t intIdx = 2;          // 1s

static bool     bigChange = false;  // false=SMALL (counter), true=BIG (full invert)
static uint32_t counter = 0;

static Adafruit_SSD1306 oled(W, H, &Wire, -1);

static void updateDisplay() {
  counter++;
  bool inv = bigChange && (counter & 1);   // BIG: flip background every update
  uint16_t fg = inv ? SSD1306_BLACK : SSD1306_WHITE;
  uint16_t bg = inv ? SSD1306_WHITE : SSD1306_BLACK;

  oled.clearDisplay();
  if (inv) oled.fillRect(0, 0, W, H, SSD1306_WHITE);

  oled.setTextColor(fg);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.printf("REFRESH  int=%s", INT_NAME[intIdx]);
  oled.setCursor(0, 10);
  oled.printf("size=%s", bigChange ? "BIG" : "SMALL");

  // big counter, centred
  char s[12];
  snprintf(s, sizeof(s), "%lu", (unsigned long)counter);
  uint8_t digits = strlen(s);
  int16_t x = (W - digits * 18) / 2; if (x < 0) x = 0;
  oled.setTextSize(3);
  oled.setTextColor(fg);
  oled.setCursor(x, 34);
  oled.print(s);

  uint32_t t0 = micros();
  oled.display();
  uint32_t d = micros() - t0;
  Serial.printf("update n=%lu interval=%s size=%s flush=%uus\n",
                (unsigned long)counter, INT_NAME[intIdx],
                bigChange ? "BIG" : "SMALL", (unsigned)d);
  (void)bg;
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  delay(50);
  Serial.println();
  Serial.println(F("RevBRefreshTest: K1 update, K2 size, K3/K4 interval"));

  for (uint8_t i = 0; i < 4; i++) pinMode(btn[i].pin, INPUT_PULLUP);

  Wire.begin(PIN_SDA, PIN_SCL);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("OLED not found at 0x3C -- check wiring"));
    while (true) { delay(1000); }
  }
  updateDisplay();   // one initial paint; then idle until interval/manual
}

void loop() {
  uint32_t now = millis();

  uint8_t hit = pollButtons(now);
  if (hit & 0x01) updateDisplay();                                  // K1: update now
  if (hit & 0x02) { bigChange = !bigChange; updateDisplay(); }      // K2: size, repaint
  if (hit & 0x04) { if (intIdx > 0)          intIdx--; updateDisplay(); }  // K3: shorter
  if (hit & 0x08) { if (intIdx < NUM_INT - 1) intIdx++; updateDisplay(); } // K4: longer

  // auto update on interval (0 = manual only). No display() traffic in between:
  // the panel keeps itself lit from GDDRAM.
  static uint32_t lastUpdate = 0;
  uint16_t iv = INTERVALS[intIdx];
  if (iv != 0 && (now - lastUpdate) >= iv) {
    lastUpdate = now;
    updateDisplay();
  }
}
