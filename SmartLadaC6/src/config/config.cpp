#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace config {

// Лампы: R10W 12V BA15S на всех 4 каналах (10 Вт). power_w — только для UI.
// ESP32-C6: пины boot-safe. Запрещены — strapping (GPIO4/5/8/9/15),
// нативный USB (GPIO12/13), SPI-флеш (GPIO24..30) и NeoPixel (GPIO8).
const ChannelDef CHANNEL_DEFS[channels::NUM_CHANNELS] = {
    {"stop", "Стоп", 10, 0},
    {"reverse", "Задний ход", 10, 1},
    {"turn", "Поворот", 10, 2},
    {"marker", "Габарит", 10, 3},
};

// Дефолты = снятая на стенде 2026-07-10 калибровка (R10W, Ecola 12В/80Вт).
// gamma расширена до 1.0..2.5 для подбора; на стенде выбрано 1.9.
static constexpr float GAMMA_MIN = 1.0f, GAMMA_MAX = 2.5f, GAMMA_DEFAULT = 1.9f;
// Рабочий диапазон PWM (аппаратно LEDC тянет до 78 кГц при 10 бит, но выше 30 кГц
// на этих лампах смысла нет — потолок сведён к 30 кГц).
static constexpr uint32_t FREQ_MIN = 10, FREQ_MAX = 30000, FREQ_DEFAULT = 30000;
static constexpr uint16_t SOFT_START_MAX = 5000, SOFT_START_DEFAULT = 30;
static constexpr uint16_t MIN_DUTY_DEFAULT = 20;  // порог свечения нити R10W
// Кап снят: на этом стенде 100% проверено безопасным (40 Вт из 80 Вт БП).
static constexpr uint8_t CAP_DEFAULT = 100;
// Тайминги тест-режимов анимации (мс): общий диапазон + дефолты (прежние константы).
static constexpr uint16_t ANIM_MIN = 100, ANIM_MAX = 10000;
static constexpr uint16_t CHASE_DEFAULT = 200, SEQ_DEFAULT = 400;
static constexpr uint16_t PULSE_DEFAULT = 2000, ALT_DEFAULT = 450;

void setDefaults(Config& cfg) {
  cfg.pwm_freq_hz = FREQ_DEFAULT;
  cfg.max_duty_cap_pct = CAP_DEFAULT;
  cfg.calib = {GAMMA_DEFAULT, MIN_DUTY_DEFAULT, channels::DUTY_MAX, SOFT_START_DEFAULT};
  for (uint8_t i = 0; i < channels::NUM_CHANNELS; i++)
    cfg.gpio[i] = CHANNEL_DEFS[i].default_gpio;
  cfg.chase_ms = CHASE_DEFAULT;
  cfg.seq_ms = SEQ_DEFAULT;
  cfg.pulse_ms = PULSE_DEFAULT;
  cfg.alt_ms = ALT_DEFAULT;
}

static float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static uint16_t clampu(uint32_t v, uint16_t lo, uint16_t hi) {
  return v < lo ? lo : (v > hi ? hi : (uint16_t)v);
}

void clamp(Config& cfg) {
  if (cfg.pwm_freq_hz < FREQ_MIN) cfg.pwm_freq_hz = FREQ_MIN;
  else if (cfg.pwm_freq_hz > FREQ_MAX) cfg.pwm_freq_hz = FREQ_MAX;
  if (cfg.max_duty_cap_pct > 100) cfg.max_duty_cap_pct = 100;
  channels::Calib& c = cfg.calib;
  c.gamma = clampf(c.gamma, GAMMA_MIN, GAMMA_MAX);
  c.min_duty = clampu(c.min_duty, 0, channels::DUTY_MAX);
  c.max_duty = clampu(c.max_duty, 0, channels::DUTY_MAX);
  if (c.min_duty > c.max_duty) c.min_duty = c.max_duty;
  c.soft_start_ms = clampu(c.soft_start_ms, 0, SOFT_START_MAX);
  cfg.chase_ms = clampu(cfg.chase_ms, ANIM_MIN, ANIM_MAX);
  cfg.seq_ms = clampu(cfg.seq_ms, ANIM_MIN, ANIM_MAX);
  cfg.pulse_ms = clampu(cfg.pulse_ms, ANIM_MIN, ANIM_MAX);
  cfg.alt_ms = clampu(cfg.alt_ms, ANIM_MIN, ANIM_MAX);
}

