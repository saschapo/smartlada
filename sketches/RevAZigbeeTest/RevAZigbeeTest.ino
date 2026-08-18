// RevAZigbeeTest - Zigbee end-device bring-up on the assembled SmartLada RevA (ESP32-C6).
//
// Goal of THIS test: prove the module joins a Zigbee coordinator (Yandex Station Midi)
// as a standard dimmable light, and that On/Off + brightness from Alice drive the four
// lamp channels. No Wi-Fi / no web here on purpose: the C6 shares one 2.4 GHz radio
// between Wi-Fi and 802.15.4, so this first test runs pure Zigbee and logs over USB.
// Web-UI + Zigbee coexistence is a separate later step.
//
// Hardware (from the RevA firmware SmartLadaC6):
//   - lamp channels: GPIO0..3 (stop/reverse/turn/marker), low-side D4184, active-high
//     PWM (higher duty = brighter). Driven together as one logical light here.
//   - status NeoPixel: GPIO8 (onboard WS2812).
//   - BOOT button: GPIO9 (active-low) -> hold 3 s for Zigbee factory reset + rejoin.
//   - native USB-Serial-JTAG for the log.
//
// [CRITICAL with lamps + 12 V connected] Common ground C6 <-> 12 V PSU, fuse in place.
// Channels are forced OUTPUT+LOW before anything else, as in the main firmware.
//
// Build (Arduino IDE):  Tools -> Zigbee mode = "Zigbee ED (end device)",
//                       Partition Scheme = "Zigbee 8MB with spiffs",
//                       USB CDC On Boot = "Enabled".
// Build (arduino-cli):  see README.md for the exact FQBN.

#include <Arduino.h>

#ifndef ZIGBEE_MODE_ED
#error "Select Tools -> Zigbee mode -> Zigbee ED (end device)"
#endif

#include "Zigbee.h"

// ---------------- hardware map (matches SmartLadaC6 RevA) ----------------
static const uint8_t CH_PINS[4] = {0, 1, 2, 3};  // stop, reverse, turn, marker
static const uint8_t NUM_CH = 4;
static const uint8_t NEOPIXEL_PIN = 8;   // onboard WS2812
static const uint8_t BUTTON_PIN = 9;     // BOOT, active-low

// ---------------- PWM (LEDC), same shape as the main firmware -----------
static const uint32_t PWM_FREQ = 20000;  // 20 kHz, flicker-free for these lamps
static const uint8_t PWM_RES_BITS = 10;  // 0..1023
static const uint16_t DUTY_MAX = 1023;
static const uint16_t MIN_DUTY = 20;     // dead-zone floor (matches bench calibration)
static const float GAMMA = 1.9f;         // perceptual curve chosen on the bench

// ---------------- Zigbee endpoint ---------------------------------------
#define ZB_LIGHT_ENDPOINT 10
ZigbeeDimmableLight zbLight = ZigbeeDimmableLight(ZB_LIGHT_ENDPOINT);

// Map a Zigbee ZCL level (1..254) to an LEDC duty via gamma + min-duty floor.
static uint16_t levelToDuty(uint8_t level) {
  if (level == 0) return 0;
  float norm = (float)level / 254.0f;         // 0..1
  float curved = powf(norm, GAMMA);           // perceptual
  uint16_t duty = MIN_DUTY + (uint16_t)((DUTY_MAX - MIN_DUTY) * curved + 0.5f);
  return duty > DUTY_MAX ? DUTY_MAX : duty;
}

// Called by the Zigbee stack whenever On/Off or Level changes from the coordinator.
static void applyLight(bool state, uint8_t level) {
  uint16_t duty = state ? levelToDuty(level) : 0;
  for (uint8_t i = 0; i < NUM_CH; i++) ledcWrite(CH_PINS[i], duty);
  Serial.printf("Light: state=%s level=%u -> duty=%u\n", state ? "ON" : "OFF", level, duty);
}

// Identify: the coordinator blinks the device so the user can find it.
static void onIdentify(uint16_t time) {
  static uint8_t on = 1;
  if (time == 0) {                 // identify finished -> restore real light state
    zbLight.restoreLight();
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

void setup() {
  // FORCE-SAFE: lamps off before anything else.
  channelsForceLow();

  Serial.begin(115200);
  delay(50);
  Serial.println("\nRevAZigbeeTest - Zigbee dimmable light bring-up");

  // Attach LEDC per channel, start at 0 (off).
  for (uint8_t i = 0; i < NUM_CH; i++) {
    if (!ledcAttach(CH_PINS[i], PWM_FREQ, PWM_RES_BITS))
      Serial.printf("LEDC attach FAILED on GPIO%u\n", CH_PINS[i]);
    ledcWrite(CH_PINS[i], 0);
  }

  neopixelWrite(NEOPIXEL_PIN, 0, 0, 0);   // dark
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Register callbacks and identity before starting the stack.
  zbLight.onLightChange(applyLight);
  zbLight.onIdentify(onIdentify);
  zbLight.setManufacturerAndModel("SmartLada", "RevA-Light4");

  Zigbee.addEndpoint(&zbLight);

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
