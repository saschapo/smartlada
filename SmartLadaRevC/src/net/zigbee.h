#pragma once
#include <Arduino.h>

// Zigbee end-device layer. Exposes 4 lamp endpoints (EP10..13) plus a "Fara" color
// endpoint (EP14) that carries master brightness (level) and effect selection (hue).
// Inbound callbacks write the shared config::s state (last-writer-wins with the local
// OLED menu); the main loop composits config::s -> channels via fx::compute.
//
// Requires the Zigbee ED build:
//   arduino-cli compile -b esp32:esp32:esp32c6:ZigbeeMode=ed,PartitionScheme=zigbee_8MB,CDCOnBoot=cdc,FlashSize=16M
namespace zb {

bool begin();               // configure endpoints + start the stack; join runs in background
void update(uint32_t now);  // poll link state; flags dirty on connect/disconnect transitions
bool connected();           // true once joined a network
bool consumeDirty();        // returns (and clears) whether network activity changed state
void factoryReset();        // leave the network + erase Zigbee NVS, then reboot

}  // namespace zb
