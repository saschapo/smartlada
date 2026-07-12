#pragma once
#include <stdint.h>

#include "../thermal/thermal.h"

// stats layer: running min/max/avg per sensor since boot + a short ring buffer of
// recent samples (for the OLED history table). Fed from the main loop; reads the
// thermal layer directly (like display/). Only valid (present) readings count.

namespace stats {

static const uint8_t HISTORY_LEN = 5;   // rows kept for the history table
static const uint32_t HISTORY_INTERVAL_MS = 30000;

void begin();
void tick(uint32_t now_ms);   // samples stats (2 s) and pushes history (30 s)

float    minC(uint8_t idx);   // NaN until a valid reading is seen
float    maxC(uint8_t idx);
float    avgC(uint8_t idx);
uint32_t sampleCount();       // stat samples taken (any sensor present)

// History, newest first (i = 0). Returns false past the filled count.
uint8_t historyCount();
bool    history(uint8_t i, uint32_t& t_ms, float out[thermal::NUM_SENSORS]);

}  // namespace stats
