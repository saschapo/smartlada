#include "web_server.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "../anim/anim.h"
#include "../version.h"
#include "web_ui.h"

namespace web {

static_assert(channels::NUM_CHANNELS == 4, "handleStatus assumes 4 channels");

static WebServer server(80);
static config::Config* s_cfg = nullptr;
static channels::Channel* s_chans = nullptr;
static ApplyGlobalsFn s_applyGlobals = nullptr;

static char s_json[1024];

static void pushConfigToChannels() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++) {
    s_chans[i].setCalib(s_cfg->calib, now);  // shared calibration for all channels
    s_chans[i].setCapPct(s_cfg->max_duty_cap_pct, now);
  }
  if (s_applyGlobals) s_applyGlobals();
}

static void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

static void handleConfigGet() {
  if (config::toJson(*s_cfg, s_json, sizeof(s_json)) == 0) {
    server.send(500, "application/json", "{\"ok\":false,\"err\":\"buffer\"}");
    return;
  }
  server.send(200, "application/json", s_json);
}

static void handleConfigPost() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"empty body\"}");
    return;
  }
  config::Config tmp = *s_cfg;
  if (!config::fromJson(server.arg("plain").c_str(), tmp)) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"bad json\"}");
    return;
  }
  *s_cfg = tmp;
  pushConfigToChannels();
  // return the applied (clamped) config — saves the client a follow-up GET
  if (config::toJson(*s_cfg, s_json, sizeof(s_json)) == 0)
    server.send(200, "application/json", "{\"ok\":true}");
  else
    server.send(200, "application/json", s_json);
}

static void handleSet() {
  if (!server.hasArg("ch") || !server.hasArg("pct")) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"need ch,pct\"}");
    return;
  }
  int ch = server.arg("ch").toInt();
  int pct = server.arg("pct").toInt();
  if (ch < 0 || ch >= channels::NUM_CHANNELS) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"bad ch\"}");
    return;
  }
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  anim::setMode(anim::MANUAL, millis());  // manual control takes over
  s_chans[ch].setPercent((uint8_t)pct, millis());
  server.send(200, "application/json", "{\"ok\":true}");
}

// Master: set one percent on all channels at once (and switch to MANUAL).
static void handleAll() {
  if (!server.hasArg("pct")) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"need pct\"}");
    return;
  }
  int pct = server.arg("pct").toInt();
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  anim::setMode(anim::MANUAL, millis());
  const uint32_t now = millis();
  for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++)
    s_chans[i].setPercent((uint8_t)pct, now);
  server.send(200, "application/json", "{\"ok\":true}");
}

// Switch the animation mode (0 = MANUAL).
static void handleMode() {
  if (!server.hasArg("id")) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"need id\"}");
    return;
  }
  int id = server.arg("id").toInt();
  if (id < 0 || id >= anim::NUM_MODES) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"bad mode\"}");
    return;
  }
  anim::setMode((uint8_t)id, millis());
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleStatus() {
  char buf[320];
  snprintf(buf, sizeof(buf),
           "{\"fw\":\"%s\",\"ssid\":\"%s\",\"mac\":\"%s\",\"clients\":%u,\"heap\":%u,"
           "\"pwm_freq_hz\":%u,\"max_duty_cap_pct\":%u,\"mode\":%u,"
           "\"pct\":[%u,%u,%u,%u]}",
           FW_VERSION, WiFi.softAPSSID().c_str(), WiFi.softAPmacAddress().c_str(),
           (unsigned)WiFi.softAPgetStationNum(), (unsigned)ESP.getFreeHeap(),
           (unsigned)s_cfg->pwm_freq_hz, s_cfg->max_duty_cap_pct, anim::mode(),
           s_chans[0].percent(), s_chans[1].percent(), s_chans[2].percent(),
           s_chans[3].percent());
  server.send(200, "application/json", buf);
}

void begin(config::Config* cfg, channels::Channel* chans, ApplyGlobalsFn applyGlobals) {
  s_cfg = cfg;
  s_chans = chans;
  s_applyGlobals = applyGlobals;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/config.json", HTTP_GET, handleConfigGet);
  server.on("/config", HTTP_POST, handleConfigPost);
  server.on("/set", HTTP_POST, handleSet);
  server.on("/all", HTTP_POST, handleAll);
  server.on("/mode", HTTP_POST, handleMode);
  server.on("/status", HTTP_GET, handleStatus);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
}

void handle() { server.handleClient(); }

}  // namespace web
