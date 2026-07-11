#pragma once
#include <stdint.h>

#include "../channels/channels.h"

// button layer: the onboard BOOT button (GPIO9) as a local remote.
//   - short press -> next anim mode, wrapping, including MANUAL (in MANUAL all
//     lamps turn on so they dim together). The change is visible via the NeoPixel
//     color in the status layer.
//   - hold        -> smooth master-brightness dimmer (channels::setMasterPct),
//     active in ALL modes including animations; speed 0..100% over DIM_FULL_MS.
//     Direction alternates from hold to hold (at a limit — back into range).
//     The level is visualized on the NeoPixel via status (gamma correction,
//     0=red, 100=green).
//
// The logic is taken from the cineink bench (debounce + long-press "while held"),
// but without an ISR: the project is single-threaded, polling from loop() is
// enough and needs no volatile/IRAM. GPIO9 is strapping only at boot; at runtime
// it is read as a regular input.

namespace button {

void begin(channels::Channel* chans);
void tick(uint32_t now_ms);

}  // namespace button
