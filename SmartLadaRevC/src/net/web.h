#pragma once
#include <Arduino.h>

// Local web UI, served from flash (no filesystem): the page is a string in the image, so a
// BLE OTA carries the UI with it and there is no second upload step to forget.
//
// The browser is just another writer of config::s, exactly like the menu and Zigbee --
// last-writer-wins, and config::tick() persists whatever settles. Endpoints take query
// parameters rather than JSON bodies so the firmware needs no parser.
//
//   GET  /              the page
//   GET  /api/state     everything the page renders
//   POST /api/set       mode= master= ch=&on= ch=&bri= p=&v= gamma= soft= min= max= pwm=
//   POST /api/wifi      ssid= pass=        POST /api/forget      POST /api/reboot
namespace web {

void begin();               // start the server (safe to call when Wi-Fi is down)
void update(uint32_t now);  // service clients; call every loop
bool running();

}  // namespace web
