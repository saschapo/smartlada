#include "web_server.h"

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

#include "web_ui.h"

namespace web {

static_assert(channels::NUM_CHANNELS == 4, "handleStatus assumes 4 channels");

static ESP8266WebServer server(80);
static config::Config* s_cfg = nullptr;
static channels::Channel* s_chans = nullptr;
static ApplyGlobalsFn s_applyGlobals = nullptr;

static char s_json[1024];

static void pushConfigToChannels() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++) {
    s_chans[i].setCalib(s_cfg->ch[i].calib, now);
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
  server.send(200, "application/json", "{\"ok\":true}");
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
  s_chans[ch].setPercent((uint8_t)pct, millis());
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleStatus() {
  char buf[240];
  snprintf(buf, sizeof(buf),
           "{\"mac\":\"%s\",\"heap\":%u,\"pwm_freq_hz\":%u,\"max_duty_cap_pct\":%u,"
           "\"pct\":[%u,%u,%u,%u]}",
           WiFi.softAPmacAddress().c_str(), ESP.getFreeHeap(), s_cfg->pwm_freq_hz,
           s_cfg->max_duty_cap_pct, s_chans[0].percent(), s_chans[1].percent(),
           s_chans[2].percent(), s_chans[3].percent());
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
  server.on("/status", HTTP_GET, handleStatus);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
}

void handle() { server.handleClient(); }

}  // namespace web
