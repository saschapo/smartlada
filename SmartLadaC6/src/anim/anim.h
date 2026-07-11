#pragma once
#include <stdint.h>

#include "../channels/channels.h"

// Слой anim: движок динамических режимов для проверки быстрой смены PWM.
// Это технические тесты динамики (не «красивые» сцены). Когда режим != MANUAL,
// tick() сам ведёт проценты каналов; главный loop() дальше считает duty и пишет
// в LEDC как обычно (soft-start каналов сглаживает фронты). MANUAL = ручное
// управление слайдерами / master, движок каналы не трогает.

namespace anim {

enum Mode : uint8_t {
  MANUAL = 0,  // ручное управление (движок молчит)
  TURN,        // поворотник: мигает только канал turn
  CHASE,       // бегущий огонь: по одному каналу за раз
  SEQ,         // наполнение: каналы зажигаются по очереди и гаснут все разом
  PULSE,       // дыхание: синус яркости на всех каналах
  ALT,         // чётные/нечётные каналы попеременно
  DRIVE,       // имитация езды: сценарный автомат фаз (см. docs/TZ_drive_mode.md)
  NUM_MODES
};

// Короткие имена для UI/лога (латиница, индекс = Mode).
extern const char* const MODE_NAMES[NUM_MODES];

void begin(channels::Channel* chans);
void setMode(uint8_t mode, uint32_t now_ms);
uint8_t mode();

// Тайминги тест-режимов (мс), из config. Зовётся при старте и на POST /config.
// DRIVE использует собственные длительности фаз (не зависит от этих значений).
void setTimings(uint16_t chase_ms, uint16_t seq_ms, uint16_t pulse_ms, uint16_t alt_ms);

// Вызывать из loop() каждый цикл. В MANUAL — ничего не делает.
void tick(uint32_t now_ms);

}  // namespace anim
