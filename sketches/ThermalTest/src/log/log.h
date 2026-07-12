#pragma once
#include <stddef.h>

// log layer (ported from cineink, simplified for ESP8266): rolling event log on
// LittleFS, /log.txt, rotates at 512 KB -> /log.bak.txt (one backup kept).
// Single-core ESP8266 -> no mutex, no panic breadcrumbs.
//
// Line format: "<prefix> <LEVEL> <TAG>: <msg>", where <prefix> is the wall-clock
// "HH:MM:SS" once the phone has set the time (rtc::isSet()), else "<sec>s"
// (millis()/1000). Session anchor "==== boot @ <ts> ====" on begin(); a one-shot
// "clock set <ts>" is logged by the web layer when the clock is first seeded.

namespace applog {

void   begin();   // mount LittleFS (format if needed) + write the boot anchor
void   write(char level, const char* mod, const char* fmt, ...);
void   clear();   // wipe both files (web CLEAR LOG); logs a "log cleared" first line
size_t size();    // bytes of the current /log.txt (for status)

}  // namespace applog

#define LOGI(mod, ...) applog::write('I', mod, __VA_ARGS__)
#define LOGW(mod, ...) applog::write('W', mod, __VA_ARGS__)
#define LOGE(mod, ...) applog::write('E', mod, __VA_ARGS__)
