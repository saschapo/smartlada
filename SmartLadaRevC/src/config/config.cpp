#include "config.h"
#include <Preferences.h>

namespace config {

static constexpr uint8_t MAGIC = 0x57;   // bump on struct/layout change -> reload defaults
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
  250,                        // smoothing time constant tau (ms): eases Alice's stepped level
  1,                          // min level 1 %
  100,                        // max level 100 %
  20000,                      // PWM 20 kHz
  0x0F,                       // lampOn: all 4 channels enabled
  1,                          // faraOn: master device on (local control works out of the box)
};

// Mirror of what is actually in NVS, so tick() can tell a real change from one already saved.
static Settings s_stored;

void load() {
  Preferences p;
  p.begin("smartlada", true);
  size_t n = p.getBytesLength("cfg");
  if (n == sizeof(Settings)) {
    p.getBytes("cfg", &s, sizeof(Settings));
  }
  p.end();
  if (n != sizeof(Settings) || s.magic != MAGIC) s = DEFAULTS;
  s_stored = s;
}

void save() {
  Preferences p;
  p.begin("smartlada", false);
  p.putBytes("cfg", &s, sizeof(Settings));
  p.end();
  s_stored = s;
}

// Power-on policy is RESTORE: the lamp comes back the way it was left, whoever set it. The menu
// already saves on button release, but a remote change has no release event -- and writing per
// dimming step would burn the flash, since Alice streams a level update every ~100 ms. So watch
// the whole struct and write once it has stopped changing. s is also written from the Zigbee
// task; a torn read here only costs one more settle round, never a bad save.
void tick(uint32_t now) {
  static Settings seen;
  static uint32_t stableSince = 0;
  static bool     primed = false;
  static constexpr uint32_t SETTLE_MS = 4000;

  if (!primed) { primed = true; seen = s; stableSince = now; return; }
  if (memcmp(&s, &seen, sizeof(Settings)) != 0) { seen = s; stableSince = now; return; }
  if (memcmp(&s, &s_stored, sizeof(Settings)) == 0) return;      // already persisted
  if ((int32_t)(now - stableSince) < (int32_t)SETTLE_MS) return;  // still settling
  save();
}

}  // namespace config
