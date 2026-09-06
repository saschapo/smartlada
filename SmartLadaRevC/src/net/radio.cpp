#include "radio.h"
#include <Preferences.h>

namespace radio {

static constexpr const char* NS  = "smartlada";
static constexpr const char* KEY = "radio";

Mode mode() {
  Preferences p; p.begin(NS, true);
  uint8_t m = p.getUChar(KEY, ZIGBEE);      // Zigbee is the product's normal state
  p.end();
  return (m == WIFI) ? WIFI : ZIGBEE;
}

void setMode(Mode m) {
  if (m == mode()) return;
  Preferences p; p.begin(NS, false); p.putUChar(KEY, (uint8_t)m); p.end();
  Serial.printf("[radio] switching to %s; restarting\n", name(m));
  delay(150);
  ESP.restart();
}

const char* name(Mode m) { return (m == WIFI) ? "Wi-Fi" : "Zigbee"; }

}  // namespace radio
