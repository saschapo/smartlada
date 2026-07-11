#include "anim.h"

#include <esp_random.h>
#include <math.h>

namespace anim {

const char* const MODE_NAMES[NUM_MODES] = {"MANUAL", "TURN",  "CHASE", "SEQ",
                                           "PULSE",  "ALT", "DRIVE"};

// Channel indices in CHANNEL_DEFS (config.cpp): stop=0, reverse=1, turn=2, marker=3.
static constexpr uint8_t CH_STOP = 0, CH_REV = 1, CH_TURN = 2, CH_MARK = 3;

// Canonical turn-signal/hazard timing (docs/blinking_values_iconic.md):
// 333 ms ON / 333 ms OFF = 1.5 Hz. Half-period — one constant for all blinking.
static constexpr uint16_t TURN_BLINK_MS = 333;
static constexpr uint8_t MARKER_DIM_PCT = 35;  // marker background; with STOP → 100%

// Test-mode timings from config (ms); defaults = former constants. Updated by
// setTimings() at startup and on POST /config. DRIVE does not use them.
static uint16_t s_chaseMs = 200, s_seqMs = 400, s_pulseMs = 2000, s_altMs = 450;

void setTimings(uint16_t chase_ms, uint16_t seq_ms, uint16_t pulse_ms, uint16_t alt_ms) {
  s_chaseMs = chase_ms ? chase_ms : 1;  // guard against division by zero
  s_seqMs = seq_ms ? seq_ms : 1;
  s_pulseMs = pulse_ms ? pulse_ms : 1;
  s_altMs = alt_ms ? alt_ms : 1;
}

static channels::Channel* s_ch = nullptr;
static uint8_t s_mode = MANUAL;
static uint32_t s_t0 = 0;

// ── DRIVE: logical random timeline as a finite-state machine ─────────────────
// A state = a set of lamps + a random duration from its own range. The next state
// is chosen by weighted random from the allowed ones (table ADJ) — yielding an
// endless non-repeating but logical scenario. The rules are baked into the graph:
//   - REVERSE is reachable ONLY from STOP (brake and stop before reversing);
//   - TURN is not reachable directly from CRUISE (slow down before a turn);
//   - PARKED — only from STOP/HAZARD.
static constexpr uint8_t BLINK = 255;  // turn blinks 100/0 in step with TURN_BLINK_MS

enum DState : uint8_t {
  D_CRUISE = 0, D_BRAKE, D_STOP, D_TURN, D_BRAKETURN, D_REVERSE, D_HAZARD, D_PARKED, D_COUNT
};

struct DDef {
  uint16_t durMin, durMax;
  uint8_t stop, turn, rev, markBase;  // turn==BLINK → blinks; stop/rev — steady
};
static const DDef DSTATE[D_COUNT] = {
    // durMin durMax  stop   turn    rev  markBase
    {3000, 12000,      0,     0,      0,  MARKER_DIM_PCT},  // CRUISE    driving steady
    {1500,  3500,    100,     0,      0,  MARKER_DIM_PCT},  // BRAKE     braking
    {1200,  2500,    100,     0,      0,  MARKER_DIM_PCT},  // STOP      standing
    {1500,  3000,      0, BLINK,      0,  MARKER_DIM_PCT},  // TURN      turning while moving
    {1000,  1500,    100, BLINK,      0,  MARKER_DIM_PCT},  // BRAKETURN braking + turning
    {2000,  6000,    100,     0,    100,  MARKER_DIM_PCT},  // REVERSE   reverse (stop steady)
    {2000,  6000,      0, BLINK,      0,  MARKER_DIM_PCT},  // HAZARD    hazard lights
    {2000,  4000,      0,     0,      0,  0},               // PARKED    parked
};

struct Edge { uint8_t to, w; };
static const Edge E_CRUISE[]    = {{D_BRAKE, 3}, {D_BRAKETURN, 2}};
static const Edge E_BRAKE[]     = {{D_STOP, 3}, {D_TURN, 2}, {D_CRUISE, 2}};
static const Edge E_STOP[]      = {{D_CRUISE, 2}, {D_TURN, 2}, {D_REVERSE, 1}, {D_HAZARD, 1}, {D_PARKED, 1}};
static const Edge E_TURN[]      = {{D_CRUISE, 3}, {D_BRAKE, 1}};
static const Edge E_BRAKETURN[] = {{D_CRUISE, 2}, {D_TURN, 1}, {D_STOP, 1}};
static const Edge E_REVERSE[]   = {{D_STOP, 2}, {D_CRUISE, 1}};
static const Edge E_HAZARD[]    = {{D_PARKED, 2}, {D_STOP, 1}, {D_CRUISE, 1}};
static const Edge E_PARKED[]    = {{D_CRUISE, 1}};

struct Adj { const Edge* e; uint8_t n; };
static const Adj ADJ[D_COUNT] = {
    {E_CRUISE, 2}, {E_BRAKE, 3}, {E_STOP, 5}, {E_TURN, 2},
    {E_BRAKETURN, 3}, {E_REVERSE, 2}, {E_HAZARD, 3}, {E_PARKED, 1},
};

static uint8_t s_dstate = D_STOP;
static uint32_t s_phaseStart = 0;
static uint16_t s_phaseDur = 0;

static uint16_t pickDur(uint8_t st) {
  uint16_t lo = DSTATE[st].durMin, hi = DSTATE[st].durMax;
  return lo + (uint16_t)(esp_random() % (uint32_t)(hi - lo + 1));
}

static uint8_t pickNext(uint8_t st) {
  const Adj& a = ADJ[st];
  uint16_t total = 0;
  for (uint8_t i = 0; i < a.n; i++) total += a.e[i].w;
  uint32_t r = esp_random() % total;
  for (uint8_t i = 0; i < a.n; i++) {
    if (r < a.e[i].w) return a.e[i].to;
    r -= a.e[i].w;
  }
  return a.e[0].to;  // unreachable
}

// ─────────────────────────────────────────────────────────────────────────────

void begin(channels::Channel* chans) {
  s_ch = chans;
  s_mode = MANUAL;
}

uint8_t mode() { return s_mode; }

void setMode(uint8_t m, uint32_t now_ms) {
  if (m >= NUM_MODES) m = MANUAL;
  s_mode = m;
  s_t0 = now_ms;
  if (m == DRIVE) {  // the scenario starts from the "standing" state
    s_dstate = D_STOP;
    s_phaseStart = now_ms;
    s_phaseDur = pickDur(D_STOP);
  }
}

// setPercent is a no-op at the channel level if the target is unchanged, so it is
// safe to call every tick (the ramp is not reset on static phases).
static void setAll(uint8_t p, uint32_t now) {
  for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++) s_ch[i].setPercent(p, now);
}

