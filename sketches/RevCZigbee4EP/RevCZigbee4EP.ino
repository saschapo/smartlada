// RevCZigbee4EP - multi-endpoint Zigbee bring-up + raw-attribute capture for SmartLada Rev C.
//
// GOAL: prove that ONE ESP32-C6 node exposing FOUR standard DimmableLight endpoints
// (EP10..13) shows up in the "Dom s Alisoy" app as FOUR independently named lamps,
// each with its own on/off + brightness. This is the gating experiment (variant A vs B
// in research/smartlada_alice_zigbee_multi_endpoint_summary.md) that decides whether the
// direct-Zigbee path is viable before touching the product firmware (SmartLadaRevC).
//
// SECOND JOB (reverse engineering): capture the RAW ZCL data Alice sends. We cannot query
// Alice's NLP over Zigbee, but we CAN log every attribute write she pushes to us -- which
// endpoint, which cluster/attribute, ZCL type, decoded + hex value, with a millis()
// timestamp to correlate against the phrase spoken. That reveals exactly how Alice maps
// "yarkost 30 procentov" onto ZCL (which cluster, level scale, transition, on/off order).
//
// HOW THE RAW LOG WORKS: LoggingLight subclasses ZigbeeDimmableLight and overrides the
// public virtual zbAttributeSet(), logging the decoded message before driving the channel.
// Because that override replaces the base handler, on/off + level are re-parsed here (the
// same two attributes the base handles) -- so the lamp still works AND every write is
// logged, including ones the stock light ignores (Groups/Scenes/Identify show up too).
//
// A second, deeper firehose is available with no code change: build with DebugLevel=debug
// (or verbose) and Zigbee.setDebugMode(true) (already called) dumps APSDE-DATA indications
// (per-frame src/dst endpoint, cluster, profile, LQI). See README.md for that capture build.
//
// BENCH (per project owner): RevA carrier board + MuseLab nanoESP32-C6 dev board.
//   - Channel GPIOs use the Rev C routing {1,0,2,3} so the mapping matches the product.
//   - Status WS2812 is the nanoESP32-C6 onboard LED on GPIO8.
//   - BOOT button GPIO9 (active-low): hold 3 s -> Zigbee factory reset + rejoin.
//   - Debug/log over native USB CDC.
// No Wi-Fi / no web on purpose: the C6 shares one 2.4 GHz radio between Wi-Fi and 802.15.4.
//
// [SAFETY] Channels are forced OUTPUT+LOW before anything else. With lamps + 12 V wired:
// common ground board <-> PSU and a fuse in place, exactly as in the main firmware.
//
// Build (nanoESP32-C6 is N16 = 16 MB, no zigbee_16MB scheme exists -> zigbee_8MB + 16M):
//   arduino-cli compile -b "esp32:esp32:esp32c6:ZigbeeMode=ed,PartitionScheme=zigbee_8MB,CDCOnBoot=cdc,FlashSize=16M" sketches/RevCZigbee4EP

#include <Arduino.h>

#ifndef ZIGBEE_MODE_ED
#error "Select Zigbee mode 'Zigbee ED (end device)' (FQBN ...:ZigbeeMode=ed)"
#endif

#include "Zigbee.h"

// Per-endpoint default naming. When 1, each endpoint advertises a distinct, human
// ModelIdentifier (LAMP_NAME below). Some hubs surface ModelIdentifier as the default
// device name, so the four cards arrive distinguishable instead of four identical
// "Lampochka". Whether Alice actually uses it as the card name is EMPIRICAL -> re-pair
// and check. Changing this requires a factory reset (BOOT 3 s) + re-add, which WIPES
// the user-assigned names/group on the hub. When 0, all use one model "RevC-Lamp".
#define DISTINCT_MODEL 1

// ASCII labels (project rule: ASCII only in code/serial). Suggested Cyrillic meaning:
// Turn=povorotnik, Brake=stop-signal, Marker=gabarit, Reverse=zadniy hod.
static const char *LAMP_NAME[4] = {"Turn", "Brake", "Marker", "Reverse"};

// ---------------- channel hardware map (Rev C routing) ------------------
// Index i == logical channel i == physical Faston OUTi. Rev C netlist:
// OUT0=GPIO1, OUT1=GPIO0, OUT2=GPIO2, OUT3=GPIO3 (low-side D4184, active-HIGH gate).
static const uint8_t NUM_CH = 4;
static const uint8_t CH_PINS[NUM_CH] = {1, 0, 2, 3};   // OUT0..OUT3
// Endpoint id per channel. Suggested lamp meaning (assigned by wiring + app naming):
//   EP10 -> turn signal, EP11 -> brake, EP12 -> marker, EP13 -> reverse.
static const uint8_t EP_ID[NUM_CH] = {10, 11, 12, 13};

