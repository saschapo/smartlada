#include "wifinet.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>

namespace wifinet {

static constexpr const char* NVS_NS   = "smartlada";
static constexpr const char* K_ON     = "wifi_on";
static constexpr const char* K_SSID   = "wifi_ssid";
static constexpr const char* K_PASS   = "wifi_pass";
static constexpr const char* HOSTNAME = "smartlada";

// A station that cannot join must not sit there forever: after this long we raise the
// provisioning AP so the user always has a way in, then retry the station periodically.
static constexpr uint32_t JOIN_TIMEOUT_MS = 15000;
static constexpr uint32_t RETRY_EVERY_MS  = 60000;

static bool     s_on = false;
static State    s_state = OFF;
static char     s_ssid[33] = {0};
static char     s_pass[65] = {0};
static char     s_apSsid[20] = {0};
static char     s_apPass[17] = {0};
static uint32_t s_since = 0;        // when the current phase started
static bool     s_mdns = false;

static void saveFlag() {
  Preferences p; p.begin(NVS_NS, false); p.putBool(K_ON, s_on); p.end();
}

// AP identity is derived from the MAC so it is stable across reboots and unique per board:
// a QR printed once keeps working, and two boards on a bench do not collide.
static void deriveAp() {
  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  snprintf(s_apSsid, sizeof(s_apSsid), "SmartLada-%02X%02X", mac[4], mac[5]);
  snprintf(s_apPass, sizeof(s_apPass), "lada%02X%02X%02X", mac[3], mac[4], mac[5]);
}

static void startMdns() {
  if (s_mdns) return;
  if (MDNS.begin(HOSTNAME)) { MDNS.addService("http", "tcp", 80); s_mdns = true; }
}

static void startAp(uint32_t now) {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(s_apSsid, s_apPass);
  s_state = AP; s_since = now;
  startMdns();
  Serial.printf("[wifi] AP '%s' pass '%s' at %s\n", s_apSsid, s_apPass,
                WiFi.softAPIP().toString().c_str());
}

static void startSta(uint32_t now) {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(s_ssid, s_pass);
  s_state = CONNECTING; s_since = now;
  Serial.printf("[wifi] joining '%s'\n", s_ssid);
}

void begin() {
  Preferences p;
  p.begin(NVS_NS, true);
  s_on = p.getBool(K_ON, false);
  p.getString(K_SSID, s_ssid, sizeof(s_ssid));
  p.getString(K_PASS, s_pass, sizeof(s_pass));
  p.end();
  deriveAp();
  if (!s_on) { s_state = OFF; return; }
  uint32_t now = millis();
  if (s_ssid[0]) startSta(now); else startAp(now);
}

void update(uint32_t now) {
  if (!s_on) return;
  switch (s_state) {
    case CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        s_state = ONLINE; s_since = now;
        startMdns();
        Serial.printf("[wifi] online as %s, rssi %d\n", WiFi.localIP().toString().c_str(),
                      (int)WiFi.RSSI());
      } else if ((int32_t)(now - s_since) > (int32_t)JOIN_TIMEOUT_MS) {
        Serial.println("[wifi] join timed out -> provisioning AP");
        startAp(now);
      }
      break;
    case ONLINE:
      if (WiFi.status() != WL_CONNECTED) {    // dropped: go straight back to joining
        Serial.println("[wifi] link lost -> reconnecting");
        startSta(now);
      }
      break;
    case AP:
      // Keep trying the saved network in the background, so a router that was merely
      // rebooting reclaims the device without anyone touching the menu.
      if (s_ssid[0] && (int32_t)(now - s_since) > (int32_t)RETRY_EVERY_MS) startSta(now);
      break;
    case OFF: break;
  }
}

void enable(bool on) {
  if (on == s_on) return;
  s_on = on; saveFlag();
  if (on) {
    uint32_t now = millis();
    if (s_ssid[0]) startSta(now); else startAp(now);
  } else {
    if (s_mdns) { MDNS.end(); s_mdns = false; }
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    s_state = OFF;
    Serial.println("[wifi] off");
  }
}

bool  enabled()   { return s_on; }
State state()     { return s_state; }
bool  haveCreds() { return s_ssid[0] != 0; }
const char* staSsid() { return s_ssid; }
const char* apSsid()  { return s_apSsid; }
const char* apPass()  { return s_apPass; }
IPAddress ip()    { return (s_state == AP) ? WiFi.softAPIP() : WiFi.localIP(); }
int8_t rssi()     { return (s_state == ONLINE) ? (int8_t)WiFi.RSSI() : 0; }

void setCreds(const char* ssid, const char* pass) {
  snprintf(s_ssid, sizeof(s_ssid), "%s", ssid ? ssid : "");
  snprintf(s_pass, sizeof(s_pass), "%s", pass ? pass : "");
  Preferences p; p.begin(NVS_NS, false);
  p.putString(K_SSID, s_ssid); p.putString(K_PASS, s_pass);
  if (!s_on) { s_on = true; p.putBool(K_ON, true); }   // provisioning implies "use Wi-Fi"
  p.end();
  Serial.printf("[wifi] credentials stored for '%s'\n", s_ssid);
  startSta(millis());
}

void forget() {
  s_ssid[0] = s_pass[0] = 0;
  Preferences p; p.begin(NVS_NS, false);
  p.remove(K_SSID); p.remove(K_PASS); p.end();
  Serial.println("[wifi] credentials cleared");
  if (s_on) startAp(millis());
}

}  // namespace wifinet
