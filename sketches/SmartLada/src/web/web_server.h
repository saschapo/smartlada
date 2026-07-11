#pragma once

#include "../channels/channels.h"
#include "../config/config.h"

// web layer: AP + HTTP + UI + export/import.
// [DISCARDED] On ESP32-C6 control goes through Zigbee.

namespace web {

// Called after global parameters change (e.g. to re-apply the PWM frequency).
typedef void (*ApplyGlobalsFn)();

void begin(config::Config* cfg, channels::Channel* chans, ApplyGlobalsFn applyGlobals);
void handle();

}  // namespace web
