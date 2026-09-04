// RevCMqttTls - Phase 2.1: ESP32-C6 lantern node over MQTT with mutual TLS.
//
// Connects to mosquitto (mqtt.saschapo.me:8883) with a CLIENT certificate (mTLS), drives
// the four lamp channels from `cmd`, and publishes the applied state (retained) to `state`.
// This is the device half of the SmartLada cloud path; the Go backend translates Yandex
// actions <-> these MQTT messages.
//
// Contract (smarthome_backend/deploy/mqtt/README.md):
//   sub  smartlada/smartlada-01/cmd    {"command_id":"..","set":{"turn":{"on":true,"brightness":40}}}
//   pub  smartlada/smartlada-01/state  {"seq":N,"online":true,"command_id":"..","channels":{...}}  (retained)
//   LWT  smartlada/smartlada-01/state  {"online":false}  (retained, on unexpected drop)
// Channel keys: turn|brake|marker|reverse.
//
// BENCH: RevA board + MuseLab nanoESP32-C6. Channel GPIOs = Rev C routing {1,0,2,3}.
// Status WS2812 on GPIO8. No Zigbee here -- pure Wi-Fi + TLS.
//
// [SAFETY] Channels forced OUTPUT+LOW before anything else. With 12 V + lamps: common
// ground, fuse in place. brightness is API percent (1..100); duty via gamma + min floor.
//
// Libs: PubSubClient, ArduinoJson (v7). Build (nanoESP32-C6, N16):
//   arduino-cli compile -b "esp32:esp32:esp32c6:CDCOnBoot=cdc,FlashSize=16M" sketches/RevCMqttTls

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "secrets.h"

// ---------------- channel map (Rev C routing) ----------------
static const uint8_t NUM_CH = 4;
static const uint8_t CH_PINS[NUM_CH] = {1, 0, 2, 3};              // OUT0..OUT3
static const char *CH_NAME[NUM_CH] = {"turn", "brake", "marker", "reverse"};

static const uint8_t NEOPIXEL_PIN = 8;

// ---------------- PWM (LEDC) ----------------
static const uint32_t PWM_FREQ = 20000;
static const uint8_t PWM_RES_BITS = 10;
static const uint16_t DUTY_MAX = 1023;
static const uint16_t MIN_DUTY = 20;
static const float GAMMA = 1.9f;

// ---------------- MQTT topics ----------------
static const char *TOPIC_CMD = "smartlada/smartlada-01/cmd";
static const char *TOPIC_STATE = "smartlada/smartlada-01/state";
static const char *LWT_MSG = "{\"online\":false}";

// ---------------- state ----------------
struct ChState { bool on; uint8_t bri; };            // bri = API percent 1..100
static ChState g_ch[NUM_CH] = {{false, 100}, {false, 100}, {false, 100}, {false, 100}};
static uint32_t g_seq = 0;

WiFiClientSecure tls;
PubSubClient mqtt(tls);

// API percent (1..100) -> LEDC duty via gamma + min-duty floor. on=false handled by caller.
static uint16_t levelToDuty(uint8_t pct) {
  if (pct == 0) return 0;
  float norm = (float)pct / 100.0f;
  float curved = powf(norm, GAMMA);
  uint16_t duty = MIN_DUTY + (uint16_t)((DUTY_MAX - MIN_DUTY) * curved + 0.5f);
  return duty > DUTY_MAX ? DUTY_MAX : duty;
}

static void applyChannel(uint8_t i) {
  uint16_t duty = g_ch[i].on ? levelToDuty(g_ch[i].bri) : 0;
  ledcWrite(CH_PINS[i], duty);
  Serial.printf("[%lu] ch%u %s %s bri=%u -> duty=%u\n", (unsigned long)millis(),
                i, CH_NAME[i], g_ch[i].on ? "ON" : "OFF", g_ch[i].bri, duty);
}

static int channelIndex(const char *name) {
  for (uint8_t i = 0; i < NUM_CH; i++)
    if (strcmp(name, CH_NAME[i]) == 0) return i;
  return -1;
}

