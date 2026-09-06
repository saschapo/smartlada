#pragma once
#include <Arduino.h>

// Event log on flash, so a session in the car can be handed over as a text file instead of a
// serial cable. Same shape as the cineink log it is modelled on: one line per event,
// "<uptime> <LEVEL> <TAG>: <message>", rotated so it cannot fill the partition.
//
// Lives on LittleFS in the "spiffs" partition of zigbee_8MB (1.4 MB). Every line also goes to
// Serial, so nothing is lost when the filesystem is unavailable -- begin() failing is not fatal.
//
// Download: GET /log (current), GET /log.1 (previous), POST /clearlog.
namespace evlog {

void   begin();                                 // mount the filesystem, open the log
void   write(char level, const char* tag, const char* fmt, ...);
size_t size();                                  // bytes in the current log
bool   ready();

}  // namespace evlog

#define LOGI(tag, ...) evlog::write('I', tag, __VA_ARGS__)
#define LOGW(tag, ...) evlog::write('W', tag, __VA_ARGS__)
#define LOGE(tag, ...) evlog::write('E', tag, __VA_ARGS__)
