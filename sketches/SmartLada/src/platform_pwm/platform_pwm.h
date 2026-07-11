#pragma once
#include <stdint.h>

// platform_pwm layer: PWM on ESP8266 (stock core, analogWrite*).
// [DISCARDED] On ESP32-C6 replaced by an LEDC implementation with the same interface.

namespace platform_pwm {

// Force-safe: drive all pins to OUTPUT+LOW before PWM init.
void forceAllLow(const uint8_t* pins, uint8_t count);

void begin(uint16_t freq_hz);
void setFreq(uint16_t freq_hz);

// duty 0..1023; duty 0 = guaranteed LOW (PWM on the pin stops).
void write(uint8_t pin, uint16_t duty);

}  // namespace platform_pwm