// Publish the applied state (retained) so the backend cache always has the truth.
static void publishState(const char *commandId) {
  JsonDocument doc;
  doc["seq"] = ++g_seq;
  doc["online"] = true;
  doc["command_id"] = commandId ? commandId : "";
  JsonObject ch = doc["channels"].to<JsonObject>();
  for (uint8_t i = 0; i < NUM_CH; i++) {
    JsonObject c = ch[CH_NAME[i]].to<JsonObject>();
    c["on"] = g_ch[i].on;
    c["brightness"] = g_ch[i].bri;
  }
  char buf[256];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  bool ok = mqtt.publish(TOPIC_STATE, (const uint8_t *)buf, n, true);  // retained
  Serial.printf("[%lu] pub state seq=%lu ok=%d: %.*s\n", (unsigned long)millis(),
                (unsigned long)g_seq, ok, (int)n, buf);
}

// Incoming command: apply the `set` patch, then echo the applied state with command_id.
static void onMessage(char *topic, byte *payload, unsigned int len) {
  Serial.printf("[%lu] cmd on %s (%u bytes)\n", (unsigned long)millis(), topic, len);
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err) { Serial.printf("  bad json: %s\n", err.c_str()); return; }

  const char *cmdId = doc["command_id"] | "";
  JsonObject set = doc["set"].as<JsonObject>();
  for (JsonPair kv : set) {
    int idx = channelIndex(kv.key().c_str());
    if (idx < 0) continue;
    JsonObject c = kv.value().as<JsonObject>();
    if (!c["on"].isNull()) g_ch[idx].on = c["on"].as<bool>();
    if (!c["brightness"].isNull()) {
      int b = c["brightness"].as<int>();
      if (b < 1) b = 1; if (b > 100) b = 100;
      g_ch[idx].bri = (uint8_t)b;
    }
    applyChannel(idx);
  }
  publishState(cmdId);
}

static void channelsForceLow() {
  for (uint8_t i = 0; i < NUM_CH; i++) {
    digitalWrite(CH_PINS[i], LOW);
    pinMode(CH_PINS[i], OUTPUT);
    digitalWrite(CH_PINS[i], LOW);
  }
}

static void wifiConnect() {
  Serial.printf("WiFi: connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    neopixelWrite(NEOPIXEL_PIN, 0, 0, 40);
    delay(250);
    neopixelWrite(NEOPIXEL_PIN, 0, 0, 0);
    delay(250);
    Serial.print(".");
  }
  Serial.printf(" ok, ip=%s\n", WiFi.localIP().toString().c_str());
}

static void mqttConnect() {
  while (!mqtt.connected()) {
    Serial.print("MQTT: connecting (mTLS)...");
    // No user/pass: broker uses the client-cert CN as the identity (use_identity_as_username).
    // LWT marks us offline (retained) if the link drops.
    if (mqtt.connect(MQTT_CLIENT_ID, TOPIC_STATE, 1, true, LWT_MSG)) {
      Serial.println(" connected");
      neopixelWrite(NEOPIXEL_PIN, 0, 60, 0);
      mqtt.subscribe(TOPIC_CMD, 1);
      publishState("");                 // announce current state on (re)connect
    } else {
      int st = mqtt.state();            // negative = TLS/socket, positive = MQTT CONNACK
      Serial.printf(" failed rc=%d, retry in 3s\n", st);
      neopixelWrite(NEOPIXEL_PIN, 60, 0, 0);
      delay(3000);
    }
  }
}

void setup() {
  channelsForceLow();
  Serial.begin(115200);
  delay(50);
  Serial.println("\nRevCMqttTls - ESP32-C6 lantern over MQTT/mTLS");

  for (uint8_t i = 0; i < NUM_CH; i++) {
    ledcAttach(CH_PINS[i], PWM_FREQ, PWM_RES_BITS);
    ledcWrite(CH_PINS[i], 0);
  }
  neopixelWrite(NEOPIXEL_PIN, 0, 0, 0);

  wifiConnect();

  // mTLS verifies the server cert's validity dates -> the clock must be real, not 1970,
  // or the handshake fails with "certificate not yet valid". Sync time over NTP first.
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  Serial.print("NTP: syncing time");
  time_t nowt = time(nullptr);
  while (nowt < 1700000000) { delay(200); Serial.print("."); nowt = time(nullptr); }
  Serial.printf(" ok: %ld\n", (long)nowt);

  tls.setCACert(CA_CERT);              // verify the mosquitto server
  tls.setCertificate(CLIENT_CERT);     // our client cert (mTLS)
  tls.setPrivateKey(CLIENT_KEY);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);             // state JSON > default 256
  mqtt.setKeepAlive(30);
  mqtt.setCallback(onMessage);
  mqttConnect();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) wifiConnect();
  if (!mqtt.connected()) mqttConnect();
  mqtt.loop();
  delay(10);
}
