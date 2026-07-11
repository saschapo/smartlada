#pragma once
#include <stddef.h>
#include <stdint.h>

#include "../channels/channels.h"

// Слой config: структура настроек + (де)сериализация JSON.
// [ПЕРЕНОСИТСЯ на ESP32-C6] Чистый C++ (snprintf/strtod), без Arduino/ESP8266/Wi-Fi.

namespace config {

// Карта каналов (ТЗ, раздел 2). GPIO предварительные — сверить с разводкой HW-365A.
struct ChannelDef {
  const char* key;   // ключ секции в JSON и UI
  const char* name;  // человекочитаемое название (UTF-8)
  uint8_t power_w;
  uint8_t default_gpio;
  uint16_t default_soft_start_ms;
};

extern const ChannelDef CHANNEL_DEFS[channels::NUM_CHANNELS];

struct ChannelConfig {
  uint8_t gpio;
  channels::Calib calib;
};

struct Config {
  uint16_t pwm_freq_hz;      // 100..1000, дефолт 150
  uint8_t max_duty_cap_pct;  // 0..100, дефолт 60 (force-safe)
  ChannelConfig ch[channels::NUM_CHANNELS];
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
