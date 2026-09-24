#include "buttons.h"
#include "../display/display.h"   // UI_TFT, SCREEN_DUMP

#if SCREEN_DUMP
namespace buttons {
// Capture build (tools/screens.py): single-char commands on USB serial press a key for one poll.
// U/D/S/B = UP/DOWN/SEL/BACK, '+' = hello (prints the UI variant and re-sends the frame),
// '-' = stop dumping. Keys are raw: under UI_TFT the menu swaps list navigation, and the script
// maps its steps from the variant in the hello line.
static int8_t serialKey() {
  while (Serial.available()) {
    switch (Serial.read()) {
      case 'U': return UP;
      case 'D': return DOWN;
      case 'S': return SEL;
      case 'B': return BACK;
      case '+': Serial.printf("\nSCRDUMP ui=%s\n", UI_TFT ? "tft" : "oled"); display::setDump(true); break;
      case '-': display::setDump(false); break;
    }
  }
  return -1;
}
}  // namespace buttons
#endif

#if UI_TFT
#include <ESP32Encoder.h>

// Bench variant: EC11 on J4 instead of the 4 keys (A=KEY3/IO21, B=KEY2/IO22, PUSH=KEY1/IO23,
// module-side 10k pull-ups + RC). A detent CW = UP (raises values), CCW = DOWN; menu::update
// swaps its list navigation so CW also steps lists forward. One event per poll, extra
// detents queued (capped, so a fast spin does not overshoot after the knob stops). A turn
// within SPIN_MS of the previous one reads as "held", so the menu's accelMult ramps and its
// deferred NVS saves wait until the knob rests. PUSH: short (on release) = SEL, held = BACK.
namespace buttons {

static constexpr uint8_t  PIN_A = 21, PIN_B = 22, PIN_PUSH = 23;
static constexpr uint32_t DEBOUNCE_MS = 25, SPIN_MS = 250, LONG_MS = 600;
static constexpr int32_t  QUEUE_MAX = 4;

static ESP32Encoder enc;
static int32_t  lastDet, pend;
static uint32_t spinStart, lastStep;
static uint8_t  spinKey = UP;
static bool     raw = HIGH, stable = HIGH, longFired;
static uint32_t rawEdge, pressT;
static bool     ev[N];

void begin() {
  pinMode(PIN_PUSH, INPUT);
  ESP32Encoder::useInternalWeakPullResistors = puType::none;
  enc.attachFullQuad(PIN_B, PIN_A);           // swap A/B to reverse the direction
  enc.setFilter(1023);
}

void poll(uint32_t now) {
  for (uint8_t i = 0; i < N; i++) ev[i] = false;

  int64_t c = enc.getCount();
  int32_t det = (int32_t)((c >= 0 ? c : c - 3) / 4);    // floor: 4 counts per detent
  pend = constrain(pend + det - lastDet, -QUEUE_MAX, QUEUE_MAX);
  lastDet = det;
  if (pend) {
    spinKey = pend > 0 ? UP : DOWN;
    ev[spinKey] = true;
    pend += pend > 0 ? -1 : 1;
    if (now - lastStep > SPIN_MS) spinStart = now;
    lastStep = now;
  }

  bool r = digitalRead(PIN_PUSH);
  if (r != raw) { raw = r; rawEdge = now; }
  else if (now - rawEdge >= DEBOUNCE_MS && r != stable) {
    stable = r;
    if (stable == LOW) { pressT = now; longFired = false; }
    else if (!longFired) ev[SEL] = true;
  }
  if (stable == LOW && !longFired && now - pressT >= LONG_MS) { longFired = true; ev[BACK] = true; }
#if SCREEN_DUMP
  int8_t k = serialKey();
  if (k >= 0) ev[k] = true;
#endif
}

bool pressed(uint8_t i) { return ev[i]; }
bool repeat(uint8_t i)  { return ev[i]; }
uint32_t heldMs(uint8_t i) {
  uint32_t now = millis();
  if (i == spinKey && now - lastStep < SPIN_MS) return now - spinStart + 1;
  if (i == SEL && stable == LOW && !longFired) return now - pressT + 1;
  return 0;
}
bool down(uint8_t i) { return i == SEL && raw == LOW; }

}  // namespace buttons

#else
namespace buttons {

// Rev C routing (from PCB netlist, J4): KEY1=GPIO23, KEY2=GPIO22, KEY3=GPIO21, KEY4=GPIO20.
// Index i == K(i+1); order reversed vs Rev B ({20,21,22,23}).
static const uint8_t PINS[N] = {23, 22, 21, 20};
static constexpr uint32_t DEBOUNCE_MS = 25;
static constexpr uint32_t REPEAT_START_MS = 260;  // first auto-repeat delay
static constexpr uint32_t REPEAT_MIN_MS   = 30;   // fastest repeat (~1 display frame)
static constexpr uint32_t REPEAT_RAMP_MS  = 700;  // time to ramp from start to min
// speed past the frame-rate cap comes from a growing step size (menu accelMult),
// not a faster repeat -- so every jump is still drawn.

struct BState {
  bool     stable, lastRaw;
  uint32_t lastEdge, pressStart, lastRepeat;
  bool     edge, rep;
};
static BState b[N];

void begin() {
  for (uint8_t i = 0; i < N; i++) {
    pinMode(PINS[i], INPUT_PULLUP);
    b[i] = {HIGH, HIGH, 0, 0, 0, false, false};
  }
}

void poll(uint32_t now) {
  for (uint8_t i = 0; i < N; i++) {
    b[i].edge = false;
    b[i].rep  = false;

    bool raw = digitalRead(PINS[i]);
    if (raw != b[i].lastRaw) { b[i].lastRaw = raw; b[i].lastEdge = now; }
    else if ((now - b[i].lastEdge) >= DEBOUNCE_MS && raw != b[i].stable) {
      b[i].stable = raw;
      if (b[i].stable == LOW) {                  // pressed
        b[i].edge = b[i].rep = true;
        b[i].pressStart = b[i].lastRepeat = now;
      }
    }

    if (b[i].stable == LOW && !b[i].edge) {       // held: smooth accelerating repeat
      uint32_t held = now - b[i].pressStart;
      float f = (held < REPEAT_RAMP_MS) ? (float)held / REPEAT_RAMP_MS : 1.0f;
      uint32_t interval = REPEAT_START_MS - (uint32_t)((REPEAT_START_MS - REPEAT_MIN_MS) * f);
      if (now - b[i].lastRepeat >= interval) { b[i].rep = true; b[i].lastRepeat = now; }
    }
  }
#if SCREEN_DUMP
  int8_t k = serialKey();
  if (k >= 0) b[k].edge = b[k].rep = true;
#endif
}

bool     pressed(uint8_t i) { return b[i].edge; }
bool     repeat(uint8_t i)  { return b[i].rep; }
uint32_t heldMs(uint8_t i)  { return (b[i].stable == LOW) ? (millis() - b[i].pressStart) : 0; }
// Raw (last-sampled) line level, before debounce settles. Unlike heldMs(), this reports a key
// that is physically held at boot immediately -- used to require a real release before arming
// an action (e.g. OTA exit) so a key held across a reboot does not fire a phantom press.
bool     down(uint8_t i)    { return b[i].lastRaw == LOW; }

}  // namespace buttons
#endif  // UI_TFT
