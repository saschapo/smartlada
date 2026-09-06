#include "channels.h"
#include <math.h>

namespace channels {

// Channel index = lamp FUNCTION: ch0=turn, ch1=marker, ch2=reverse, ch3=stop.
// Function -> GPIO map (as wired): turn=IO0, marker=IO1, reverse=IO2, stop=IO3.
// Per the netlist those GPIOs land on Fastons OUT1/OUT0/OUT2/OUT3 (PCB crosses IO0/IO1).
static const uint8_t PINS[N] = {0, 1, 2, 3};   // ch: turn,marker,reverse,stop -> IO0..IO3
// LEDC on ESP32-C6 clocks from the 40 MHz source, so max freq = 40e6 / 2^RES_BITS.
// 10-bit -> ceiling ~39 kHz, which covers the config range (up to 30 kHz) incl. the
// 20 kHz default. (11-bit capped at ~19.5 kHz, below the 20 kHz default -> channels dead.)
static constexpr uint8_t  RES_BITS = 10;       // 1023 levels; supports up to ~39 kHz
static constexpr uint16_t DUTY_MAX = (1 << RES_BITS) - 1;

static uint16_t s_lut[256];                    // input 0..255 -> duty 0..DUTY_MAX
static uint16_t s_softMs = 250;                // smoothing time constant tau (ms)
static float    s_actual[N] = {0, 0, 0, 0};    // slewing output (0..255 domain)
static uint32_t s_lastMs = 0;

// Rebuild the gamma / min-max lookup. Input 0 stays off; 1..255 map into the
// [min%, max%] duty window through the gamma curve.
void setCalib(uint8_t gammaX10, uint8_t minPct, uint8_t maxPct) {
  float gamma = gammaX10 / 10.0f;
  if (gamma < 1.0f) gamma = 1.0f;
  float loDuty = (float)DUTY_MAX * minPct / 100.0f;
  float hiDuty = (float)DUTY_MAX * maxPct / 100.0f;
  if (hiDuty < loDuty) hiDuty = loDuty;
  s_lut[0] = 0;
  for (int i = 1; i < 256; i++) {
    float g = powf((float)i / 255.0f, gamma);
    int32_t d = (int32_t)(loDuty + g * (hiDuty - loDuty) + 0.5f);
    if (d < 0) d = 0; if (d > DUTY_MAX) d = DUTY_MAX;
    s_lut[i] = (uint16_t)d;
  }
}

void setSoftMs(uint16_t ms) { s_softMs = ms; }

void begin(uint32_t freqHz) {
  setCalib(19, 1, 100);                        // sane default until config applies
  for (uint8_t i = 0; i < N; i++) {
    ledcAttach(PINS[i], freqHz, RES_BITS);
    ledcWrite(PINS[i], 0);
    s_actual[i] = 0;
  }
  s_lastMs = millis();
}

void setFreq(uint32_t freqHz) {
  for (uint8_t i = 0; i < N; i++) {
    ledcDetach(PINS[i]);
    ledcAttach(PINS[i], freqHz, RES_BITS);
  }
}

// Start a smoothing ramp from a clean state: reset the clock so the first write() after a
// long, unrelated setup phase (OLED/NVS/Zigbee init) sees a small dt and actually ramps,
// instead of one big dt collapsing the ramp into a jump (boot brownout risk).
void resetSmoothing(uint32_t now) {
  s_lastMs = now;
  for (uint8_t i = 0; i < N; i++) s_actual[i] = 0;
}

// Protective shutdown: zero the physical PWM and the smoothing state immediately, regardless
// of the user's tau. Keeps the clock current so a later write() (supply restored) eases back
// up from zero rather than jumping on a huge accumulated dt.
void inhibit(uint32_t now) {
  s_lastMs = now;
  for (uint8_t i = 0; i < N; i++) { s_actual[i] = 0; ledcWrite(PINS[i], 0); }
}

void write(uint32_t now, const uint8_t target[N]) {
  uint32_t dt = now - s_lastMs;
  s_lastMs = now;
  // Exponential smoothing toward the target: s_softMs is the time constant tau (ms). This
  // continuously eases toward the latest target, so Alice's stepped level stream (~15/100ms)
  // rounds into a smooth curve instead of a rate-limiter's jump-and-hold staircase. A big
  // change settles in ~3*tau. tau=0 => instant.
  float alpha = (s_softMs == 0) ? 1.0f : (1.0f - expf(-(float)dt / (float)s_softMs));
  for (uint8_t i = 0; i < N; i++) {
    float t = (float)target[i];
    s_actual[i] += (t - s_actual[i]) * alpha;
    if (fabsf(t - s_actual[i]) < 0.5f) s_actual[i] = t;   // snap so it settles, no asymptotic crawl
    ledcWrite(PINS[i], s_lut[(uint8_t)(s_actual[i] + 0.5f)]);
  }
}

}  // namespace channels
