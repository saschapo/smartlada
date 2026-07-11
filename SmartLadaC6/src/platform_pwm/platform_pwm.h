#pragma once
#include <stdint.h>

// Слой platform_pwm: аппаратный PWM на ESP32-C6 через LEDC.
// Интерфейс сохранён по смыслу от ESP8266-стенда; отличие — begin() принимает
// список пинов: LEDC требует явного attach канала на пин (нет автопривязки
// как у analogWrite). Шкала duty 0..1023 (10 бит) — совместима со слоем channels.

namespace platform_pwm {

// Force-safe: перевести все пины в OUTPUT+LOW до инициализации PWM.
void forceAllLow(const uint8_t* pins, uint8_t count);

// Привязать каждый пин к аппаратному каналу LEDC на частоте freq_hz, 10 бит.
void begin(const uint8_t* pins, uint8_t count, uint32_t freq_hz);

// Сменить частоту на всех привязанных пинах (разрешение остаётся 10 бит).
// Логирует фактически применённую частоту (LEDC округляет/ограничивает у потолка).
void setFreq(uint32_t freq_hz);

// duty 0..1023; duty 0 = удержание LOW (0% скважность).
void write(uint8_t pin, uint16_t duty);

}  // namespace platform_pwm
