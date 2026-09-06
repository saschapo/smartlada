#pragma once
#include <Arduino.h>

// The C6 has ONE 2.4 GHz radio. The libraries are built with software coexistence, so Wi-Fi
// and Zigbee can nominally run together -- but in practice a phone could not join the
// provisioning AP while the Zigbee stack was up. So the radio is a choice, not a pair:
// exactly one protocol owns it, the choice is persisted, and switching reboots (the Arduino
// Zigbee stack has no clean shutdown, and Wi-Fi must come up before the band is claimed).
namespace radio {

enum Mode : uint8_t { ZIGBEE = 0, WIFI = 1 };

Mode mode();               // stored choice; what this boot did and what the next boot will do
void setMode(Mode m);      // persist + reboot into it (no-op if unchanged)
const char* name(Mode m);

}  // namespace radio
