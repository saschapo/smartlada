#include "thermal.h"

#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <math.h>

namespace thermal {

static constexpr uint8_t ONE_WIRE_PIN = 13;   // D7 on the HW-364A header
static constexpr uint8_t RESOLUTION_BITS = 12;
static constexpr uint32_t SAMPLE_PERIOD_MS = 2000;

const char* const LABELS[NUM_SENSORS] = {"psu", "body"};

// ROM->slot map (order = LABELS). Addresses read off the bus with dumpBus().
// PSU/BODY assignment is PROVISIONAL until the sensors are physically labelled:
// heat one point and see which value rises; if it's the wrong slot, swap the two
// lines below. All-zero = unconfigured -> fall back to bus-discovery order.
static const DeviceAddress CONFIGURED[NUM_SENSORS] = {
    {0x28, 0x45, 0x6E, 0x6F, 0x00, 0x00, 0x00, 0x39},  // psu (replacement 2026-07-12)
    {0x28, 0xDF, 0x2D, 0x6D, 0x00, 0x00, 0x00, 0xE7},  // body
};

static OneWire s_wire(ONE_WIRE_PIN);
static DallasTemperature s_sensors(&s_wire);

static DeviceAddress s_rom[NUM_SENSORS];
static bool s_have[NUM_SENSORS];      // a sensor is assigned to this slot
static bool s_present[NUM_SENSORS];   // last read was valid
static float s_temp[NUM_SENSORS];     // last good reading (NaN otherwise)
static float s_raw[NUM_SENSORS];      // TEMP debug: last raw getTempC
static uint32_t s_err[NUM_SENSORS];   // TEMP debug: invalid reads since begin()

enum State { IDLE, CONVERTING };
static State s_state = IDLE;
static uint32_t s_cycleStart = 0;   // last time a conversion was requested
static uint32_t s_convWaitMs = 750;

static bool isZero(const DeviceAddress a) {
  for (uint8_t b = 0; b < 8; b++)
    if (a[b]) return false;
  return true;
}

static void resolveAddresses() {
  bool anyConfigured = false;
  for (uint8_t i = 0; i < NUM_SENSORS; i++)
    if (!isZero(CONFIGURED[i])) anyConfigured = true;

  for (uint8_t i = 0; i < NUM_SENSORS; i++) {
    s_have[i] = false;
    memset(s_rom[i], 0, 8);
  }

  if (anyConfigured) {
    for (uint8_t i = 0; i < NUM_SENSORS; i++) {
      if (isZero(CONFIGURED[i])) continue;
      memcpy(s_rom[i], CONFIGURED[i], 8);
      s_have[i] = true;
    }
  } else {
    // Unconfigured: take sensors in bus-discovery order (bring-up convenience).
    uint8_t n = s_sensors.getDeviceCount();
    for (uint8_t i = 0; i < NUM_SENSORS && i < n; i++)
      s_have[i] = s_sensors.getAddress(s_rom[i], i);
  }
}

void begin() {
  s_sensors.begin();
  s_sensors.setResolution(RESOLUTION_BITS);
  s_sensors.setWaitForConversion(false);   // non-blocking: we poll the timer
  s_convWaitMs = s_sensors.millisToWaitForConversion(RESOLUTION_BITS);
  resolveAddresses();
  for (uint8_t i = 0; i < NUM_SENSORS; i++) {
    s_present[i] = false;
    s_temp[i] = NAN;
    s_raw[i] = NAN;
    s_err[i] = 0;
  }
  s_state = IDLE;
  s_cycleStart = millis() - SAMPLE_PERIOD_MS;   // read on the first tick
}

static void readAll() {
  for (uint8_t i = 0; i < NUM_SENSORS; i++) {
    if (!s_have[i]) {
      s_present[i] = false;
      s_temp[i] = NAN;
      continue;
    }
    float t = s_sensors.getTempC(s_rom[i]);
    // DEVICE_DISCONNECTED_C (-127) and any out-of-spec value fail the range test;
    // the full conversion wait rules out the false 85.0 power-on reading.
    bool ok = (t > -55.0f && t < 125.0f);
    s_raw[i] = t;                 // TEMP debug
    if (!ok) s_err[i]++;          // TEMP debug
    s_present[i] = ok;
    s_temp[i] = ok ? t : NAN;
  }
}

void tick(uint32_t now_ms) {
  switch (s_state) {
    case IDLE:
      if (now_ms - s_cycleStart >= SAMPLE_PERIOD_MS) {
        s_sensors.requestTemperatures();   // async (waitForConversion == false)
        s_cycleStart = now_ms;
        s_state = CONVERTING;
      }
      break;
    case CONVERTING:
      if (now_ms - s_cycleStart >= s_convWaitMs) {
        readAll();
        s_state = IDLE;
      }
      break;
  }
}

float celsius(uint8_t idx) { return idx < NUM_SENSORS ? s_temp[idx] : NAN; }
bool present(uint8_t idx) { return idx < NUM_SENSORS ? s_present[idx] : false; }

bool getRom(uint8_t idx, uint8_t out[8]) {
  if (idx >= NUM_SENSORS || !s_have[idx]) return false;
  memcpy(out, s_rom[idx], 8);
  return true;
}

float rawC(uint8_t idx) { return idx < NUM_SENSORS ? s_raw[idx] : NAN; }
uint32_t errorCount(uint8_t idx) { return idx < NUM_SENSORS ? s_err[idx] : 0; }

void dumpBus(Print& out) {
  uint8_t n = s_sensors.getDeviceCount();
  out.printf("thermal: %u device(s) on 1-Wire bus (GPIO%u)\n", n, ONE_WIRE_PIN);
  s_sensors.requestTemperatures();
  delay(s_convWaitMs);
  for (uint8_t i = 0; i < n; i++) {
    DeviceAddress a;
    if (!s_sensors.getAddress(a, i)) {
      out.printf("  [%u] address read failed\n", i);
      continue;
    }
    out.printf("  [%u] ", i);
    for (uint8_t b = 0; b < 8; b++) out.printf("%02X", a[b]);
    out.printf("  %.2f C\n", s_sensors.getTempC(a));
  }
  out.println("  -> paste addresses into CONFIGURED[] (thermal.cpp) to pin points");
}

void dumpScratch(Print& out) {
  uint8_t n = s_sensors.getDeviceCount();
  out.printf("scratchpad check: %u device(s)  (genuine: sp[5]=FF sp[7]=10 cfg=7F)\n", n);
  for (uint8_t i = 0; i < n; i++) {
    DeviceAddress a;
    if (!s_sensors.getAddress(a, i)) {
      out.printf("  [%u] address read failed\n", i);
      continue;
    }
    uint8_t sp[9];
    bool crcok = s_sensors.readScratchPad(a, sp);
    out.printf("  [%u] ", i);
    for (uint8_t b = 0; b < 8; b++) out.printf("%02X", a[b]);
    out.print("  sp=");
    for (uint8_t b = 0; b < 9; b++) out.printf("%02X", sp[b]);
    // cpetrich reserved-byte signature for a genuine DS18B20 die.
    bool genuine = crcok && sp[5] == 0xFF && sp[7] == 0x10 && sp[4] == 0x7F;
    out.printf("  crc=%s cfg=%02X -> %s\n", crcok ? "ok" : "BAD", sp[4],
               genuine ? "genuine-like" : "CLONE?");
  }
}

}  // namespace thermal
