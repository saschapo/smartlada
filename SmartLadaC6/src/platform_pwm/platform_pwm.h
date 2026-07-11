#pragma once
#include <stdint.h>

// platform_pwm layer: hardware PWM on ESP32-C6 via LEDC.
// Interface kept semantically from the ESP8266 bench; the difference is that
// begin() takes a pin list: LEDC needs an explicit channel-to-pin attach (there is
// no auto-binding like analogWrite). Duty scale 0..1023 (10 bit) — matches channels.

namespace platform_pwm {

// Force-safe: drive all pins to OUTPUT+LOW before PWM init.
void forceAllLow(const uint8_t* pins, uint8_t count);

// Attach each pin to a hardware LEDC channel at freq_hz, 10 bit.
void begin(const uint8_t* pins, uint8_t count, uint32_t freq_hz);

// Change the frequency on all attached pins (resolution stays 10 bit).
// Logs the actually applied frequency (LEDC rounds/clamps near the ceiling).
void setFreq(uint32_t freq_hz);

// duty 0..1023; duty 0 = hold LOW (0% duty cycle).
void write(uint8_t pin, uint16_t duty);

}  // namespace platform_pwm
