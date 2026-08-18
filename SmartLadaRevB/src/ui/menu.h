#pragma once
#include <Arduino.h>

// Menu / UI state machine. update() consumes button events and mutates settings;
// render() redraws only when something changed (keeps the panel static -> no
// tearing). The lamp animation runs independently of the display.
namespace menu {

void begin();
void update(uint32_t now);
void render();

}  // namespace menu
