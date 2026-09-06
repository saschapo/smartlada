#pragma once
#include <Arduino.h>

// Persisted user settings (NVS). This holds the scalar UI state; effect timing params live in
// fx::EFFECTS[] and are persisted separately via fx::loadParams()/saveParams().
namespace config {

struct Settings {
  uint8_t magic;          // bumped when the layout changes -> reload defaults
  uint8_t mode;           // 0 = Static, 1..N = fx::EFFECTS[mode-1]
  uint8_t staticBri[4];   // per-channel level in Static mode (0..255)
  uint8_t master;         // global brightness for EFFECTS only; Static uses per-channel staticBri (no master mult, model v2)
  uint8_t dispBri;        // display contrast (0x81, 0..255)
  uint8_t  dimSec;        // dim display to ~1% after N seconds idle (0 = never)
  uint16_t offSec;        // power display off after N seconds idle (0 = never)
  // lamp setup (output calibration)
  uint8_t  gammaX10;      // gamma * 10, 10..30 (1.0..3.0)
  uint16_t softMs;        // soft-start slew time, 0..3000 ms
  uint8_t  minLvl;        // output floor %, 0..50 (so LEDs light at low input)
  uint8_t  maxLvl;        // output ceiling %, 50..100
  uint16_t pwmFreq;       // PWM frequency Hz, 100..30000
  uint8_t  lampOn;        // per-channel on/off bitmask (bit i = channel i); Zigbee EP10-13 on/off
  uint8_t  faraOn;        // reserved (model v2: EP14 sets mode directly; kept to hold NVS layout)
};

extern Settings s;

void load();              // load from NVS, or defaults if absent/mismatched
void save();              // persist current s

}  // namespace config
