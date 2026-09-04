// RevCZigbeeFx - Zigbee 4 lamps + a 5th "Fara" color device (master + effects).
//
// Extends RevCZigbee4EP with the chosen final architecture:
//   EP10..13  ZigbeeDimmableLight   -> 4 individual lamps (turn/brake/marker/reverse)
//   EP14      ZigbeeColorDimmableLight "Fara" -> master brightness + effect selector by color
//
// Effect selection by color (hue quantized into zones; Alice preset swatches act as the
// discrete palette). White/low-saturation = no effect (lamps show individually):
//   red -> blink (turn),   green -> chase,   blue -> fade/breathe
//
// Priority / compositor (per loop):
//   master = Fara.on ? Fara.level/254 : 1.0            (Fara off = per-lamp passthrough)
//   effect active (Fara.on && saturated): frame = animation; else frame = individual lamps
//   out[i] = frame[i] * master  ->  slew (soft-start)  ->  gamma  ->  LEDC
// Soft-start smooths Alice's coarse ~15-unit/100ms brightness steps AND power-on inrush in
// normal mode (SOFT_NORMAL_MS); effects use a short slew (SOFT_FX_MS) so blinks stay crisp.
//
// BENCH: RevA board + MuseLab nanoESP32-C6. Channel GPIOs = Rev C routing {1,0,2,3}.
// Status WS2812 GPIO8. BOOT GPIO9 hold 3 s = factory reset. Raw ZCL logging kept.
//
// Build: arduino-cli compile -b "esp32:esp32:esp32c6:ZigbeeMode=ed,PartitionScheme=zigbee_8MB,CDCOnBoot=cdc,FlashSize=16M" sketches/RevCZigbeeFx

#include <Arduino.h>
#ifndef ZIGBEE_MODE_ED
#error "Select Zigbee mode 'Zigbee ED (end device)' (FQBN ...:ZigbeeMode=ed)"
#endif
#include "Zigbee.h"
#include <math.h>

#define DISTINCT_MODEL 1  // distinct ModelIdentifier per endpoint (readable default names)

// ---------------- hardware ----------------
static const uint8_t NUM_CH = 4;
static const uint8_t CH_PINS[NUM_CH] = {1, 0, 2, 3};              // OUT0..OUT3 (Rev C)
static const uint8_t EP_ID[NUM_CH] = {10, 11, 12, 13};
static const char *LAMP_NAME[NUM_CH] = {"Turn", "Brake", "Marker", "Reverse"};
static const uint8_t FARA_EP = 14;
static const uint8_t NEOPIXEL_PIN = 8;
static const uint8_t BUTTON_PIN = 9;

// ---------------- PWM ----------------
static const uint32_t PWM_FREQ = 20000;
static const uint8_t PWM_RES_BITS = 10;
static const uint16_t DUTY_MAX = 1023;
static const uint16_t MIN_DUTY = 20;
static const float GAMMA = 1.9f;

// ---------------- soft-start ----------------
static const uint16_t SOFT_NORMAL_MS = 150;  // smooths Alice's stepped brightness + inrush
static const uint16_t SOFT_FX_MS = 20;        // effects: just tame inrush, keep edges crisp

// ---------------- effect selection ----------------
static const uint8_t SAT_THRESH = 40;         // saturation below this = white = no effect
enum Effect { FX_NONE, FX_BLINK, FX_CHASE, FX_FADE };

// ---------------- shared state (written by Zigbee callbacks, read by loop) ----------------
struct Lamp { bool on; uint8_t bri; };        // bri = Zigbee level 0..254
static volatile Lamp g_lamp[NUM_CH] = {{false, 254}, {false, 254}, {false, 254}, {false, 254}};
static volatile bool    g_faraOn = false;
static volatile uint8_t g_master = 254, g_hue = 0, g_sat = 0;

