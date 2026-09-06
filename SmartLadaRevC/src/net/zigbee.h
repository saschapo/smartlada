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
void factoryReset();        // leave the network + erase Zigbee NVS, then reboot (== re-pair)
bool colorFixActive();      // Yandex color-report workaround installed (see zigbee.cpp)

// Debug switch, persisted. The Arduino Zigbee stack has no clean shutdown, so this decides
// whether begin() runs on the NEXT boot: setEnabled() stores the flag and reboots. Lets the
// 2.4 GHz band be handed entirely to Wi-Fi while chasing a coexistence problem.
bool enabledPref();          // the stored choice (what the NEXT boot will do)
bool enabledOnThisBoot();    // whether begin() actually ran and the stack is live
void setEnabled(bool on);

// Network status (valid once connected(); 0/0xFFFF-ish before join).
uint16_t panId();
uint8_t  channel();
uint16_t shortAddr();
// Link to the parent (coordinator) from our neighbor table. Returns false if not found.
// lqi 0..255, rssi in dBm, parentAddr usually 0x0000 (coordinator).
bool     parentLink(uint8_t& lqi, int8_t& rssi, uint16_t& parentAddr);

}  // namespace zb
