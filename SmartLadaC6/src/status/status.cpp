#include "status.h"

#include <Arduino.h>
#include <WiFi.h>
#include <math.h>

#include "../anim/anim.h"

namespace status {

static constexpr uint8_t NEOPIXEL_PIN = 8;  // бортовой WS2812 ESP32-C6
static constexpr uint8_t MAX_BRIGHT = 40;   // потолок яркости (диод очень яркий)
static constexpr uint16_t UPDATE_MS = 17;   // частота обновления кадра (~59 fps)

static constexpr uint16_t OVERLAY_HOLD_MS = 500;  // сколько держать индикацию яркости

static channels::Channel* s_chans = nullptr;  // в DRIVE читаем проценты каналов
static uint32_t s_last = 0;
static uint8_t s_ovlPct = 0;      // уровень яркости для оверлея
static uint32_t s_ovlUntil = 0;   // до какого момента оверлей активен

void begin(channels::Channel* chans) {
  s_chans = chans;
  neopixelWrite(NEOPIXEL_PIN, 0, 0, 0);  // старт погашенным
}

void brightnessOverlay(uint8_t pct, uint32_t now_ms) {
  s_ovlPct = pct > 100 ? 100 : pct;
  s_ovlUntil = now_ms + OVERLAY_HOLD_MS;
}

// Базовый цвет режима на полной MAX_BRIGHT (индекс = anim::Mode, кроме MANUAL).
static void modeColor(uint8_t m, uint8_t& r, uint8_t& g, uint8_t& b) {
  switch (m) {
    case anim::TURN:  r = MAX_BRIGHT; g = MAX_BRIGHT * 45 / 100; b = 0; break;  // янтарь
    case anim::CHASE: r = 0; g = MAX_BRIGHT; b = MAX_BRIGHT; break;             // циан
    case anim::SEQ:   r = 0; g = MAX_BRIGHT; b = 0; break;                      // зелёный
    case anim::PULSE: r = MAX_BRIGHT * 60 / 100; g = 0; b = MAX_BRIGHT; break;  // фиолет
    case anim::ALT:   r = MAX_BRIGHT; g = 0; b = 0; break;                      // красный
    default:          r = g = b = 0; break;
  }
}

void tick(uint32_t now_ms) {
  if (now_ms - s_last < UPDATE_MS) return;
  s_last = now_ms;

  // Оверлей яркости (диммирование кнопкой) — приоритет над всем остальным.
  // Крайние точки маркируются цветом: 0% — яркий красный, 100% — полный зелёный.
  // Середина — белый с гамма-коррекцией: линейный duty на глаз почти не меняется
  // в верхней трети (70..100 «одинаковы»), поэтому шкалу гамма-кодируем (b∝x^2.2),
  // чтобы воспринимаемая яркость менялась равномерно по всему ходу 0..100%.
  if ((int32_t)(s_ovlUntil - now_ms) > 0) {
    if (s_ovlPct == 0) {
      neopixelWrite(NEOPIXEL_PIN, MAX_BRIGHT, 0, 0);  // 0% — яркий красный
    } else if (s_ovlPct >= 100) {
      neopixelWrite(NEOPIXEL_PIN, 0, MAX_BRIGHT, 0);  // 100% — полный зелёный
    } else {
      float f = powf((float)s_ovlPct / 100.0f, 2.2f);
      uint8_t b = (uint8_t)(f * MAX_BRIGHT + 0.5f);
      if (b == 0) b = 1;  // не гаснуть на нижних процентах
      neopixelWrite(NEOPIXEL_PIN, b, b, b);  // белый по гамма-скорректированному уровню
    }
    return;
  }

  const uint8_t m = anim::mode();

  if (m == anim::MANUAL) {
    // Статус сети: клиент — ровный зелёный, иначе голубое «дыхание» (3 с)
    if (WiFi.softAPgetStationNum() > 0) {
      neopixelWrite(NEOPIXEL_PIN, 0, MAX_BRIGHT, 0);
    } else {
      uint16_t phase = now_ms % 3000;
      uint16_t tri = phase < 1500 ? phase : (3000 - phase);  // 0..1500..0
      uint8_t b = (uint8_t)((uint32_t)tri * MAX_BRIGHT / 1500);
      neopixelWrite(NEOPIXEL_PIN, 0, b / 2, b);
    }
    return;
  }

  if (m == anim::DRIVE) {
    // Полная синхронизация с фонарём: цвет = доминирующий сигнал (как в машине).
    // Приоритет: поворот/аварийка > задний > стоп > габарит-фон.
    uint8_t stp = s_chans[0].percent(), rev = s_chans[1].percent();
    uint8_t trn = s_chans[2].percent(), mrk = s_chans[3].percent();
    if (trn > 0) {  // жёлтый (мигает в такт лампе)
      neopixelWrite(NEOPIXEL_PIN, MAX_BRIGHT, MAX_BRIGHT * 80 / 100, 0);
    } else if (rev > 0) {  // белый (задний ход)
      neopixelWrite(NEOPIXEL_PIN, MAX_BRIGHT, MAX_BRIGHT, MAX_BRIGHT);
    } else if (stp > 0) {  // красный (стоп)
      neopixelWrite(NEOPIXEL_PIN, MAX_BRIGHT, 0, 0);
    } else if (mrk > 0) {  // едет с габаритом — постоянное тусклое белое (ниже reverse-full)
      neopixelWrite(NEOPIXEL_PIN, MAX_BRIGHT / 3, MAX_BRIGHT / 3, MAX_BRIGHT / 3);
    } else {
      neopixelWrite(NEOPIXEL_PIN, 0, 0, 0);
    }
    return;
  }

  // Прочие анимации — цвет по режиму, яркость по паттерну
  uint8_t r, g, b;
  modeColor(m, r, g, b);
  uint8_t br;  // 0..MAX_BRIGHT
  if (m == anim::PULSE) {
    uint16_t phase = now_ms % 2000;             // «дыхание» в такт лампам
    uint16_t tri = phase < 1000 ? phase : (2000 - phase);
    br = (uint8_t)((uint32_t)tri * MAX_BRIGHT / 1000);
  } else {
    br = ((now_ms / 250) % 2 == 0) ? MAX_BRIGHT : 0;  // мигание ~2 Гц
  }
  neopixelWrite(NEOPIXEL_PIN, (uint8_t)((uint16_t)r * br / MAX_BRIGHT),
                (uint8_t)((uint16_t)g * br / MAX_BRIGHT),
                (uint8_t)((uint16_t)b * br / MAX_BRIGHT));
}

}  // namespace status
