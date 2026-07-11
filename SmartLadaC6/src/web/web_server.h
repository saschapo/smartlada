#pragma once

#include "../channels/channels.h"
#include "../config/config.h"

// Слой web: AP + HTTP + UI + export/import (ESP32-C6, ядро arduino-esp32).
// Временный слой на этапе bring-up C6: даёт проверку 4 каналов без координатора.
// Финал управления — Zigbee; этот слой тогда заменяется.

namespace web {

// Вызывается после изменения глобальных параметров (перенастроить частоту PWM).
typedef void (*ApplyGlobalsFn)();

void begin(config::Config* cfg, channels::Channel* chans, ApplyGlobalsFn applyGlobals);
void handle();

}  // namespace web
