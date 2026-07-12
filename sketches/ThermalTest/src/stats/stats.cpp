#include "stats.h"

#include <Arduino.h>
#include <math.h>

namespace stats {

static constexpr uint32_t STAT_INTERVAL_MS = 2000;   // matches the live sample rate

static float s_min[thermal::NUM_SENSORS];
static float s_max[thermal::NUM_SENSORS];
static double s_sum[thermal::NUM_SENSORS];
static uint32_t s_n[thermal::NUM_SENSORS];
static uint32_t s_count;   // stat ticks where at least one sensor was present

struct Entry {
  uint32_t t_ms;
  float v[thermal::NUM_SENSORS];
};
static Entry s_hist[HISTORY_LEN];
static uint8_t s_filled;   // valid entries (s_hist[0] is newest)

static uint32_t s_statT, s_histT;

void begin() {
  for (uint8_t i = 0; i < thermal::NUM_SENSORS; i++) {
    s_min[i] = NAN;
    s_max[i] = NAN;
    s_sum[i] = 0;
    s_n[i] = 0;
  }
  s_count = 0;
  s_filled = 0;
  s_statT = 0;
  s_histT = 0;
}

static void sampleStats() {
  bool any = false;
  for (uint8_t i = 0; i < thermal::NUM_SENSORS; i++) {
    if (!thermal::present(i)) continue;
    float t = thermal::celsius(i);
    if (isnan(s_min[i]) || t < s_min[i]) s_min[i] = t;
    if (isnan(s_max[i]) || t > s_max[i]) s_max[i] = t;
    s_sum[i] += t;
    s_n[i]++;
    any = true;
  }
  if (any) s_count++;
}

static void pushHistory(uint32_t now_ms) {
  for (int8_t k = HISTORY_LEN - 1; k > 0; k--) s_hist[k] = s_hist[k - 1];
  s_hist[0].t_ms = now_ms;
  for (uint8_t i = 0; i < thermal::NUM_SENSORS; i++)
    s_hist[0].v[i] = thermal::present(i) ? thermal::celsius(i) : NAN;
  if (s_filled < HISTORY_LEN) s_filled++;
}

void tick(uint32_t now_ms) {
  if (now_ms - s_statT >= STAT_INTERVAL_MS) {
    s_statT = now_ms;
    sampleStats();
  }
  if (now_ms - s_histT >= HISTORY_INTERVAL_MS) {
    s_histT = now_ms;
    pushHistory(now_ms);
  }
}

float minC(uint8_t idx) { return idx < thermal::NUM_SENSORS ? s_min[idx] : NAN; }
float maxC(uint8_t idx) { return idx < thermal::NUM_SENSORS ? s_max[idx] : NAN; }

float avgC(uint8_t idx) {
  if (idx >= thermal::NUM_SENSORS || s_n[idx] == 0) return NAN;
  return (float)(s_sum[idx] / s_n[idx]);
}

uint32_t sampleCount() { return s_count; }
uint8_t historyCount() { return s_filled; }

bool history(uint8_t i, uint32_t& t_ms, float out[thermal::NUM_SENSORS]) {
  if (i >= s_filled) return false;
  t_ms = s_hist[i].t_ms;
  for (uint8_t k = 0; k < thermal::NUM_SENSORS; k++) out[k] = s_hist[i].v[k];
  return true;
}

}  // namespace stats
