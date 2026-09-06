#include "effects.h"
#include <Preferences.h>
#include <esp_random.h>
#include <math.h>

namespace fx {

// Channel index = lamp function (see channels.cpp): ch0=turn, ch1=marker, ch2=reverse, ch3=stop.
static constexpr uint8_t CH_TURN = 0, CH_MARK = 1, CH_REV = 2, CH_STOP = 3;

// Current time, stashed by compute() so the stateful DRIVE effect (an FSM, not a pure phase
// function) can advance on real time inside its render().
static uint32_t s_fxNow = 0;

static inline uint8_t bscale(uint8_t bright, float f) {   // f in 0..1
  if (f <= 0) return 0;
  int v = (int)(bright * f + 0.5f);
  return v > 255 ? 255 : (uint8_t)v;
}
static inline uint8_t pctScale(uint8_t bright, uint8_t pct) { return bscale(bright, pct / 100.0f); }

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

// ---- Turn: ONLY the turn-signal channel blinks (iconic 1.5 Hz = 333/333 ms) ----
static Param turnP[] = {
  {"Period", PT_TIME_MS, 200, 4000, 50, 666, 666},   // full on+off; 666 ms = 1.5 Hz, 50/50
};
static uint32_t turnCycle(const Effect* s) { return (uint32_t)s->params[0].value; }
static void turnRender(float ph, const Effect*, uint8_t bright, uint8_t out[4]) {
  for (uint8_t i = 0; i < 4; i++) out[i] = 0;
  out[CH_TURN] = (ph < 0.5f) ? bright : 0;
}

// ---- Chase: a lamp is triggered every Step; each one rises and falls on its own ----
// Modelled as trigger + envelope, NOT as a slice of the cycle: the previous shape made Fade a
// fraction of Step, so it could never outlast one slot (Step 0.2 s with Fade 2 s simply
// clamped, and the lamp went dark in 0.2 s).
//
// Every Step a lamp is triggered and runs its own Fade In -> On -> Fade Out envelope, which is
// free to outlive several later triggers -- that overlap is what makes it read as a snake
// rather than a row of separate blinks. Step and On are deliberately separate knobs: with
// Step < FadeIn+On+FadeOut the tails overlap, with Step larger the lamps run with gaps between
// them. Random picks the next lamp instead of walking in order, turning the same envelope into
// scattered flicker. Stateful -> renders off the wall clock, not the phase.
static Param chaseP[] = {
  {"Step",     PT_TIME_MS, 100, 10000, 50, 300, 300},   // time from one lamp starting to the next
  {"On",       PT_TIME_MS,   0,  5000, 50,   0,   0},   // hold at full between the two fades
  {"Fade In",  PT_TIME_MS,   0,  5000, 50, 200, 200},
  {"Fade Out", PT_TIME_MS,   0,  5000, 50, 600, 600},
  {"Random",   PT_ONOFF,     0,     1,  1,   0,   0},
};
static uint32_t chaseCycle(const Effect* s) { return (uint32_t)s->params[0].value * 4; }

static uint32_t s_chaseNextAt = 0;                   // when the next lamp is triggered
static uint32_t s_chaseTrig[4] = {0, 0, 0, 0};       // last trigger time per lamp
static bool     s_chaseLive[4] = {false, false, false, false};
static int8_t   s_chaseLast = -1;                    // last lamp triggered (Random avoids repeats)
static bool     s_chaseInit = false;                 // reset by compute() on entering CHASE

static void chaseRender(float, const Effect* s, uint8_t bright, uint8_t out[4]) {
  const uint32_t now  = s_fxNow;
  const uint32_t step = (uint32_t)s->params[0].value;
  const uint32_t hold = (uint32_t)s->params[1].value;
  const uint32_t fin  = (uint32_t)s->params[2].value;
  const uint32_t fout = (uint32_t)s->params[3].value;
  const bool     rnd  = s->params[4].value != 0;

  if (!s_chaseInit) {
    s_chaseInit = true; s_chaseNextAt = now; s_chaseLast = -1;
    for (uint8_t i = 0; i < 4; i++) { s_chaseTrig[i] = now; s_chaseLive[i] = false; }
  }
  // Resync rather than catch up if the clock ran away (mode just entered, long stall):
  // replaying every missed trigger would burst all four lamps at once.
  if ((int32_t)(now - s_chaseNextAt) > (int32_t)(4 * step)) s_chaseNextAt = now;

  while ((int32_t)(now - s_chaseNextAt) >= 0) {
    int8_t nxt;
    if (rnd) { do { nxt = (int8_t)(esp_random() & 3); } while (nxt == s_chaseLast); }
    else       nxt = (int8_t)((s_chaseLast + 1) & 3);
    s_chaseLast = nxt;
    s_chaseTrig[nxt] = s_chaseNextAt;     // re-triggering a still-lit lamp restarts its envelope
    s_chaseLive[nxt] = true;
    s_chaseNextAt += step;
  }

  for (uint8_t i = 0; i < 4; i++) {
    out[i] = 0;
    if (!s_chaseLive[i]) continue;
    uint32_t age = now - s_chaseTrig[i];
    float v;
    if (age < fin)                    v = fin ? (float)age / (float)fin : 1.0f;
    else if (age < fin + hold)        v = 1.0f;                       // steady on
    else if (age < fin + hold + fout) v = fout ? 1.0f - (float)(age - fin - hold) / (float)fout : 0.0f;
    else { s_chaseLive[i] = false; continue; }
    out[i] = bscale(bright, v);
  }
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

// ---- Drive: logical random driving timeline as a finite-state machine (ported from
// SmartLadaC6/src/anim). Stateful, so it renders off s_fxNow, not the phase. Rules baked
// into the graph: REVERSE only from STOP; TURN not directly from CRUISE; PARKED from STOP/HAZARD.
static constexpr uint8_t  D_BLINK = 255;
static constexpr uint16_t DRV_BLINK_MS = 333;        // 1.5 Hz iconic
static constexpr uint8_t  DRV_MARK_DIM = 35;         // marker background; with STOP -> 100%
enum DState : uint8_t { D_CRUISE=0, D_BRAKE, D_STOP, D_TURN, D_BRAKETURN, D_REVERSE, D_HAZARD, D_PARKED, D_COUNT };
struct DDef { uint16_t durMin, durMax; uint8_t stop, turn, rev, markBase; };
static const DDef DSTATE[D_COUNT] = {
  {3000, 12000,      0,      0,     0,  DRV_MARK_DIM},   // CRUISE
  {1500,  3500,    100,      0,     0,  DRV_MARK_DIM},   // BRAKE
  {1200,  2500,    100,      0,     0,  DRV_MARK_DIM},   // STOP
  {1500,  3000,      0, D_BLINK,    0,  DRV_MARK_DIM},   // TURN
  {1000,  1500,    100, D_BLINK,    0,  DRV_MARK_DIM},   // BRAKETURN
  {2000,  6000,    100,      0,   100,  DRV_MARK_DIM},   // REVERSE
  {2000,  6000,      0, D_BLINK,    0,  DRV_MARK_DIM},   // HAZARD
  {2000,  4000,      0,      0,     0,  0},              // PARKED
};
struct DEdge { uint8_t to, w; };
static const DEdge E_CRUISE[]={{D_BRAKE,3},{D_BRAKETURN,2}};
static const DEdge E_BRAKE[]={{D_STOP,3},{D_TURN,2},{D_CRUISE,2}};
static const DEdge E_STOP[]={{D_CRUISE,2},{D_TURN,2},{D_REVERSE,1},{D_HAZARD,1},{D_PARKED,1}};
static const DEdge E_TURN[]={{D_CRUISE,3},{D_BRAKE,1}};
static const DEdge E_BRAKETURN[]={{D_CRUISE,2},{D_TURN,1},{D_STOP,1}};
static const DEdge E_REVERSE[]={{D_STOP,2},{D_CRUISE,1}};
static const DEdge E_HAZARD[]={{D_PARKED,2},{D_STOP,1},{D_CRUISE,1}};
static const DEdge E_PARKED[]={{D_CRUISE,1}};
struct DAdj { const DEdge* e; uint8_t n; };
static const DAdj DADJ[D_COUNT] = {
  {E_CRUISE,2},{E_BRAKE,3},{E_STOP,5},{E_TURN,2},
  {E_BRAKETURN,3},{E_REVERSE,2},{E_HAZARD,3},{E_PARKED,1},
};
static uint8_t  s_dstate = D_STOP;
static uint32_t s_dPhaseStart = 0;
static uint16_t s_dPhaseDur = 0;
static bool     s_driveInit = false;                 // reset by compute() on entering DRIVE
static uint16_t dPickDur(uint8_t st) {
  uint16_t lo = DSTATE[st].durMin, hi = DSTATE[st].durMax;
  return lo + (uint16_t)(esp_random() % (uint32_t)(hi - lo + 1));
}
static uint8_t dPickNext(uint8_t st) {
  const DAdj& a = DADJ[st];
  uint16_t total = 0;
  for (uint8_t i = 0; i < a.n; i++) total += a.e[i].w;
  uint32_t r = esp_random() % total;
  for (uint8_t i = 0; i < a.n; i++) { if (r < a.e[i].w) return a.e[i].to; r -= a.e[i].w; }
  return a.e[0].to;
}
static void driveRender(float, const Effect*, uint8_t bright, uint8_t out[4]) {
  uint32_t now = s_fxNow;
  if (!s_driveInit) { s_dstate = D_STOP; s_dPhaseStart = now; s_dPhaseDur = dPickDur(D_STOP); s_driveInit = true; }
  if (now - s_dPhaseStart >= s_dPhaseDur) {
    s_dstate = dPickNext(s_dstate); s_dPhaseStart = now; s_dPhaseDur = dPickDur(s_dstate);
  }
  const DDef& d = DSTATE[s_dstate];
  bool blinkOn = (now / DRV_BLINK_MS) % 2 == 0;
  uint8_t stop = d.stop;
  uint8_t turn = (d.turn == D_BLINK) ? (blinkOn ? 100 : 0) : d.turn;
  uint8_t mark = (stop > 0) ? 100 : d.markBase;      // marker/stop combined
  out[CH_STOP] = pctScale(bright, stop);
  out[CH_TURN] = pctScale(bright, turn);
  out[CH_REV]  = pctScale(bright, d.rev);
  out[CH_MARK] = pctScale(bright, mark);
}
static uint32_t driveCycle(const Effect*) { return 1000; }   // phase unused (FSM on real time)

// Derive nparams from the array itself. Hand-written counts drift: Chase shipped with 5
// params and a stale "2" here, which hid three of them from the menu and silently loaded the
// old saved value into the wrong slot.
#define NPARAMS(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))

Effect EFFECTS[] = {
  {"Breathe", breatheP, NPARAMS(breatheP), breatheCycle, breatheRender},
  {"Turn",    turnP,    NPARAMS(turnP),    turnCycle,    turnRender},
  {"Chase",   chaseP,   NPARAMS(chaseP),   chaseCycle,   chaseRender},
  {"Fade",    fadeP,    NPARAMS(fadeP),    fadeCycle,    fadeRender},
  {"Drive",   nullptr,  0,                 driveCycle,   driveRender},
};
const uint8_t COUNT = sizeof(EFFECTS) / sizeof(EFFECTS[0]);

// ---- phase integrator (the only animation state) ----
static float    s_phase = 0.0f;
static uint32_t s_lastMs = 0;
static uint8_t  s_lastMode = 255;

void compute(uint8_t mode, uint32_t now, uint8_t master,
             uint8_t lampOn, const uint8_t staticBri[4], uint8_t out[4]) {
  uint32_t dt = now - s_lastMs;
  s_lastMs = now;                                    // advance clock even when off (no jump on resume)
  s_fxNow  = now;                                    // for the stateful DRIVE effect
  if (mode != s_lastMode) { s_phase = 0.0f; s_lastMode = mode; s_driveInit = false; s_chaseInit = false; }

  // Effect layer: Fara (EP14) color selected an animation mode -> frame * master (effect
  // brightness) across all 4 channels. Fara off/white -> mode 0 -> static below.
  if (mode != 0 && mode <= COUNT) {
    const Effect* e = &EFFECTS[mode - 1];
    uint32_t cyc = e->cycle(e);
    if (cyc > 0) {
      s_phase += (float)dt / (float)cyc;
      s_phase -= floorf(s_phase);                  // wrap to [0,1)
    }
    e->render(s_phase, e, master, out);            // effect spans all 4 channels * master
    return;
  }

  // Static: each lamp at its own level, gated by lampOn. The static master (group dimmer /
  // Fara-white / local idle) fans out into staticBri, so it is NOT scaled again here.
  for (uint8_t i = 0; i < 4; i++)
    out[i] = (lampOn & (1 << i)) ? staticBri[i] : 0;
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
