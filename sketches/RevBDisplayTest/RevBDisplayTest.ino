// RevBDisplayTest -- hardware bring-up test for the SmartLada Rev B carrier.
// Verifies the OLED module (WEO012864V, SSD1315 controller, I2C, 128x64) and
// the 4 tactile buttons using the pinout ALREADY ROUTED on smartlada_revB (J4).
//
// Rev B J4 "OLED AND BUTTONS" routing (from the KiCad netlist, verified):
//   GND -> GND      +3V3 -> VCC
//   SCL -> GPIO18   SDA  -> GPIO19
//   K1  -> GPIO20   K2   -> GPIO21   K3 -> GPIO22   K4 -> GPIO23
//
// Button symbols (module silkscreen / photo):
//   K1 = Up (^)   K2 = Down (v)   K3 = Hash (#)   K4 = Star (*)
//
// Buttons are wired to GND (no external pull), so use INPUT_PULLUP: pressed = LOW.
// The OLED module carries its own I2C pull-ups; Rev B also fits R8/R9 on the bus.
//
// Library: Adafruit SSD1306 (+ Adafruit GFX). SSD1315 speaks the SSD1306 command
// set, so the SSD1306 driver drives this glass unchanged.
//
// Expected behaviour: the OLED shows one line per button with its symbol and a
// live press counter; the pressed button is highlighted (inverted). A heartbeat
// digit in the header proves the refresh loop is alive. Each press is also logged
// to Serial @115200. If the OLED is not found, the test keeps polling buttons and
// reports over Serial so the button wiring can still be checked.

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---- Rev B pinout (do not change without re-routing the PCB) ----------------
static constexpr uint8_t PIN_SCL = 18;
static constexpr uint8_t PIN_SDA = 19;
static constexpr uint8_t OLED_ADDR = 0x3C;

struct Button {
  uint8_t     pin;
  const char* label;   // K1..K4
  char        symbol;  // ^ v # *
  bool        stable;  // debounced level: HIGH = released, LOW = pressed
  bool        lastRaw; // last raw sample
  uint32_t    lastEdge;// ms of last raw change (debounce timer)
  uint32_t    count;   // press events (release->press transitions)
};

static Button buttons[] = {
  {20, "K1", '^', HIGH, HIGH, 0, 0},
  {21, "K2", 'v', HIGH, HIGH, 0, 0},
  {22, "K3", '#', HIGH, HIGH, 0, 0},
  {23, "K4", '*', HIGH, HIGH, 0, 0},
};
static constexpr uint8_t NUM_BUTTONS = sizeof(buttons) / sizeof(buttons[0]);
static constexpr uint32_t DEBOUNCE_MS = 25;

static Adafruit_SSD1306 oled(128, 64, &Wire, -1);
static bool     oledOk    = false;
static uint32_t lastDraw  = 0;
static uint8_t  heartbeat = 0;

static void drawScreen() {
  oled.clearDisplay();

  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.print(F("RevB I/O test  hb:"));
  oled.print(heartbeat);          // 0..9 heartbeat -> loop is alive

  oled.setTextSize(2);            // one row per button, 4 rows below the header
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    const Button& b = buttons[i];
    bool pressed = (b.stable == LOW);
    int16_t y = 16 + i * 12;

    if (pressed) {                // invert the whole row when held
      oled.fillRect(0, y - 1, 128, 12, SSD1306_WHITE);
      oled.setTextColor(SSD1306_BLACK);
    } else {
      oled.setTextColor(SSD1306_WHITE);
    }
    oled.setCursor(0, y);
    // e.g. "K1 ^   3"  -- label, symbol, press count
    oled.printf("%s %c %lu", b.label, b.symbol, (unsigned long)b.count);
  }
  oled.display();
}

static void pollButtons(uint32_t now) {
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    Button& b = buttons[i];
    bool raw = digitalRead(b.pin);
    if (raw != b.lastRaw) {       // raw edge: restart debounce window
      b.lastRaw  = raw;
      b.lastEdge = now;
    } else if ((now - b.lastEdge) >= DEBOUNCE_MS && raw != b.stable) {
      b.stable = raw;             // level held long enough -> accept it
      if (b.stable == LOW) {      // released -> pressed transition
        b.count++;
        Serial.printf("%s (%c) pressed  count=%lu\n",
                      b.label, b.symbol, (unsigned long)b.count);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  // C6 Serial is HWCDC (native USB). By default write() blocks loop() for up to
  // ~2 s (20 x 100 ms) when a host holds the port open but isn't draining bytes.
  // 0 = non-blocking: enqueue what fits, drop the rest -- logging never stalls
  // the control loop.
  Serial.setTxTimeoutMs(0);
#endif
  delay(50);
  Serial.println();
  Serial.println(F("RevBDisplayTest: OLED + 4 buttons on Rev B pinout"));
  Serial.println(F("I2C SDA=GPIO19 SCL=GPIO18  keys K1..K4=GPIO20..23"));

  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
  }

  Wire.begin(PIN_SDA, PIN_SCL);
  oledOk = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (oledOk) {
    Serial.println(F("OLED ready at 0x3C"));
    oled.clearDisplay();
    oled.display();
  } else {
    Serial.println(F("OLED NOT found at 0x3C -- check SDA/SCL/VCC/addr; "
                     "buttons still tested over Serial"));
  }
}

void loop() {
  uint32_t now = millis();
  pollButtons(now);

  if (now - lastDraw >= 100) {    // ~10 fps refresh + heartbeat tick
    lastDraw = now;
    heartbeat = (heartbeat + 1) % 10;
    if (oledOk) drawScreen();
  }
}
