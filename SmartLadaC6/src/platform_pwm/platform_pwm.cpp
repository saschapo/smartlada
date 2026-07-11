#include "platform_pwm.h"

#include <Arduino.h>

namespace platform_pwm {

static constexpr uint8_t PWM_RES_BITS = 10;  // 0..1023, matches DUTY_MAX
static constexpr uint8_t MAX_PINS = 8;

static uint8_t s_pins[MAX_PINS];
static uint8_t s_count = 0;

void forceAllLow(const uint8_t* pins, uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    // LOW into the output latch before switching to OUTPUT — no glitch on the gate
    digitalWrite(pins[i], LOW);
    pinMode(pins[i], OUTPUT);
    digitalWrite(pins[i], LOW);
  }
}

void begin(const uint8_t* pins, uint8_t count, uint32_t freq_hz) {
  if (count > MAX_PINS) count = MAX_PINS;
  s_count = count;
  for (uint8_t i = 0; i < count; i++) {
    s_pins[i] = pins[i];
    // ledcAttach allocates an LEDC channel for the pin; initial value 0 (LOW).
    // false = no LEDC channel was allocated -> write() on this pin becomes a no-op.
    if (!ledcAttach(pins[i], freq_hz, PWM_RES_BITS))
      Serial.printf("LEDC attach FAILED on GPIO%u\n", pins[i]);
    ledcWrite(pins[i], 0);
  }
}

void setFreq(uint32_t freq_hz) {
  uint32_t applied = 0;
  for (uint8_t i = 0; i < s_count; i++) {
    // returns the actually set frequency, 0 if unreachable at 10 bit
    uint32_t a = ledcChangeFrequency(s_pins[i], freq_hz, PWM_RES_BITS);
    if (i == 0) applied = a;
  }
  Serial.printf("PWM freq: req %lu Hz -> applied %lu Hz\n",
                (unsigned long)freq_hz, (unsigned long)applied);
}

void write(uint8_t pin, uint16_t duty) {
  ledcWrite(pin, duty > 1023 ? 1023 : duty);
}

}  // namespace platform_pwm
