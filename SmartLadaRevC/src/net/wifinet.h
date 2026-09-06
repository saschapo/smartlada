#pragma once
#include <Arduino.h>
#include <IPAddress.h>

// Wi-Fi layer. Which protocol owns the radio is radio::mode()'s decision, not ours -- see
// radio.h for why they cannot share it.
//
// The lamp is access-point first: in a car there is no network to join, so the device hosts
// its own ("fara_NNNN" by default, password "smartlada"), both editable and persisted. Station
// credentials are still honoured if something stored them, which is what makes a bench setup
// on a home network possible, but nothing in the UI sets them any more.
//
// All of this lives in its own NVS keys, NOT in the config::Settings blob: that struct's size
// is part of its validity check, so growing it would wipe every user setting on the next boot.
namespace wifinet {

enum State : uint8_t { OFF, CONNECTING, ONLINE, AP };

void begin();                 // bring the radio up (only called when radio::mode() == WIFI)
void update(uint32_t now);

bool  enabled();              // radio::mode() == WIFI
State state();

const char* staSsid();        // saved station SSID ("" if none)
const char* apSsid();         // this device's access point
const char* apPass();
uint8_t     apClients();      // stations currently associated (drives the QR swap)
IPAddress   ip();
int8_t      rssi();           // 0 unless ONLINE

void setAp(const char* ssid, const char* pass);     // persist + restart the AP
void setCreds(const char* ssid, const char* pass);  // station credentials, persist + retry
void resetWifi();                                   // AP identity back to defaults, forget station

}  // namespace wifinet
