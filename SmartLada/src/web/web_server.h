#pragma once

#include "../channels/channels.h"
#include "../config/config.h"

// Слой web: AP + HTTP + UI + export/import.
// [ВЫБРАСЫВАЕТСЯ] На ESP32-C6 управление идёт через Zigbee.

namespace web {

// Вызывается после изменения глобальных параметров (перенастроить частоту PWM).
typedef void (*ApplyGlobalsFn)();

void begin(config::Config* cfg, channels::Channel* chans, ApplyGlobalsFn applyGlobals);
void handle();

}  // namespace web
