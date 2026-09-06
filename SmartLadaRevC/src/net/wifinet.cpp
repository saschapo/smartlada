#include "wifinet.h"
#include "radio.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "../log/eventlog.h"

namespace wifinet {

static constexpr const char* NVS_NS   = "smartlada";
static constexpr const char* K_SSID   = "wifi_ssid";   // station credentials (optional)
static constexpr const char* K_PASS   = "wifi_pass";
static constexpr const char* K_APSSID = "ap_ssid";     // this device's own access point
static constexpr const char* K_APPASS = "ap_pass";
static constexpr const char* HOSTNAME = "smartlada";
static constexpr const char* AP_PASS_DEFAULT = "smartlada";

// A station that cannot join must not sit there forever: after this we raise our own AP so
// there is always a way in, then keep retrying the station in the background.
static constexpr uint32_t JOIN_TIMEOUT_MS = 15000;
static constexpr uint32_t RETRY_EVERY_MS  = 60000;

static State    s_state = OFF;
static char     s_ssid[33] = {0};
static char     s_pass[65] = {0};
static char     s_apSsid[33] = {0};
static char     s_apPass[65] = {0};
static uint32_t s_since = 0;
static bool     s_mdns = false;

// Default AP name carries the last two MAC bytes as a plain 4-digit decimal number: stable
// across reboots, unique per board, and easy to read off a screen or type by hand.
static void defaultApName(char* out, size_t n) {
  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  uint16_t v = ((uint16_t)mac[4] << 8) | mac[5];
  snprintf(out, n, "fara_%04u", (unsigned)(v % 10000));
}

static void startMdns() {
  if (s_mdns) return;
  if (MDNS.begin(HOSTNAME)) { MDNS.addService("http", "tcp", 80); s_mdns = true; }
}

static void startAp(uint32_t now) {
  // Log what actually happened, not what was asked for: "unable to join" on a phone looks
  // identical whether the AP failed to start or merely landed somewhere awkward.
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(s_apSsid, s_apPass);
  s_state = AP; s_since = now;
  startMdns();
  LOGI("wifi", "AP '%s' pass '%s' -> %s, ch %d, ip %s", s_apSsid, s_apPass,
       ok ? "up" : "FAILED", (int)WiFi.channel(), WiFi.softAPIP().toString().c_str());
}

static void startSta(uint32_t now) {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(s_ssid, s_pass);
  s_state = CONNECTING; s_since = now;
  Serial.printf("[wifi] joining '%s'\n", s_ssid);
}

void begin() {
  char def[33]; defaultApName(def, sizeof(def));
  Preferences p;
  p.begin(NVS_NS, true);
  p.getString(K_SSID, s_ssid, sizeof(s_ssid));
  p.getString(K_PASS, s_pass, sizeof(s_pass));
  if (p.getString(K_APSSID, s_apSsid, sizeof(s_apSsid)) == 0) snprintf(s_apSsid, sizeof(s_apSsid), "%s", def);
  if (p.getString(K_APPASS, s_apPass, sizeof(s_apPass)) == 0) snprintf(s_apPass, sizeof(s_apPass), "%s", AP_PASS_DEFAULT);
  p.end();
  if (!enabled()) { s_state = OFF; return; }
  uint32_t now = millis();
  if (s_ssid[0]) startSta(now); else startAp(now);
}

void update(uint32_t now) {
  if (!enabled()) return;
  switch (s_state) {
    case CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        s_state = ONLINE; s_since = now;
        startMdns();
        LOGI("wifi", "online as %s, rssi %d", WiFi.localIP().toString().c_str(),
             (int)WiFi.RSSI());
      } else if ((int32_t)(now - s_since) > (int32_t)JOIN_TIMEOUT_MS) {
        Serial.println("[wifi] join timed out -> own AP");
        startAp(now);
      }
      break;
    case ONLINE:
      if (WiFi.status() != WL_CONNECTED) { Serial.println("[wifi] link lost"); startSta(now); }
      break;
    case AP:
      if (s_ssid[0] && (int32_t)(now - s_since) > (int32_t)RETRY_EVERY_MS) startSta(now);
      break;
    case OFF: break;
  }
}

bool  enabled() { return radio::mode() == radio::WIFI; }
State state()   { return s_state; }
const char* staSsid() { return s_ssid; }
const char* apSsid()  { return s_apSsid; }
const char* apPass()  { return s_apPass; }
uint8_t apClients()   { return (s_state == AP) ? WiFi.softAPgetStationNum() : 0; }
IPAddress ip()  { return (s_state == AP) ? WiFi.softAPIP() : WiFi.localIP(); }
int8_t rssi()   { return (s_state == ONLINE) ? (int8_t)WiFi.RSSI() : 0; }

void setAp(const char* ssid, const char* pass) {
  if (ssid && ssid[0]) snprintf(s_apSsid, sizeof(s_apSsid), "%s", ssid);
  // WPA2 needs 8 characters; anything shorter would silently open the network.
  if (pass && strlen(pass) >= 8) snprintf(s_apPass, sizeof(s_apPass), "%s", pass);
  Preferences p; p.begin(NVS_NS, false);
  p.putString(K_APSSID, s_apSsid); p.putString(K_APPASS, s_apPass); p.end();
  Serial.printf("[wifi] AP identity stored: '%s'\n", s_apSsid);
  if (enabled()) startAp(millis());
}

void setCreds(const char* ssid, const char* pass) {
  snprintf(s_ssid, sizeof(s_ssid), "%s", ssid ? ssid : "");
  snprintf(s_pass, sizeof(s_pass), "%s", pass ? pass : "");
  Preferences p; p.begin(NVS_NS, false);
  p.putString(K_SSID, s_ssid); p.putString(K_PASS, s_pass); p.end();
  if (enabled()) startSta(millis());
}

void resetWifi() {
  char def[33]; defaultApName(def, sizeof(def));
  snprintf(s_apSsid, sizeof(s_apSsid), "%s", def);
  snprintf(s_apPass, sizeof(s_apPass), "%s", AP_PASS_DEFAULT);
  s_ssid[0] = s_pass[0] = 0;
  Preferences p; p.begin(NVS_NS, false);
  p.remove(K_SSID); p.remove(K_PASS); p.remove(K_APSSID); p.remove(K_APPASS); p.end();
  Serial.printf("[wifi] reset: AP back to '%s' / '%s'\n", s_apSsid, s_apPass);
  if (enabled()) startAp(millis());
}

}  // namespace wifinet