static void driveTick(uint32_t now_ms) {
  if (now_ms - s_phaseStart >= s_phaseDur) {  // phase elapsed → next per the graph
    s_dstate = pickNext(s_dstate);
    s_phaseStart = now_ms;
    s_phaseDur = pickDur(s_dstate);
  }
  const DDef& d = DSTATE[s_dstate];
  const bool blinkOn = (now_ms / TURN_BLINK_MS) % 2 == 0;
  const uint8_t stop = d.stop;  // stop/reverse — steady; only turn blinks
  const uint8_t turn = (d.turn == BLINK) ? (blinkOn ? 100 : 0) : d.turn;
  const uint8_t rev = d.rev;
  const uint8_t mark = (stop > 0) ? 100 : d.markBase;  // marker/stop combined
  s_ch[CH_STOP].setPercent(stop, now_ms);
  s_ch[CH_TURN].setPercent(turn, now_ms);
  s_ch[CH_REV].setPercent(rev, now_ms);
  s_ch[CH_MARK].setPercent(mark, now_ms);
}

void tick(uint32_t now_ms) {
  if (s_mode == MANUAL || !s_ch) return;
  const uint32_t t = now_ms - s_t0;

  switch (s_mode) {
    case TURN: {  // only the turn channel blinks, 333/333 ms (1.5 Hz, iconic)
      bool on = (t / TURN_BLINK_MS) % 2 == 0;
      for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++)
        s_ch[i].setPercent(i == CH_TURN ? (on ? 100 : 0) : 0, now_ms);
      break;
    }
    case CHASE: {  // one channel at 100%, step s_chaseMs
      uint8_t idx = (t / s_chaseMs) % channels::NUM_CHANNELS;
      for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++)
        s_ch[i].setPercent(i == idx ? 100 : 0, now_ms);
      break;
    }
    case SEQ: {  // 0..4 channels growing, then all go off, step s_seqMs
      uint8_t step = (t / s_seqMs) % (channels::NUM_CHANNELS + 1);
      for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++)
        s_ch[i].setPercent(i < step ? 100 : 0, now_ms);
      break;
    }
    case PULSE: {  // "breathing" 0..100 over s_pulseMs; amplitude >50 → floor clips to 0
      float ph = (float)(t % s_pulseMs) / (float)s_pulseMs;
      float v = 50.0f - 65.0f * cosf(ph * 6.2831853f);
      if (v < 0.0f) v = 0.0f;
      if (v > 100.0f) v = 100.0f;
      setAll((uint8_t)(v + 0.5f), now_ms);
      break;
    }
    case ALT: {  // even vs odd channels, step s_altMs
      bool evenPhase = (t / s_altMs) % 2 == 0;
      for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++)
        s_ch[i].setPercent(((i % 2 == 0) == evenPhase) ? 100 : 0, now_ms);
      break;
    }
    case DRIVE:
      driveTick(now_ms);
      break;
    default:
      break;
  }
}

}  // namespace anim
