#include "platform_pwm.h"

#include <Arduino.h>

namespace platform_pwm {

void forceAllLow(const uint8_t* pins, uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    // LOW в выходной регистр до переключения в OUTPUT — без импульса на затворе
    digitalWrite(pins[i], LOW);
    pinMode(pins[i], OUTPUT);
    digitalWrite(pins[i], LOW);
  }
}

void begin(uint16_t freq_hz) {
  analogWriteRange(1023);
  analogWriteFreq(freq_hz);
}

void setFreq(uint16_t freq_hz) {
  analogWriteFreq(freq_hz);
  analogWriteRange(1023);
}

void write(uint8_t pin, uint16_t duty) {
  if (duty == 0) {
    analogWrite(pin, 0);
    digitalWrite(pin, LOW);  // страховка: физический 0 без остаточного PWM
    return;
  }
  analogWrite(pin, duty > 1023 ? 1023 : duty);
}

}  // namespace platform_pwm
