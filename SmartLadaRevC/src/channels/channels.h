#pragma once
#include <Arduino.h>

// 4 PWM lamp channels via LEDC. Channel index = lamp function: ch0=turn, ch1=marker,
// ch2=reverse, ch3=stop, wired to IO0..IO3 (low-side AOD4184). Each gate has a 100k pulldown (Rpd1..4)
// + 100R series (Rg1..4) in HW; begin() also forces the pins low first thing in setup()
// to define the boot off-state (belt-and-suspenders with the pulldowns).
// Callers turn brightness into a duty fraction with remap() (gamma + min/max window) or shape()
// (gamma only); write() slews that duty by the soft-start tau and drives LEDC (10-bit, see
// channels.cpp RES_BITS).
namespace channels {

constexpr uint8_t N = 4;

void begin(uint32_t freqHz);                          // attach LEDC, force all to 0
void setFreq(uint32_t freqHz);                        // re-attach at a new frequency
void setCalib(uint8_t gammaX10, uint8_t minPct, uint8_t maxPct);  // gamma + min/max output window
float remap(float level);                             // brightness 0..1 -> duty 0..1: 0 = off, >0 -> [min, max]
float shape(float v);                                 // v^gamma, no window (effect envelope inside the master)
void setSoftMs(uint16_t ms);                          // smoothing time constant tau (0 = instant; ~3*tau to settle)
void write(uint32_t now, const float duty[N]);        // slew duty (0..1) -> output
void resetSmoothing(uint32_t now);                    // fresh ramp: clean clock + zeroed slew state
void inhibit(uint32_t now);                           // protective off: zero output + slew state immediately

}  // namespace channels
