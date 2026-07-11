#include "button.h"

#include <Arduino.h>

#include "../anim/anim.h"
#include "../status/status.h"

namespace button {

static constexpr uint8_t PIN_BOOT = 9;         // BOOT button (active-low)
static constexpr uint16_t DEBOUNCE_MS = 30;    // edge debounce
static constexpr uint16_t LONG_PRESS_MS = 500; // threshold to enter the dimmer
static constexpr uint32_t DIM_FULL_MS = 5000;  // brightness travel 0..100% over 5 s

static channels::Channel* s_ch = nullptr;

// Pin-read debounce (true = released / HIGH).
static bool s_stable = true;
static bool s_lastRead = true;
static uint32_t s_debAt = 0;

// Gesture state.
static bool s_pressed = false;     // debounced: button held
static uint32_t s_pressStart = 0;  // moment of the stable press
static bool s_dimming = false;     // a hold-dimmer is in progress
static int8_t s_dir = -1;          // direction of the NEXT hold (start from 100% → down)
static float s_level = 100.0f;     // master brightness 0..100 (default full: animations visible)
static uint32_t s_lastRamp = 0;    // previous ramp tick

// Treat MANUAL as "all lamps on": base 100% on all channels, then brightness is
// driven by the master multiplier (channels::setMasterPct) — the lamps dim
// together, same as in the animations.
static void manualAllOn(uint32_t now_ms) {
  for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++) s_ch[i].setPercent(100, now_ms);
}

static void onClick(uint32_t now_ms) {
  const uint8_t next = (anim::mode() + 1) % anim::NUM_MODES;  // next, wrapping
  anim::setMode(next, now_ms);
  if (next == anim::MANUAL) manualAllOn(now_ms);  // MANUAL = all lamps at master brightness
}

void begin(channels::Channel* chans) {
  s_ch = chans;
  pinMode(PIN_BOOT, INPUT_PULLUP);
  s_stable = s_lastRead = (digitalRead(PIN_BOOT) == HIGH);
  channels::setMasterPct((uint8_t)(s_level + 0.5f));  // sync the global multiplier
}

void tick(uint32_t now_ms) {
  if (!s_ch) return;

  // ── debounce (active-low: LOW = pressed) ──
  const bool raw = (digitalRead(PIN_BOOT) == HIGH);  // true = released
  if (raw != s_lastRead) {
    s_lastRead = raw;
    s_debAt = now_ms;
  }
  if (now_ms - s_debAt >= DEBOUNCE_MS) s_stable = raw;
  const bool pressedNow = !s_stable;

  // ── press edge ──
  if (pressedNow && !s_pressed) {
    s_pressed = true;
    s_pressStart = now_ms;
  }

  // ── hold -> smooth master-brightness dimmer (in all modes) ──
  if (s_pressed && pressedNow) {
    if (!s_dimming && now_ms - s_pressStart >= LONG_PRESS_MS) {
      s_dimming = true;
      // At a limit, force the direction back into range (else the hold would be
      // "empty"); in the middle, keep the alternation from the previous hold.
      if (s_level >= 100.0f) s_dir = -1;
      else if (s_level <= 0.0f) s_dir = +1;
      if (anim::mode() == anim::MANUAL) manualAllOn(now_ms);  // give it something to dim
      s_lastRamp = now_ms;
    }
    if (s_dimming) {
      const uint32_t dt = now_ms - s_lastRamp;
      if (dt > 0) {
        s_lastRamp = now_ms;
        s_level += (float)s_dir * 100.0f * (float)dt / (float)DIM_FULL_MS;
        if (s_level < 0.0f) s_level = 0.0f;
        if (s_level > 100.0f) s_level = 100.0f;
        channels::setMasterPct((uint8_t)(s_level + 0.5f));
      }
      status::brightnessOverlay((uint8_t)(s_level + 0.5f), now_ms);  // level indication
    }
  }

  // ── release edge ──
  if (!pressedNow && s_pressed) {
    s_pressed = false;
    if (s_dimming) {
      s_dimming = false;
      s_dir = -s_dir;  // next hold goes the other way
    } else {
      onClick(now_ms);  // short press
    }
  }
}

}  // namespace button
