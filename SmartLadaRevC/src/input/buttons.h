#pragma once
#include <Arduino.h>

// 4 buttons (Rev C, J4): K1..K4 = GPIO23,22,21,20, active LOW (INPUT_PULLUP).
// UI roles: UP/DOWN navigate & adjust, BACK exits, ENTER selects.
// repeat() adds an accelerating auto-repeat while UP/DOWN are held.
namespace buttons {

constexpr uint8_t N = 4;
enum { UP = 0, DOWN = 1, SEL = 2, BACK = 3 };     // K1(^), K2(v), K3(#)=select, K4(*)=back

void begin();
void poll(uint32_t now);

bool     pressed(uint8_t i);   // true only on the poll of the initial press edge
bool     repeat(uint8_t i);    // true on press edge and on accelerating repeats
uint32_t heldMs(uint8_t i);    // how long the button has been held (0 if up)
bool     down(uint8_t i);      // raw line level (LOW = pressed), pre-debounce; true even at boot

}  // namespace buttons
