#pragma once
#include <stdint.h>

// Слой platform_pwm: PWM под ESP8266 (стандартный core, analogWrite*).
// [ВЫБРАСЫВАЕТСЯ] На ESP32-C6 заменяется реализацией на LEDC с тем же интерфейсом.

namespace platform_pwm {

// Force-safe: перевести все пины в OUTPUT+LOW до инициализации PWM.
void forceAllLow(const uint8_t* pins, uint8_t count);

void begin(uint16_t freq_hz);
void setFreq(uint16_t freq_hz);

// duty 0..1023; duty 0 = гарантированный LOW (PWM на пине останавливается).
void write(uint8_t pin, uint16_t duty);

}  // namespace platform_pwm