// ---------------- endpoints ----------------
class LoggingLight : public ZigbeeDimmableLight {
public:
  LoggingLight(uint8_t ep, uint8_t ch) : ZigbeeDimmableLight(ep), _ep(ep), _ch(ch) {}
  void zbAttributeSet(const esp_zb_zcl_set_attr_value_message_t *m) override {
    uint16_t cl = m->info.cluster, aid = m->attribute.id;
    if (cl == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF && aid == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID
        && m->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
      g_lamp[_ch].on = *(bool *)m->attribute.data.value;
    } else if (cl == ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL
               && aid == ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID
               && m->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U8) {
      g_lamp[_ch].bri = *(uint8_t *)m->attribute.data.value;
    }
    Serial.printf("[%lu] EP%u ch%u cl=0x%04x attr=0x%04x -> on=%d bri=%u\n",
                  (unsigned long)millis(), _ep, _ch, cl, aid, g_lamp[_ch].on, g_lamp[_ch].bri);
  }
private:
  uint8_t _ep, _ch;
};

LoggingLight zbCh0(10, 0), zbCh1(11, 1), zbCh2(12, 2), zbCh3(13, 3);
static LoggingLight *zbCh[NUM_CH] = {&zbCh0, &zbCh1, &zbCh2, &zbCh3};
ZigbeeColorDimmableLight zbFara(FARA_EP);

// Fara color/level/power -> effect + master (decoded HSV callback).
static void onFaraHsv(bool state, uint8_t hue, uint8_t sat, uint8_t value) {
  g_faraOn = state;
  g_hue = hue;
  g_sat = sat;
  g_master = value;
  Serial.printf("[%lu] FARA on=%d hue=%u sat=%u val=%u\n",
                (unsigned long)millis(), state, hue, sat, value);
}

// ---------------- effects (fill 0..254 per channel) ----------------
static Effect hueToEffect(uint8_t hue, uint8_t sat) {
  if (sat < SAT_THRESH) return FX_NONE;               // white-ish = normal mode
  if (hue < 43 || hue >= 213) return FX_BLINK;         // red
  if (hue < 128) return FX_CHASE;                      // green
  return FX_FADE;                                       // blue
}

static void effectFrame(Effect fx, uint32_t now, uint8_t out[NUM_CH]) {
  switch (fx) {
    case FX_BLINK: {                                    // all blink together ~1.5 Hz
      uint8_t v = ((now / 340) & 1) ? 254 : 0;
      for (uint8_t i = 0; i < NUM_CH; i++) out[i] = v;
      break;
    }
    case FX_CHASE: {                                    // running light, 150 ms/step
      uint8_t step = (now / 150) % NUM_CH;
      for (uint8_t i = 0; i < NUM_CH; i++) out[i] = (i == step) ? 254 : 0;
      break;
    }
    case FX_FADE: {                                     // breathe, ~3 s period
      float ph = (float)(now % 3000) / 3000.0f * 2.0f * (float)M_PI;
      uint8_t v = (uint8_t)((sinf(ph) * 0.5f + 0.5f) * 254.0f);
      for (uint8_t i = 0; i < NUM_CH; i++) out[i] = v;
      break;
    }
    default:
      for (uint8_t i = 0; i < NUM_CH; i++) out[i] = 0;
  }
}

// ---------------- output pipeline: level 0..254 -> gamma/min -> duty ----------------
static uint16_t levelToDuty(uint8_t level) {
  if (level == 0) return 0;
  float curved = powf((float)level / 254.0f, GAMMA);
  uint16_t d = MIN_DUTY + (uint16_t)((DUTY_MAX - MIN_DUTY) * curved + 0.5f);
  return d > DUTY_MAX ? DUTY_MAX : d;
}

static float s_actual[NUM_CH] = {0, 0, 0, 0};
static uint32_t s_lastMs = 0;

