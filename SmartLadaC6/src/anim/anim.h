#pragma once
#include <stdint.h>

#include "../channels/channels.h"

// anim layer: dynamic-mode engine for testing fast PWM changes.
// These are technical dynamics tests (not "pretty" scenes). When mode != MANUAL,
// tick() drives the channel percents itself; the main loop() then computes duty and
// writes to LEDC as usual (channel soft-start smooths the edges). MANUAL = manual
// control via sliders / master, the engine leaves the channels alone.

namespace anim {

enum Mode : uint8_t {
  MANUAL = 0,  // manual control (engine is silent)
  TURN,        // turn signal: only the turn channel blinks
  CHASE,       // running light: one channel at a time
  SEQ,         // fill: channels light up in turn, then all go off at once
  PULSE,       // breathing: sine brightness on all channels
  ALT,         // even/odd channels alternating
  DRIVE,       // driving simulation: scenario state machine (see docs/TZ_drive_mode.md)
  NUM_MODES
};

// Short names for UI/log (Latin, index = Mode).
extern const char* const MODE_NAMES[NUM_MODES];

void begin(channels::Channel* chans);
void setMode(uint8_t mode, uint32_t now_ms);
uint8_t mode();

// Test-mode timings (ms), from config. Called at startup and on POST /config.
// DRIVE uses its own phase durations (independent of these values).
void setTimings(uint16_t chase_ms, uint16_t seq_ms, uint16_t pulse_ms, uint16_t alt_ms);

// Call from loop() every cycle. In MANUAL — does nothing.
void tick(uint32_t now_ms);

}  // namespace anim
