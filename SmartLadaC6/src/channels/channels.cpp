#include "channels.h"

#include <math.h>

namespace channels {

static uint8_t g_master_pct = 100;  // global brightness multiplier for all channels

void setMasterPct(uint8_t pct) { g_master_pct = pct > 100 ? 100 : pct; }
uint8_t masterPct() { return g_master_pct; }

void Channel::setCalib(const Calib& c, uint32_t now_ms) {
  calib_ = c;
  if (calib_.min_duty > DUTY_MAX) calib_.min_duty = DUTY_MAX;
  if (calib_.max_duty > DUTY_MAX) calib_.max_duty = DUTY_MAX;
  if (calib_.min_duty > calib_.max_duty) calib_.min_duty = calib_.max_duty;
  retarget(now_ms);
}

void Channel::setPercent(uint8_t pct, uint32_t now_ms) {
  pct_ = (pct > 100) ? 100 : pct;
  retarget(now_ms);
}

void Channel::setCapPct(uint8_t cap_pct, uint32_t now_ms) {
  cap_pct_ = (cap_pct > 100) ? 100 : cap_pct;
  retarget(now_ms);
}

uint16_t Channel::computeTarget() const {
  if (pct_ == 0) return 0;  // guaranteed physical off
  float g = powf((float)pct_ / 100.0f, calib_.gamma);
  float duty = (float)calib_.min_duty + g * (float)(calib_.max_duty - calib_.min_duty);
  float cap = (float)cap_pct_ * (float)DUTY_MAX / 100.0f;
  if (duty > cap) duty = cap;
  if (duty < 0.0f) duty = 0.0f;
  if (duty > (float)DUTY_MAX) duty = (float)DUTY_MAX;
  return (uint16_t)(duty + 0.5f);
}

void Channel::retarget(uint32_t now_ms) {
  uint16_t t = computeTarget();
  if (t == target_) return;
  target_ = t;
  ramp_from_ = current_;
  ramp_start_ms_ = now_ms;
}

uint16_t Channel::update(uint32_t now_ms) {
  if (current_ != target_) {
    uint16_t ss = calib_.soft_start_ms;
    uint32_t elapsed = now_ms - ramp_start_ms_;
    if (ss == 0 || elapsed >= ss) {
      current_ = target_;
    } else {
      int32_t delta = (int32_t)target_ - (int32_t)ramp_from_;
      current_ = (uint16_t)((int32_t)ramp_from_ + delta * (int32_t)elapsed / (int32_t)ss);
    }
  }
  // Master brightness scales the output in all modes (base current_ stays for the
  // ramp; we return the attenuated duty). 100 = unchanged.
  return (uint16_t)((uint32_t)current_ * g_master_pct / 100);
}

}  // namespace channels
