#pragma once
#include <stdint.h>

class Print;

// thermal layer: 2x DS18B20 on ONE 1-Wire bus (GPIO13 / D7 on the HW-364A bench).
// 12-bit, non-blocking conversion (request -> wait ~750 ms -> read) on a 2 s cycle.
// Sensor index 0..1 maps to a fixed point label (psu/body). The ROM->slot map is a
// hard-coded table in thermal.cpp; while it is left unconfigured (all zero) sensors
// are taken in bus-discovery order so the bench works before the addresses are
// pasted in. Faults (disconnect / out of range) report present=false / NaN.
// (The third point — the ESP die — comes from the C6 internal sensor on the target,
// not from a DS18B20; room temp ~22.5 C serves as the ambient reference.)

namespace thermal {

static const uint8_t NUM_SENSORS = 2;
extern const char* const LABELS[NUM_SENSORS];  // "psu", "body"

void  begin();
void  tick(uint32_t now_ms);   // async state machine; call every loop()
float celsius(uint8_t idx);    // last good reading, or NaN if !present
bool  present(uint8_t idx);

// Copies the resolved 8-byte ROM of a slot; false if the slot has no sensor.
bool  getRom(uint8_t idx, uint8_t out[8]);

// Bring-up: list every device on the bus (ROM + current temp) to `out`.
// Blocking (used once at boot before the main loop). Paste the printed addresses
// into CONFIGURED[] in thermal.cpp to pin each physical sensor to its point.
void  dumpBus(Print& out);

}  // namespace thermal
