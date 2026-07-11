#pragma once
#include <stddef.h>
#include <stdint.h>

#include "../channels/channels.h"

// Слой config: структура настроек + (де)сериализация JSON.
// Чистый C++ (snprintf/strtod), без Arduino/ESP8266/Wi-Fi.

namespace config {

// Карта каналов. GPIO фиксированы прошивкой (boot-safe, см. config.cpp/README).
struct ChannelDef {
  const char* key;   // ключ канала в UI
  const char* name;  // человекочитаемое название (UTF-8)
  uint8_t power_w;
  uint8_t default_gpio;
};

extern const ChannelDef CHANNEL_DEFS[channels::NUM_CHANNELS];

// Лампы идентичны (R10W) → калибровка общая для всех каналов.
struct Config {
  uint32_t pwm_freq_hz;               // 10..30000
  uint8_t max_duty_cap_pct;           // 0..100
  channels::Calib calib;              // общая: gamma / min_duty / max_duty / soft_start
  uint8_t gpio[channels::NUM_CHANNELS];  // пины (фиксированы прошивкой)
  // Тайминги тест-режимов анимации (мс), редактируются из web (100..10000):
  uint16_t chase_ms;   // CHASE — шаг смены активного канала
  uint16_t seq_ms;     // SEQ — шаг наполнения
  uint16_t pulse_ms;   // PULSE — период «дыхания»
  uint16_t alt_ms;     // ALT — шаг чёт/нечёт
};

void setDefaults(Config& cfg);

// Приводит все поля к допустимым диапазонам (в т.ч. min_duty <= max_duty).
void clamp(Config& cfg);

// Сериализация в человекочитаемый JSON. Возвращает длину (0 при нехватке буфера).
size_t toJson(const Config& cfg, char* buf, size_t buflen);

// Применяет найденные в JSON поля к cfg (частичный JSON допустим), значения
// клампятся к диапазонам. Поле "gpio" сознательно игнорируется: пины
// фиксированы прошивкой. Возвращает false, если не распознано ни одно поле.
bool fromJson(const char* json, Config& cfg);

}  // namespace config