static void driveChannels(uint32_t now) {
  bool faraOn = g_faraOn;
  float master = faraOn ? (float)g_master / 254.0f : 1.0f;
  Effect fx = faraOn ? hueToEffect(g_hue, g_sat) : FX_NONE;

  uint8_t frame[NUM_CH];
  if (fx != FX_NONE) {
    effectFrame(fx, now, frame);
  } else {
    for (uint8_t i = 0; i < NUM_CH; i++) frame[i] = g_lamp[i].on ? g_lamp[i].bri : 0;
  }

  uint16_t soft = (fx == FX_NONE) ? SOFT_NORMAL_MS : SOFT_FX_MS;
  uint32_t dt = now - s_lastMs;
  s_lastMs = now;
  float maxStep = (soft == 0) ? 1e9f : (254.0f * (float)dt / (float)soft);

  for (uint8_t i = 0; i < NUM_CH; i++) {
    float t = (float)frame[i] * master;               // master scale
    if (s_actual[i] < t) s_actual[i] = (t - s_actual[i] <= maxStep) ? t : s_actual[i] + maxStep;
    else if (s_actual[i] > t) s_actual[i] = (s_actual[i] - t <= maxStep) ? t : s_actual[i] - maxStep;
    ledcWrite(CH_PINS[i], levelToDuty((uint8_t)(s_actual[i] + 0.5f)));
  }
}

static void onIdentify(uint16_t time) {
  static uint8_t on = 1;
  if (time == 0) return;                                // loop keeps driving channels from state
  neopixelWrite(NEOPIXEL_PIN, 60 * on, 60 * on, 60 * on);
  on = !on;
}

static void channelsForceLow() {
  for (uint8_t i = 0; i < NUM_CH; i++) {
    digitalWrite(CH_PINS[i], LOW);
    pinMode(CH_PINS[i], OUTPUT);
    digitalWrite(CH_PINS[i], LOW);
  }
}

void setup() {
  channelsForceLow();
  Serial.begin(115200);
  delay(50);
  Serial.println("\nRevCZigbeeFx - 4 lamps + Fara color(master+effects)");

  for (uint8_t i = 0; i < NUM_CH; i++) {
    ledcAttach(CH_PINS[i], PWM_FREQ, PWM_RES_BITS);
    ledcWrite(CH_PINS[i], 0);
  }
  neopixelWrite(NEOPIXEL_PIN, 0, 0, 0);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  s_lastMs = millis();

  Zigbee.setDebugMode(true);

  for (uint8_t i = 0; i < NUM_CH; i++) {
    zbCh[i]->onIdentify(onIdentify);
#if DISTINCT_MODEL
    zbCh[i]->setManufacturerAndModel("SmartLada", LAMP_NAME[i]);
#else
    zbCh[i]->setManufacturerAndModel("SmartLada", "RevC-Lamp");
#endif
    Zigbee.addEndpoint(zbCh[i]);
  }
  // Fara: advertise hue/saturation color so Alice sends hue (not XY) -> onFaraHsv.
  zbFara.setLightColorCapabilities(ZIGBEE_COLOR_CAPABILITY_HUE_SATURATION);
  zbFara.onLightChangeHsv(onFaraHsv);
  zbFara.onIdentify(onIdentify);
  zbFara.setManufacturerAndModel("SmartLada", "Fara");
  Zigbee.addEndpoint(&zbFara);

  Serial.println("Starting Zigbee (end device)...");
  if (!Zigbee.begin()) {
    Serial.println("Zigbee failed to start! Rebooting in 1 s.");
    delay(1000);
    ESP.restart();
  }
  Serial.println("Joining network (open pairing on the Midi now)...");
  while (!Zigbee.connected()) {
    Serial.print(".");
    neopixelWrite(NEOPIXEL_PIN, 0, 0, 40);
    delay(250);
    neopixelWrite(NEOPIXEL_PIN, 0, 0, 0);
    delay(250);
  }
  Serial.println("\nJoined Zigbee network.");
  neopixelWrite(NEOPIXEL_PIN, 0, 60, 0);
}

void loop() {
  // BOOT hold 3 s -> factory reset.
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(100);
    uint32_t t0 = millis();
    while (digitalRead(BUTTON_PIN) == LOW) {
      delay(50);
      if (millis() - t0 > 3000) {
        Serial.println("Factory reset -> rebooting.");
        neopixelWrite(NEOPIXEL_PIN, 60, 0, 0);
        delay(500);
        Zigbee.factoryReset();
      }
    }
  }

  driveChannels(millis());                              // compositor + soft-start every loop

  static uint32_t last = 0;
  if (millis() - last > 500) {                          // link-state neopixel
    last = millis();
    neopixelWrite(NEOPIXEL_PIN, 0, Zigbee.connected() ? 8 : 0, 0);
  }
  delay(5);
}