// ---------------- dev-board pins (MuseLab nanoESP32-C6) ------------------
static const uint8_t NEOPIXEL_PIN = 8;   // onboard WS2812
static const uint8_t BUTTON_PIN = 9;     // BOOT, active-low

// ---------------- PWM (LEDC), same shape as the main firmware -----------
static const uint32_t PWM_FREQ = 20000;  // 20 kHz, flicker-free for these lamps
static const uint8_t PWM_RES_BITS = 10;  // 0..1023
static const uint16_t DUTY_MAX = 1023;
static const uint16_t MIN_DUTY = 20;     // dead-zone floor (bench calibration)
static const float GAMMA = 1.9f;         // perceptual curve chosen on the bench

// Map a Zigbee ZCL level (1..254) to an LEDC duty via gamma + min-duty floor.
static uint16_t levelToDuty(uint8_t level) {
  if (level == 0) return 0;
  float norm = (float)level / 254.0f;
  float curved = powf(norm, GAMMA);
  uint16_t duty = MIN_DUTY + (uint16_t)((DUTY_MAX - MIN_DUTY) * curved + 0.5f);
  return duty > DUTY_MAX ? DUTY_MAX : duty;
}

// Drive one channel and log the resulting effect (paired with the raw-attribute line).
static void applyChannel(uint8_t i, bool state, uint8_t level) {
  uint16_t duty = state ? levelToDuty(level) : 0;
  ledcWrite(CH_PINS[i], duty);
  Serial.printf("[%lu] EP%u ch%u APPLY %s level=%u -> duty=%u\n",
                (unsigned long)millis(), EP_ID[i], i, state ? "ON" : "OFF", level, duty);
}

// Human-readable ZCL cluster name for the raw log.
static const char *clusterName(uint16_t c) {
  switch (c) {
    case 0x0000: return "Basic";
    case 0x0003: return "Identify";
    case 0x0004: return "Groups";
    case 0x0005: return "Scenes";
    case 0x0006: return "OnOff";
    case 0x0008: return "Level";
    case 0x0300: return "Color";
    default:     return "?";
  }
}

// RAW DUMP: one line per incoming attribute write. This is the reverse-engineering core.
static void logAttr(uint8_t ep, const esp_zb_zcl_set_attr_value_message_t *m) {
  uint16_t cl = m->info.cluster;
  uint16_t aid = m->attribute.id;
  uint8_t ty = m->attribute.data.type;
  uint16_t sz = m->attribute.data.size;
  const uint8_t *p = (const uint8_t *)m->attribute.data.value;
  uint32_t v = 0;
  for (uint16_t i = 0; i < sz && i < 4 && p; i++) v |= (uint32_t)p[i] << (8 * i);
  Serial.printf("[%lu] EP%u SET %s(0x%04x) attr=0x%04x type=0x%02x size=%u val=%lu raw=",
                (unsigned long)millis(), ep, clusterName(cl), cl, aid, ty, sz, (unsigned long)v);
  for (uint16_t i = 0; i < sz && p; i++) Serial.printf("%02x ", p[i]);
  Serial.println();
}

// ---------------- four DimmableLight endpoints with raw logging ---------
class LoggingLight : public ZigbeeDimmableLight {
public:
  LoggingLight(uint8_t ep, uint8_t ch) : ZigbeeDimmableLight(ep), _ep(ep), _ch(ch) {}

  // Re-apply the last known state to the channel (used after an identify blink).
  void reapply() { applyChannel(_ch, _state, _level); }

  // Overrides the base handler: log the raw attribute, then re-parse on/off + level so
  // the lamp keeps working. Cannot chain to the (private) base override, so we duplicate
  // the two attributes the base cares about -- intentionally, to keep one code path.
  void zbAttributeSet(const esp_zb_zcl_set_attr_value_message_t *m) override {
    logAttr(_ep, m);
    if (m->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF
        && m->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID
        && m->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
      _state = *(bool *)m->attribute.data.value;
    } else if (m->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL
               && m->attribute.id == ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID
               && m->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U8) {
      _level = *(uint8_t *)m->attribute.data.value;
    }
    applyChannel(_ch, _state, _level);
  }

private:
  uint8_t _ep, _ch;
  bool _state = false;
  uint8_t _level = 255;
};

LoggingLight zbCh0(10, 0), zbCh1(11, 1), zbCh2(12, 2), zbCh3(13, 3);
static LoggingLight *zbCh[NUM_CH] = {&zbCh0, &zbCh1, &zbCh2, &zbCh3};

