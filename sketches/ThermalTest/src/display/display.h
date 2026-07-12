#pragma once
#include <stdint.h>

// display layer: onboard SSD1306 128x64 on the HW-364A board (I2C SDA=GPIO14,
// SCL=GPIO12, addr 0x3C) + FLASH button (GPIO0) to cycle pages. Don't hold the
// button on reset (enters the bootloader). Test-only: dropped on the C6 port.
//
// Pages: 1) temps + clock  2) status (IP/clients/heap/log/clock)  3) sensor ROMs.

namespace display {

bool begin();               // false if the OLED is not found (bench keeps running)
void tick(uint32_t now_ms); // button + periodic redraw

}  // namespace display
