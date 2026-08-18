#pragma once
#include <Arduino.h>

// Animation effects. Each effect renders as a PURE function of a normalised cycle
// phase (0..1): render(phase, ...). The single stateful bit is a phase integrator
// in compute() that advances phase by dt/cycle each tick -- so changing a timing
// changes only the RATE, and the lamp never jumps or flickers on edits.
//
// Adding an effect = write render() + cycle() + a Param[] + one line in EFFECTS[].
namespace fx {

enum ParamType { PT_TIME_MS, PT_PERCENT, PT_COUNT };

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

// Compute the 4 channel outputs for a UI mode. Static -> per-channel * master.
// Animation modes advance the internal phase integrator.
void compute(uint8_t mode, uint32_t now, uint8_t master,
             const uint8_t staticBri[4], uint8_t out[4]);

const char* modeName(uint8_t mode);                // 0 = "Static"
uint8_t     modeCount();                           // 1 (Static) + COUNT

void loadParams();                                 // effect params from NVS
void saveParams();                                 // effect params to NVS
void resetParams(uint8_t mode);                    // restore defaults for one effect (1..COUNT)

}  // namespace fx
