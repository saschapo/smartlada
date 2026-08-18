#include "buttons.h"

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
}

bool     pressed(uint8_t i) { return b[i].edge; }
bool     repeat(uint8_t i)  { return b[i].rep; }
uint32_t heldMs(uint8_t i)  { return (b[i].stable == LOW) ? (millis() - b[i].pressStart) : 0; }

}  // namespace buttons
