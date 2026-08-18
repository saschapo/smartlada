#include "config.h"
#include <Preferences.h>

namespace config {

static constexpr uint8_t MAGIC = 0x56;
Settings s;

static const Settings DEFAULTS = {
  MAGIC,
  0,                          // Static
  {128, 128, 128, 128},       // per-channel
  200,                        // master brightness
  0x7F,                       // display contrast
  30,                         // dim after 30 s
  300,                        // off after 300 s (5 min)
  19,                         // gamma 1.9
  30,                         // soft start 30 ms
  1,                          // min level 1 %
  100,                        // max level 100 %
  20000,                      // PWM 20 kHz
};

void load() {
  Preferences p;
  p.begin("smartlada", true);
  size_t n = p.getBytesLength("cfg");
  if (n == sizeof(Settings)) {
    p.getBytes("cfg", &s, sizeof(Settings));
  }
  p.end();
  if (n != sizeof(Settings) || s.magic != MAGIC) s = DEFAULTS;
}

void save() {
  Preferences p;
  p.begin("smartlada", false);
  p.putBytes("cfg", &s, sizeof(Settings));
  p.end();
}

}  // namespace config