size_t toJson(const Config& cfg, char* buf, size_t buflen) {
  const channels::Calib& c = cfg.calib;
  int n = snprintf(buf, buflen,
                   "{\n  \"pwm_freq_hz\": %u,\n  \"max_duty_cap_pct\": %u,\n"
                   "  \"gamma\": %.2f,\n  \"min_duty\": %u,\n  \"max_duty\": %u,\n"
                   "  \"soft_start_ms\": %u,\n  \"chase_ms\": %u,\n  \"seq_ms\": %u,\n"
                   "  \"pulse_ms\": %u,\n  \"alt_ms\": %u\n}\n",
                   (unsigned)cfg.pwm_freq_hz, cfg.max_duty_cap_pct, (double)c.gamma,
                   c.min_duty, c.max_duty, c.soft_start_ms, cfg.chase_ms, cfg.seq_ms,
                   cfg.pulse_ms, cfg.alt_ms);
  if (n < 0 || (size_t)n >= buflen) return 0;
  return (size_t)n;
}

// Ищет "key": <число>. Минимальный парсер под нашу плоскую схему (все значения —
// числа). Возвращает false, если ключ не найден или после него не число.
static bool findNum(const char* json, const char* key, double& out) {
  char pat[24];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  size_t patlen = strlen(pat);
  const char* k = json;
  while ((k = strstr(k, pat)) != nullptr) {
    const char* c = k + patlen;
    while (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n') c++;
    if (*c == ':') {
      char* ep = nullptr;
      double v = strtod(c + 1, &ep);
      if (ep != c + 1) {
        out = v;
        return true;
      }
      return false;
    }
    k += patlen;
  }
  return false;
}

static double clampd(double v, double lo, double hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

bool fromJson(const char* json, Config& cfg) {
  if (!json || !*json) return false;
  int applied = 0;
  double v;
  if (findNum(json, "pwm_freq_hz", v)) {
    cfg.pwm_freq_hz = (uint32_t)clampd(v, FREQ_MIN, FREQ_MAX);
    applied++;
  }
  if (findNum(json, "max_duty_cap_pct", v)) {
    cfg.max_duty_cap_pct = (uint8_t)clampd(v, 0, 100);
    applied++;
  }
  channels::Calib& c = cfg.calib;
  if (findNum(json, "gamma", v)) {
    c.gamma = (float)clampd(v, GAMMA_MIN, GAMMA_MAX);
    applied++;
  }
  if (findNum(json, "min_duty", v)) {
    c.min_duty = (uint16_t)clampd(v, 0, channels::DUTY_MAX);
    applied++;
  }
  if (findNum(json, "max_duty", v)) {
    c.max_duty = (uint16_t)clampd(v, 0, channels::DUTY_MAX);
    applied++;
  }
  if (findNum(json, "soft_start_ms", v)) {
    c.soft_start_ms = (uint16_t)clampd(v, 0, SOFT_START_MAX);
    applied++;
  }
  if (findNum(json, "chase_ms", v)) {
    cfg.chase_ms = (uint16_t)clampd(v, ANIM_MIN, ANIM_MAX);
    applied++;
  }
  if (findNum(json, "seq_ms", v)) {
    cfg.seq_ms = (uint16_t)clampd(v, ANIM_MIN, ANIM_MAX);
    applied++;
  }
  if (findNum(json, "pulse_ms", v)) {
    cfg.pulse_ms = (uint16_t)clampd(v, ANIM_MIN, ANIM_MAX);
    applied++;
  }
  if (findNum(json, "alt_ms", v)) {
    cfg.alt_ms = (uint16_t)clampd(v, ANIM_MIN, ANIM_MAX);
    applied++;
  }
  clamp(cfg);
  return applied > 0;
}

}  // namespace config
