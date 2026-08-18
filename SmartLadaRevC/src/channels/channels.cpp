#include "channels.h"
#include <math.h>

namespace channels {

// Rev C routing (from PCB netlist): OUT0=GPIO1, OUT1=GPIO0, OUT2=GPIO2, OUT3=GPIO3.
// Index i == physical Faston OUTi. OUT0/OUT1 are crossed vs Rev B ({0,1,2,3}).
static const uint8_t PINS[N] = {1, 0, 2, 3};   // Rev C OUT0..OUT3
static constexpr uint8_t  RES_BITS = 11;       // 2047 levels; supports up to ~39 kHz
static constexpr uint16_t DUTY_MAX = (1 << RES_BITS) - 1;

static uint16_t s_lut[256];                    // input 0..255 -> duty 0..DUTY_MAX
static uint16_t s_softMs = 30;
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

void write(uint32_t now, const uint8_t target[N]) {
  uint32_t dt = now - s_lastMs;
  s_lastMs = now;
  // max change this tick, in the 0..255 domain (full swing takes s_softMs)
  float maxStep = (s_softMs == 0) ? 1e9f : (255.0f * (float)dt / (float)s_softMs);
  for (uint8_t i = 0; i < N; i++) {
    float t = (float)target[i];
    if (s_actual[i] < t)      s_actual[i] = (t - s_actual[i] <= maxStep) ? t : s_actual[i] + maxStep;
    else if (s_actual[i] > t) s_actual[i] = (s_actual[i] - t <= maxStep) ? t : s_actual[i] - maxStep;
    ledcWrite(PINS[i], s_lut[(uint8_t)(s_actual[i] + 0.5f)]);
  }
}

}  // namespace channels
