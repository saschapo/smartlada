#include "effects.h"
#include <Preferences.h>
#include <math.h>

namespace fx {

static inline uint8_t bscale(uint8_t bright, float f) {   // f in 0..1
  if (f <= 0) return 0;
  int v = (int)(bright * f + 0.5f);
  return v > 255 ? 255 : (uint8_t)v;
}

// ---- Breathe: all channels swell up and down together ----
static Param breatheP[] = {
  {"Period", PT_TIME_MS, 500, 20000, 100, 3000, 3000},
};
static uint32_t breatheCycle(const Effect* s) { return (uint32_t)s->params[0].value; }
static void breatheRender(float ph, const Effect*, uint8_t bright, uint8_t out[4]) {
  float v = (ph < 0.5f) ? ph * 2.0f : (1.0f - ph) * 2.0f;
  uint8_t o = bscale(bright, v);
  for (uint8_t i = 0; i < 4; i++) out[i] = o;
}

// ---- Blink: all channels on/off ----
static Param blinkP[] = {
  {"On",  PT_TIME_MS, 100, 20000, 100, 500, 500},
  {"Off", PT_TIME_MS, 100, 20000, 100, 500, 500},
};
static uint32_t blinkCycle(const Effect* s) { return (uint32_t)(s->params[0].value + s->params[1].value); }
static void blinkRender(float ph, const Effect* s, uint8_t bright, uint8_t out[4]) {
  float onFrac = (float)s->params[0].value / (s->params[0].value + s->params[1].value);
  uint8_t o = (ph < onFrac) ? bright : 0;
  for (uint8_t i = 0; i < 4; i++) out[i] = o;
}

// ---- Chase: one channel at a time sweeps CH1->CH4, with optional edge fade ----
static Param chaseP[] = {
  {"Step", PT_TIME_MS, 100, 10000, 50, 400, 400},
  {"Fade", PT_TIME_MS,   0,  4000, 50, 150, 150},
};
static uint32_t chaseCycle(const Effect* s) { return (uint32_t)s->params[0].value * 4; }
static void chaseRender(float ph, const Effect* s, uint8_t bright, uint8_t out[4]) {
  float sp = ph * 4.0f;
  int slot = (int)sp; if (slot > 3) slot = 3;
  float local = sp - slot;                                  // 0..1 within the slot
  // fade as a fraction of the slot, clamped so the up/down ramps never overlap
  float fadeFrac = (float)s->params[1].value / s->params[0].value;
  if (fadeFrac > 0.5f) fadeFrac = 0.5f;
  float v = 1.0f;
  if (fadeFrac > 0) {
    if (local < fadeFrac)            v = local / fadeFrac;
    else if (local > 1 - fadeFrac)   v = (1 - local) / fadeFrac;
  }
  for (uint8_t i = 0; i < 4; i++) out[i] = 0;
  out[slot] = bscale(bright, v);
}

// ---- Fade: smooth crossfade around the ring CH1->CH2->CH3->CH4-> ----
static Param fadeP[] = {
  {"Cross", PT_TIME_MS, 300, 20000, 100, 1500, 1500},
};
static uint32_t fadeCycle(const Effect* s) { return (uint32_t)s->params[0].value * 4; }
static void fadeRender(float ph, const Effect*, uint8_t bright, uint8_t out[4]) {
  float sp = ph * 4.0f;
  int seg = (int)sp; if (seg > 3) seg = 3;
  float local = sp - seg;
  uint8_t a = seg, b = (seg + 1) & 3;
  for (uint8_t i = 0; i < 4; i++) out[i] = 0;
  out[a] = bscale(bright, 1.0f - local);
  out[b] = bscale(bright, local);
}

Effect EFFECTS[] = {
  {"Breathe", breatheP, 1, breatheCycle, breatheRender},
  {"Blink",   blinkP,   2, blinkCycle,   blinkRender},
  {"Chase",   chaseP,   2, chaseCycle,   chaseRender},
  {"Fade",    fadeP,    1, fadeCycle,    fadeRender},
};
const uint8_t COUNT = sizeof(EFFECTS) / sizeof(EFFECTS[0]);

// ---- phase integrator (the only animation state) ----
static float    s_phase = 0.0f;
static uint32_t s_lastMs = 0;
static uint8_t  s_lastMode = 255;

void compute(uint8_t mode, uint32_t now, uint8_t master, bool faraOn,
             uint8_t lampOn, const uint8_t staticBri[4], uint8_t out[4]) {
  uint32_t dt = now - s_lastMs;
  s_lastMs = now;                                    // advance clock even when off (no jump on resume)
  if (mode != s_lastMode) { s_phase = 0.0f; s_lastMode = mode; }

  // Effect only when the Fara master device is on AND an animation mode is selected.
  if (faraOn && mode != 0 && mode <= COUNT) {
    const Effect* e = &EFFECTS[mode - 1];
    uint32_t cyc = e->cycle(e);
    if (cyc > 0) {
      s_phase += (float)dt / (float)cyc;
      s_phase -= floorf(s_phase);                  // wrap to [0,1)
    }
    e->render(s_phase, e, master, out);            // effect spans all 4 channels * master
    return;
  }

  // Static (Fara on) or passthrough (Fara off): per-channel staticBri gated by lampOn.
  // Fara off -> no master scaling, lamps run at their own EP levels (independent control).
  uint16_t m = faraOn ? master : 255;
  for (uint8_t i = 0; i < 4; i++)
    out[i] = (lampOn & (1 << i)) ? (uint8_t)((int)staticBri[i] * m / 255) : 0;
}

const char* modeName(uint8_t mode) { return (mode == 0) ? "Static" : EFFECTS[mode - 1].name; }
uint8_t     modeCount()            { return 1 + COUNT; }

// ---- persistence ----
static constexpr uint8_t MAX_FX_PARAMS = 32;   // NVS blob cap; guards the stack buffers below
static uint8_t totalParams() {
  uint8_t n = 0;
  for (uint8_t e = 0; e < COUNT; e++) n += EFFECTS[e].nparams;
  return n;
}
static int32_t clampParam(const Param& p, int32_t v) {
  if (v < p.min) v = p.min;
  if (v > p.max) v = p.max;
  return v;
}

void loadParams() {
  Preferences pr;
  pr.begin("smartlada", true);
  uint8_t n = totalParams();
  size_t bytes = (size_t)n * sizeof(int32_t);
  if (n <= MAX_FX_PARAMS && pr.getBytesLength("fxparams") == bytes) {
    int32_t buf[MAX_FX_PARAMS];
    pr.getBytes("fxparams", buf, bytes);
    uint8_t k = 0;
    for (uint8_t e = 0; e < COUNT; e++)
      for (uint8_t i = 0; i < EFFECTS[e].nparams; i++)
        EFFECTS[e].params[i].value = clampParam(EFFECTS[e].params[i], buf[k++]);
  }
  pr.end();
}

void saveParams() {
  if (totalParams() > MAX_FX_PARAMS) return;   // grew past the blob cap; bump MAX_FX_PARAMS
  Preferences pr;
  pr.begin("smartlada", false);
  int32_t buf[MAX_FX_PARAMS];
  uint8_t k = 0;
  for (uint8_t e = 0; e < COUNT; e++)
    for (uint8_t i = 0; i < EFFECTS[e].nparams; i++)
      buf[k++] = EFFECTS[e].params[i].value;
  pr.putBytes("fxparams", buf, (size_t)k * sizeof(int32_t));
  pr.end();
}

void resetParams(uint8_t mode) {
  if (mode < 1 || mode > COUNT) return;
  Effect& e = EFFECTS[mode - 1];
  for (uint8_t i = 0; i < e.nparams; i++) e.params[i].value = e.params[i].def;
}

}  // namespace fx
