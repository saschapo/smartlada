#pragma once
#include <stdint.h>

// Слой channels: чистая математика duty для 4 каналов.
// [ПЕРЕНОСИТСЯ на ESP32-C6] Никаких зависимостей от Arduino/ESP8266/Wi-Fi.
// Время передаётся снаружи (millis() вызывающего кода).

namespace channels {

constexpr uint16_t DUTY_MAX = 1023;  // 10-битная шкала duty
constexpr uint8_t NUM_CHANNELS = 4;

struct Calib {
  float gamma;             // 1.0..2.5, дефолт 1.9 (диапазон/дефолты — config.cpp)
  uint16_t min_duty;       // 0..1023
  uint16_t max_duty;       // 0..1023
  uint16_t soft_start_ms;  // 0..1000
};

// Порядок операций зафиксирован ТЗ:
// slider% -> gamma -> масштаб [min_duty..max_duty] -> глобальный кап -> soft-start ramp
class Channel {
 public:
  void setCalib(const Calib& c, uint32_t now_ms);
  const Calib& calib() const { return calib_; }

  // 0..100; 0% = гарантированный физический 0 даже при min_duty > 0
  void setPercent(uint8_t pct, uint32_t now_ms);
  uint8_t percent() const { return pct_; }

  // Глобальный кап, применяется поверх пер-канального max_duty
  void setCapPct(uint8_t cap_pct, uint32_t now_ms);

  // Текущий duty с учётом soft-start ramp; вызывать периодически из loop().
  uint16_t update(uint32_t now_ms);

  // Последний вычисленный duty (без побочных эффектов, для индикации).
  uint16_t currentDuty() const { return current_; }

 private:
  uint16_t computeTarget() const;
  void retarget(uint32_t now_ms);

  // Фоллбэк до первого setCalib(); setup() всегда перезатирает дефолтами config.cpp.
  Calib calib_{1.9f, 20, DUTY_MAX, 30};
  uint8_t pct_ = 0;
  uint8_t cap_pct_ = 100;
  uint16_t target_ = 0;
  uint16_t current_ = 0;
  uint16_t ramp_from_ = 0;
  uint32_t ramp_start_ms_ = 0;
};

// Глобальная мастер-яркость 0..100: множитель ИТОГОВОГО duty всех каналов, поверх
// пер-канальных процентов. Действует во ВСЕХ режимах, включая анимации (в отличие
// от setPercent, который анимация перезаписывает каждый тик). 100 = без ослабления.
void setMasterPct(uint8_t pct);
uint8_t masterPct();

}  // namespace channels