// Identify: coordinator asks the device to blink. On finish, restore every channel.
static void onIdentify(uint16_t time) {
  static uint8_t on = 1;
  if (time == 0) {
    for (uint8_t i = 0; i < NUM_CH; i++) zbCh[i]->reapply();
    return;
  }
  neopixelWrite(NEOPIXEL_PIN, 60 * on, 60 * on, 60 * on);  // blink white
  on = !on;
}

static void channelsForceLow() {
  for (uint8_t i = 0; i < NUM_CH; i++) {
    digitalWrite(CH_PINS[i], LOW);
    pinMode(CH_PINS[i], OUTPUT);
    digitalWrite(CH_PINS[i], LOW);
  }
}

// Print our own node identity + network coordinates (reverse-engineering context).
static void logNetworkInfo() {
  esp_zb_ieee_addr_t ieee;
  esp_zb_get_long_address(ieee);
  Serial.printf("[%lu] NET short=0x%04x pan=0x%04x channel=%u\n",
                (unsigned long)millis(), esp_zb_get_short_address(),
                esp_zb_get_pan_id(), esp_zb_get_current_channel());
  Serial.print("[NET] ieee (little-endian) = ");
  for (int i = 0; i < 8; i++) Serial.printf("%02x ", ieee[i]);
  Serial.println();
}

void setup() {
  // FORCE-SAFE: lamps off before anything else.
  channelsForceLow();

  Serial.begin(115200);
  delay(50);
  Serial.println("\nRevCZigbee4EP - 4x DimmableLight multi-endpoint + raw ZCL capture");

  for (uint8_t i = 0; i < NUM_CH; i++) {
    if (!ledcAttach(CH_PINS[i], PWM_FREQ, PWM_RES_BITS))
      Serial.printf("LEDC attach FAILED on GPIO%u\n", CH_PINS[i]);
    ledcWrite(CH_PINS[i], 0);
  }

  neopixelWrite(NEOPIXEL_PIN, 0, 0, 0);   // dark
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Enable library debug: with a DebugLevel=debug/verbose build this dumps APSDE-DATA
  // indications (per-frame endpoint/cluster/profile/LQI). Harmless on a normal build.
  Zigbee.setDebugMode(true);

  for (uint8_t i = 0; i < NUM_CH; i++) {
    zbCh[i]->onIdentify(onIdentify);
#if DISTINCT_MODEL
    zbCh[i]->setManufacturerAndModel("SmartLada", LAMP_NAME[i]);
#else
    zbCh[i]->setManufacturerAndModel("SmartLada", "RevC-Lamp");
#endif
    Zigbee.addEndpoint(zbCh[i]);
    Serial.printf("Added endpoint EP%u -> ch%u (GPIO%u)\n", EP_ID[i], i, CH_PINS[i]);
  }

  Serial.println("Starting Zigbee (end device)...");
  if (!Zigbee.begin()) {
    Serial.println("Zigbee failed to start! Rebooting in 1 s.");
    delay(1000);
    ESP.restart();
  }

  Serial.println("Joining network (open pairing on the Midi now)...");
  while (!Zigbee.connected()) {
    Serial.print(".");
    neopixelWrite(NEOPIXEL_PIN, 0, 0, 40);   // blue = searching
    delay(250);
    neopixelWrite(NEOPIXEL_PIN, 0, 0, 0);
    delay(250);
  }
  Serial.println("\nJoined Zigbee network.");
  logNetworkInfo();
  neopixelWrite(NEOPIXEL_PIN, 0, 60, 0);     // green = joined
}

void loop() {
  // BOOT hold 3 s -> factory reset + rejoin.
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(100);  // debounce
    uint32_t t0 = millis();
    while (digitalRead(BUTTON_PIN) == LOW) {
      delay(50);
      if (millis() - t0 > 3000) {
        Serial.println("Factory reset -> rebooting.");
        neopixelWrite(NEOPIXEL_PIN, 60, 0, 0);  // red flash
        delay(500);
        Zigbee.factoryReset();  // reboots
      }
    }
  }

  // Reflect link state on the NeoPixel: dim green when connected, blue blink when not.
  static uint32_t last = 0;
  if (millis() - last > 500) {
    last = millis();
    if (Zigbee.connected()) {
      neopixelWrite(NEOPIXEL_PIN, 0, 8, 0);   // idle dim green
    } else {
      static bool b = false;
      b = !b;
      neopixelWrite(NEOPIXEL_PIN, 0, 0, b ? 40 : 0);
    }
  }
  delay(20);
}
