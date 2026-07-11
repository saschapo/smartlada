#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace config {

const ChannelDef CHANNEL_DEFS[channels::NUM_CHANNELS] = {
    {"stop", "Stop", 21, 4, 300},
    {"reverse", "Reverse", 21, 5, 300},
    // GPIO15: not 12/14 (I2C OLED on the HW-364A board); the board's pulldown holds
    // the channel off at boot. Nothing external must pull GPIO15 high at startup!
    {"turn", "Turn", 21, 15, 300},
    {"marker", "Marker", 5, 13, 200},
};

static constexpr float GAMMA_MIN = 1.8f, GAMMA_MAX = 2.6f, GAMMA_DEFAULT = 2.2f;
static constexpr uint16_t FREQ_MIN = 100, FREQ_MAX = 1000, FREQ_DEFAULT = 150;
static constexpr uint16_t SOFT_START_MAX = 1000;
static constexpr uint8_t CAP_DEFAULT = 60;

void setDefaults(Config& cfg) {
  cfg.pwm_freq_hz = FREQ_DEFAULT;
  cfg.max_duty_cap_pct = CAP_DEFAULT;
  for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++) {
    cfg.ch[i].gpio = CHANNEL_DEFS[i].default_gpio;
    cfg.ch[i].calib = {GAMMA_DEFAULT, 0, channels::DUTY_MAX,
                       CHANNEL_DEFS[i].default_soft_start_ms};
  }
}

static float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static uint16_t clampu(uint32_t v, uint16_t lo, uint16_t hi) {
  return v < lo ? lo : (v > hi ? hi : (uint16_t)v);
}

void clamp(Config& cfg) {
  cfg.pwm_freq_hz = clampu(cfg.pwm_freq_hz, FREQ_MIN, FREQ_MAX);
  if (cfg.max_duty_cap_pct > 100) cfg.max_duty_cap_pct = 100;
  for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++) {
    channels::Calib& c = cfg.ch[i].calib;
    c.gamma = clampf(c.gamma, GAMMA_MIN, GAMMA_MAX);
    c.min_duty = clampu(c.min_duty, 0, channels::DUTY_MAX);
    c.max_duty = clampu(c.max_duty, 0, channels::DUTY_MAX);
    if (c.min_duty > c.max_duty) c.min_duty = c.max_duty;
    c.soft_start_ms = clampu(c.soft_start_ms, 0, SOFT_START_MAX);
  }
}

size_t toJson(const Config& cfg, char* buf, size_t buflen) {
  size_t off = 0;
  int n = snprintf(buf, buflen,
                   "{\n  \"pwm_freq_hz\": %u,\n  \"max_duty_cap_pct\": %u,\n"
                   "  \"channels\": {\n",
                   cfg.pwm_freq_hz, cfg.max_duty_cap_pct);
  if (n < 0 || (size_t)n >= buflen) return 0;
  off = (size_t)n;
  for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++) {
    const channels::Calib& c = cfg.ch[i].calib;
    n = snprintf(buf + off, buflen - off,
                 "    \"%s\": { \"gpio\": %u, \"gamma\": %.2f, \"min_duty\": %u,"
                 " \"max_duty\": %u, \"soft_start_ms\": %u }%s\n",
                 CHANNEL_DEFS[i].key, cfg.ch[i].gpio, (double)c.gamma, c.min_duty,
                 c.max_duty, c.soft_start_ms,
                 (i + 1 < channels::NUM_CHANNELS) ? "," : "");
    if (n < 0 || off + (size_t)n >= buflen) return 0;
    off += (size_t)n;
  }
  n = snprintf(buf + off, buflen - off, "  }\n}\n");
  if (n < 0 || off + (size_t)n >= buflen) return 0;
  return off + (size_t)n;
}

// Finds "key": <number> in the range [p, end). Minimal parser for our fixed schema
// (all values are numbers, no string escaping).
static bool findNum(const char* p, const char* end, const char* key, double& out) {
  char pat[24];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  size_t patlen = strlen(pat);
  const char* k = p;
  while ((k = strstr(k, pat)) != nullptr && k < end) {
    const char* c = k + patlen;
    while (c < end && (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n')) c++;
    if (c < end && *c == ':') {
      char* ep = nullptr;
      double v = strtod(c + 1, &ep);
      if (ep != c + 1) {
        out = v;
        return true;
      }
      return false;
    }
    k += patlen;
  }
  return false;
}

// Finds a channel section's { ... } object by its key. Channel objects contain no
// nested braces (values are only numbers), so the first '}' is the end.
static bool findSection(const char* json, const char* key, const char** beg,
                        const char** end) {
  char pat[24];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char* k = strstr(json, pat);
  if (!k) return false;
  const char* o = strchr(k, '{');
  if (!o) return false;
  const char* c = strchr(o, '}');
  if (!c) return false;
  *beg = o;
  *end = c;
  return true;
}

static double clampd(double v, double lo, double hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

bool fromJson(const char* json, Config& cfg) {
  if (!json || !*json) return false;
  const char* docEnd = json + strlen(json);
  int applied = 0;
  double v;
  if (findNum(json, docEnd, "pwm_freq_hz", v)) {
    cfg.pwm_freq_hz = (uint16_t)clampd(v, FREQ_MIN, FREQ_MAX);
    applied++;
  }
  if (findNum(json, docEnd, "max_duty_cap_pct", v)) {
    cfg.max_duty_cap_pct = (uint8_t)clampd(v, 0, 100);
    applied++;
  }
  for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++) {
    const char *b, *e;
    if (!findSection(json, CHANNEL_DEFS[i].key, &b, &e)) continue;
    channels::Calib& c = cfg.ch[i].calib;
    if (findNum(b, e, "gamma", v)) {
      c.gamma = (float)clampd(v, GAMMA_MIN, GAMMA_MAX);
      applied++;
    }
    if (findNum(b, e, "min_duty", v)) {
      c.min_duty = (uint16_t)clampd(v, 0, channels::DUTY_MAX);
      applied++;
    }
    if (findNum(b, e, "max_duty", v)) {
      c.max_duty = (uint16_t)clampd(v, 0, channels::DUTY_MAX);
      applied++;
    }
    if (findNum(b, e, "soft_start_ms", v)) {
      c.soft_start_ms = (uint16_t)clampd(v, 0, SOFT_START_MAX);
      applied++;
    }
    // "gpio" from JSON is ignored: pins are fixed by firmware (see README)
  }
  clamp(cfg);
  return applied > 0;
}

}  // namespace config
