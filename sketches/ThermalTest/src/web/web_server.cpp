#include "web_server.h"

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

#include "../log/log.h"
#include "../rtc/rtc.h"
#include "../thermal/thermal.h"
#include "web_ui.h"

namespace web {

static ESP8266WebServer server(80);

static void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

// GET /api/state — live telemetry for the SPA (and any other poller).
static void handleState() {
  char t[thermal::NUM_SENSORS][12];
  for (uint8_t i = 0; i < thermal::NUM_SENSORS; i++) {
    if (thermal::present(i))
      snprintf(t[i], sizeof(t[i]), "%.1f", (double)thermal::celsius(i));
    else
      strcpy(t[i], "null");
  }
  char clk[12];
  rtc::format(clk, sizeof(clk), false);

  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"temp\":[%s,%s],\"time\":\"%s\",\"clock\":%s,"
           "\"clients\":%u,\"heap\":%u,\"log\":%u}",
           t[0], t[1], clk, rtc::isSet() ? "true" : "false",
           WiFi.softAPgetStationNum(), (unsigned)ESP.getFreeHeap(),
           (unsigned)applog::size());
  server.send(200, "application/json", buf);
}

// POST /api/time — browser hands the device its LOCAL wall-clock on page load
// (epoch already shifted by the browser tz offset; see web_ui.h).
static void handleApiTime() {
  if (server.hasArg("epoch")) {
    bool was = rtc::isSet();
    rtc::setLocalEpoch((uint32_t)strtoul(server.arg("epoch").c_str(), nullptr, 10));
    if (!was && rtc::isSet()) {        // first wall-clock this session -> anchor the log
      char ts[24];
      rtc::format(ts, sizeof(ts), true);
      LOGI("SYS", "clock set %s", ts);
    }
  }
  server.send(200, "text/plain", "ok");
}

// Download a log file with a dated name ("260711_thermal.log") once the clock is
// set, else a plain name.
static void handleLogFile(const char* path, const char* suffix) {
  if (!LittleFS.exists(path)) {
    server.send(404, "text/plain", "No log yet");
    return;
  }
  File f = LittleFS.open(path, "r");
  if (!f) {
    server.send(500, "text/plain", "Open failed");
    return;
  }
  char fname[32];
  if (rtc::isSet()) {
    char ts[24];
    rtc::format(ts, sizeof(ts), true);   // "YYYY-MM-DD ..." -> YYMMDD
    snprintf(fname, sizeof(fname), "%c%c%c%c%c%c_thermal%s", ts[2], ts[3], ts[5],
             ts[6], ts[8], ts[9], suffix);
  } else {
    snprintf(fname, sizeof(fname), "thermal%s", suffix);
  }
  server.sendHeader("Content-Disposition",
                    String("attachment; filename=\"") + fname + "\"");
  server.streamFile(f, "text/plain");
  f.close();
}
static void handleLog() { handleLogFile("/log.txt", ".log"); }
static void handleLogBak() { handleLogFile("/log.bak.txt", ".log.bak"); }

static void handleClearLog() {
  applog::clear();
  server.send(200, "text/plain", "ok");
}

void begin() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/time", HTTP_POST, handleApiTime);
  server.on("/log", HTTP_GET, handleLog);
  server.on("/log.1", HTTP_GET, handleLogBak);
  server.on("/clearlog", HTTP_POST, handleClearLog);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
}

void handle() { server.handleClient(); }

}  // namespace web
