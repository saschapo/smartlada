#pragma once
#include <stddef.h>
#include <stdint.h>

#include "../channels/channels.h"

// config layer: settings struct + JSON (de)serialization.
// [PORTABLE to ESP32-C6] Pure C++ (snprintf/strtod), no Arduino/ESP8266/Wi-Fi.

namespace config {

// Channel map (spec, section 2). GPIOs are tentative — verify against the HW-365A layout.
struct ChannelDef {
  const char* key;   // section key in JSON and UI
  const char* name;  // human-readable name (UTF-8)
  uint8_t power_w;
  uint8_t default_gpio;
  uint16_t default_soft_start_ms;
};

extern const ChannelDef CHANNEL_DEFS[channels::NUM_CHANNELS];

struct ChannelConfig {
  uint8_t gpio;
  channels::Calib calib;
};

struct Config {
  uint16_t pwm_freq_hz;      // 100..1000, default 150
  uint8_t max_duty_cap_pct;  // 0..100, default 60 (force-safe)
  ChannelConfig ch[channels::NUM_CHANNELS];
};

void setDefaults(Config& cfg);

// Clamps all fields to valid ranges (including min_duty <= max_duty).
void clamp(Config& cfg);

// Serialize to human-readable JSON. Returns length (0 if the buffer is too small).
size_t toJson(const Config& cfg, char* buf, size_t buflen);

// Applies fields found in the JSON to cfg (partial JSON allowed); values are
// clamped to ranges. The "gpio" field is intentionally ignored: pins are fixed
// by firmware. Returns false if no field was recognized.
bool fromJson(const char* json, Config& cfg);

}  // namespace config
