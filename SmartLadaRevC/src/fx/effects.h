#pragma once
#include <Arduino.h>

// Animation effects. Most render as a PURE function of a normalised cycle phase
// (0..1): render(phase, ...). The single stateful bit is a phase integrator in
// compute() that advances phase by dt/cycle each tick -- so changing a timing
// changes only the RATE, and the lamp never jumps or flickers on edits.
// DRIVE and CHASE are stateful instead: they render off the wall clock (s_fxNow),
// because their envelopes outlive one cycle and cannot be recovered from a phase.
//
// Adding an effect = write render() + cycle() + a Param[] + one line in EFFECTS[].
namespace fx {

enum ParamType { PT_TIME_MS, PT_PERCENT, PT_COUNT, PT_ONOFF };

struct Param {
  const char* name;
  ParamType   type;
  int32_t     min, max, step;
  int32_t     value;
  int32_t     def;      // factory default (for "Reset To Defaults")
};

struct Effect;
typedef void     (*RenderFn)(float phase, const Effect* self, uint8_t bright, uint8_t out[4]);
typedef uint32_t (*CycleFn)(const Effect* self);   // full cycle length in ms

struct Effect {
  const char* name;
  Param*      params;
  uint8_t     nparams;
  CycleFn     cycle;
  RenderFn    render;
};

extern Effect        EFFECTS[];
extern const uint8_t COUNT;                        // animation effects (excludes Static)

// Compute the 4 channel outputs (the compositor). The mode alone picks the layer:
//   mode != 0 (effect) -> effect frame * master, across all 4 channels (master = effect
//                         brightness = Fara EP14 level). Fara color selects the effect.
//   mode == 0 (static) -> per-channel staticBri, gated by lampOn (NO master scaling).
//                         The static "master" is the group dimmer / Fara-white / local idle,
//                         which fans out into staticBri -- so it is not applied twice here.
// EP14 (Fara) on/off/color sets mode; EP10-13 (lamps/group) set staticBri + lampOn.
void compute(uint8_t mode, uint32_t now, uint8_t master,
             uint8_t lampOn, const uint8_t staticBri[4], uint8_t out[4]);

const char* modeName(uint8_t mode);                // 0 = "Static"
uint8_t     modeCount();                           // 1 (Static) + COUNT

void loadParams();                                 // effect params from NVS
void saveParams();                                 // effect params to NVS
void resetParams(uint8_t mode);                    // restore defaults for one effect (1..COUNT)

}  // namespace fx
