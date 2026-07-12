#pragma once
#include <stddef.h>
#include <stdint.h>

// rtc layer (ported from cineink): wall clock seeded by the phone via POST
// /api/time. The stored value is the phone's LOCAL epoch, so gmtime_r() yields
// local wall-clock without a timezone database. No battery RTC on this bench —
// the clock is lost on power cut and re-seeded on the next page load.

namespace rtc {

void   setLocalEpoch(uint32_t localEpoch);   // from POST /api/time
bool   isSet();                              // true once a real (>2021) clock is set
size_t format(char* out, size_t n, bool withDate);  // "HH:MM:SS" or "YYYY-MM-DD HH:MM:SS"

}  // namespace rtc
