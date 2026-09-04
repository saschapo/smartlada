#pragma once
#include <Arduino.h>

// 4 PWM lamp channels via LEDC. Rev C routing (PCB netlist): OUT0=GPIO1, OUT1=GPIO0,
// OUT2=GPIO2, OUT3=GPIO3 (low-side AOD4184). NOTE: revC has NO external gate pulldown,
// so begin() forces the pins low first thing in setup() to define the boot off-state.
// Effect/UI values are 0..255; the layer applies a gamma curve, a min/max output window,
// and a soft-start slew before writing the LEDC duty (10-bit, see channels.cpp RES_BITS).
namespace channels {

constexpr uint8_t N = 4;

void begin(uint32_t freqHz);                          // attach LEDC, force all to 0
void setFreq(uint32_t freqHz);                        // re-attach at a new frequency
void setCalib(uint8_t gammaX10, uint8_t minPct, uint8_t maxPct);  // rebuild gamma LUT
void setSoftMs(uint16_t ms);                          // soft-start slew time (0 = instant)
void write(uint32_t now, const uint8_t target[N]);    // slew -> gamma -> output

}  // namespace channels
