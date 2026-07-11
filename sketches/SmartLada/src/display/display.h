#pragma once
#include <stdint.h>

#include "../channels/channels.h"
#include "../config/config.h"

// display layer: onboard OLED SSD1306 128x64 on the HW-364A board
// (I2C: SDA=GPIO14, SCL=GPIO12, address 0x3C) + FLASH button (GPIO0)
// for switching pages. Don't hold the button on reset (enters the bootloader).
// [DISCARDED] Tied to the bench board; not ported to the C6.
//
// Pages: 1) status AP/heap/PWM/cap/loop-rate  2) channels: % and duty
//        3) calibration: gamma / min..max / soft_start.

namespace display {

// false — display not found (the bench keeps running without indication).
bool begin(const config::Config* cfg, const channels::Channel* chans);

// Call every loop(): button, periodic redraw, loop/s counter.
void tick(uint32_t now_ms);

}  // namespace display
