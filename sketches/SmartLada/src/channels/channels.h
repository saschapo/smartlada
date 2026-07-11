#pragma once
#include <stdint.h>

// channels layer: pure duty math for 4 channels.
// [PORTABLE to ESP32-C6] No dependencies on Arduino/ESP8266/Wi-Fi.
// Time is passed in from the outside (caller's millis()).

namespace channels {

constexpr uint16_t DUTY_MAX = 1023;  // 10-bit duty scale
constexpr uint8_t NUM_CHANNELS = 4;

struct Calib {
  float gamma;             // 1.8..2.6, default 2.2
  uint16_t min_duty;       // 0..1023
  uint16_t max_duty;       // 0..1023
  uint16_t soft_start_ms;  // 0..1000
};

// Operation order fixed by spec:
// slider% -> gamma -> scale to [min_duty..max_duty] -> global cap -> soft-start ramp
class Channel {
 public:
  void setCalib(const Calib& c, uint32_t now_ms);
  const Calib& calib() const { return calib_; }

  // 0..100; 0% = guaranteed physical 0 even when min_duty > 0
  void setPercent(uint8_t pct, uint32_t now_ms);
  uint8_t percent() const { return pct_; }

  // Global cap, applied on top of the per-channel max_duty
  void setCapPct(uint8_t cap_pct, uint32_t now_ms);

  // Current duty including the soft-start ramp; call periodically from loop().
  uint16_t update(uint32_t now_ms);

  // Last computed duty (side-effect free, for indication).
  uint16_t currentDuty() const { return current_; }

 private:
  uint16_t computeTarget() const;
  void retarget(uint32_t now_ms);

  Calib calib_{2.2f, 0, DUTY_MAX, 300};
  uint8_t pct_ = 0;
  uint8_t cap_pct_ = 100;
  uint16_t target_ = 0;
  uint16_t current_ = 0;
  uint16_t ramp_from_ = 0;
  uint32_t ramp_start_ms_ = 0;
};

}  // namespace channels
