#include "button.h"

#include <Arduino.h>

#include "../anim/anim.h"
#include "../status/status.h"

namespace button {

static constexpr uint8_t PIN_BOOT = 9;         // кнопка BOOT (active-low)
static constexpr uint16_t DEBOUNCE_MS = 30;    // антидребезг фронта
static constexpr uint16_t LONG_PRESS_MS = 500; // порог перехода в диммер
static constexpr uint32_t DIM_FULL_MS = 5000;  // ход яркости 0..100% за 5 с

static channels::Channel* s_ch = nullptr;

// Антидребезг чтения пина (true = отпущена / HIGH).
static bool s_stable = true;
static bool s_lastRead = true;
static uint32_t s_debAt = 0;

// Состояние жеста.
static bool s_pressed = false;     // debounced: кнопка удерживается
static uint32_t s_pressStart = 0;  // момент устойчивого нажатия
static bool s_dimming = false;     // идёт удержание-диммер
static int8_t s_dir = -1;          // направление СЛЕД. удержания (старт от 100% → вниз)
static float s_level = 100.0f;     // мастер-яркость 0..100 (дефолт full: анимации видны сразу)
static uint32_t s_lastRamp = 0;    // предыдущий тик рампы

// MANUAL трактуем как «все лампы включены»: база 100% на все каналы, дальше
// яркостью правит мастер-множитель (channels::setMasterPct) — лампы диммируются
// все вместе, как и в анимациях.
static void manualAllOn(uint32_t now_ms) {
  for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++) s_ch[i].setPercent(100, now_ms);
}

static void onClick(uint32_t now_ms) {
  const uint8_t next = (anim::mode() + 1) % anim::NUM_MODES;  // следующий по кругу
  anim::setMode(next, now_ms);
  if (next == anim::MANUAL) manualAllOn(now_ms);  // MANUAL = все лампы на мастер-яркости
}

void begin(channels::Channel* chans) {
  s_ch = chans;
  pinMode(PIN_BOOT, INPUT_PULLUP);
  s_stable = s_lastRead = (digitalRead(PIN_BOOT) == HIGH);
  channels::setMasterPct((uint8_t)(s_level + 0.5f));  // синхронизировать глобальный множитель
}

void tick(uint32_t now_ms) {
  if (!s_ch) return;

  // ── антидребезг (active-low: LOW = нажата) ──
  const bool raw = (digitalRead(PIN_BOOT) == HIGH);  // true = отпущена
  if (raw != s_lastRead) {
    s_lastRead = raw;
    s_debAt = now_ms;
  }
  if (now_ms - s_debAt >= DEBOUNCE_MS) s_stable = raw;
  const bool pressedNow = !s_stable;

  // ── фронт нажатия ──
  if (pressedNow && !s_pressed) {
    s_pressed = true;
    s_pressStart = now_ms;
  }

  // ── удержание -> плавный диммер мастер-яркости (во всех режимах) ──
  if (s_pressed && pressedNow) {
    if (!s_dimming && now_ms - s_pressStart >= LONG_PRESS_MS) {
      s_dimming = true;
      // У предела принудительно направляем внутрь диапазона (иначе удержание было бы
      // «пустым»); в середине сохраняем чередование с прошлого удержания.
      if (s_level >= 100.0f) s_dir = -1;
      else if (s_level <= 0.0f) s_dir = +1;
      if (anim::mode() == anim::MANUAL) manualAllOn(now_ms);  // чтобы было что диммировать
      s_lastRamp = now_ms;
    }
    if (s_dimming) {
      const uint32_t dt = now_ms - s_lastRamp;
      if (dt > 0) {
        s_lastRamp = now_ms;
        s_level += (float)s_dir * 100.0f * (float)dt / (float)DIM_FULL_MS;
        if (s_level < 0.0f) s_level = 0.0f;
        if (s_level > 100.0f) s_level = 100.0f;
        channels::setMasterPct((uint8_t)(s_level + 0.5f));
      }
      status::brightnessOverlay((uint8_t)(s_level + 0.5f), now_ms);  // индикация уровня
    }
  }

  // ── фронт отпускания ──
  if (!pressedNow && s_pressed) {
    s_pressed = false;
    if (s_dimming) {
      s_dimming = false;
      s_dir = -s_dir;  // следующее удержание — в обратную сторону
    } else {
      onClick(now_ms);  // короткое нажатие
    }
  }
}

}  // namespace button
