#pragma once
#include <Arduino.h>
#include <IPAddress.h>

// Wi-Fi layer, deliberately independent of Zigbee: the C6 libs are built with
// CONFIG_SW_COEXIST_ENABLE, so both radios may run at once. Wi-Fi is OFF by default and is
// switched on from the menu, which also acts as the fallback if coexistence misbehaves.
//
// Credentials live in their own NVS keys (namespace "smartlada", keys wifi_*), NOT in the
// config::Settings blob -- adding fields there changes its size and would wipe every user
// setting on the next boot.
//
// No credentials, or a station that will not join, ends in provisioning AP mode: the device
// hosts "SmartLada-XXXX", and the OLED shows a QR that joins it. Both modes register mDNS,
// so http://smartlada.local reaches the web UI either way.
namespace wifinet {

enum State : uint8_t {
  OFF,          // radio down (default)
  CONNECTING,   // joining the saved network
  ONLINE,       // joined, IP valid
  AP,           // hosting the provisioning access point
};

void begin();                 // load settings; bring the radio up if it was left enabled
void update(uint32_t now);    // drive reconnects / AP fallback; call every loop

void enable(bool on);         // menu switch; persisted
bool enabled();
State state();

bool        haveCreds();
const char* staSsid();        // saved station SSID ("" if none)
const char* apSsid();         // this device's provisioning AP name
const char* apPass();         // its password (stable, derived from the MAC)
IPAddress   ip();             // STA address when ONLINE, AP address in AP mode
int8_t      rssi();           // 0 unless ONLINE

void setCreds(const char* ssid, const char* pass);  // persist + retry immediately
void forget();                                      // drop creds, fall back to AP

}  // namespace wifinet
