#pragma once
#include <Arduino.h>

// Power / PD monitoring for the Rev C board. Read-only for now (diagnostics + Info
// screen); PD output gating (Phase 4) will grow here.
//
// Rails (verified from the PCB netlist): the USB-C VBUS pins ARE the +12V net, which
// feeds both the buck (U2 -> 3V3 for the ESP) and the lamp common (J7) + indicator LEDs.
// So a cold-boot inrush event on +12V can brown out the ESP itself.
//
//   VBUS sense = IO4  (ADC; divider R7 100k / R8 33k -> Vbus = Vpin * 133/33)
//   PD good    = IO10 (CH224K PG, open-drain, pull-up R9 -> 3V3; LOW = 12 V present)
namespace power {

void        begin();       // configure PG input (VBUS uses ADC, no pinMode needed)
uint16_t    vbusMv();      // measured VBUS in mV (after the divider)
bool        good();        // true when PD reports 12 V present (PG low)
const char* resetReason(); // short string for esp_reset_reason() (flags BROWNOUT)

// Output gating (Phase 4). present12V() is VBUS-based with hysteresis: it stays false only
// when the bus is clearly not 12 V (e.g. USB-5 V only), so it will not false-blank on a real
// 12 V supply. The loop zeroes the lamps when !present12V() unless forced (debug bypass to
// drive the FETs at 5 V for indicator-LED debugging without lamps).
bool        present12V();
bool        forced();
void        setForce(bool on);

}  // namespace power
