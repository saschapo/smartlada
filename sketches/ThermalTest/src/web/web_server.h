#pragma once

// web layer: SoftAP + HTTP for the ThermalTest bench. Serves the dark UI, the
// /api/state telemetry (temps + wall clock), the phone time-sync (/api/time) and
// the event-log download/clear endpoints. Ported to SmartLadaC6 almost verbatim.

namespace web {

void begin();
void handle();

}  // namespace web
