#pragma once
#include <stdint.h>

#include "../channels/channels.h"

// status layer: indication on the onboard NeoPixel (WS2812, GPIO8). Replaces the
// ESP8266 bench's OLED. Logic:
//   - channel(s) active -> mode color / brightness overlay
//   - client on AP       -> steady green
//   - AP with no clients -> slow blue "breathing" (sign of life)

namespace status {

void begin(channels::Channel* chans);
void tick(uint32_t now_ms);

// Show a brightness level 0..100% on the NeoPixel for a short window (for button
// dimming). Takes priority over the normal indication while active.
void brightnessOverlay(uint8_t pct, uint32_t now_ms);

}  // namespace status
