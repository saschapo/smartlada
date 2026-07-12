#include "log.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <stdarg.h>
#include <stdio.h>

#include "../rtc/rtc.h"

namespace applog {

#define LOG_PATH "/log.txt"
#define LOG_BAK_PATH "/log.bak.txt"
static const size_t LOG_MAX_BYTES = 512UL * 1024;

static bool s_fsOk = false;

// Prefix: wall-clock once the phone set the time, else seconds since boot.
static void prefix(char* out, size_t n) {
  if (rtc::isSet())
    rtc::format(out, n, false);
  else
    snprintf(out, n, "%lus", millis() / 1000UL);
}

void begin() {
  s_fsOk = LittleFS.begin();
  if (!s_fsOk) {                 // first boot / corrupt FS -> format then remount
    LittleFS.format();
    s_fsOk = LittleFS.begin();
  }
  char ts[24];
  rtc::format(ts, sizeof(ts), true);   // dashes on a cold boot until /api/time
  write('I', "SYS", "==== boot @ %s ====", ts);
}

void write(char level, const char* mod, const char* fmt, ...) {
  if (!s_fsOk) return;

  char msg[160];
  va_list args;
  va_start(args, fmt);
  vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);

  // Rotate the current log once it crosses the size limit.
  File chk = LittleFS.open(LOG_PATH, "r");
  if (chk) {
    if (chk.size() >= LOG_MAX_BYTES) {
      chk.close();
      LittleFS.remove(LOG_BAK_PATH);
      LittleFS.rename(LOG_PATH, LOG_BAK_PATH);
    } else {
      chk.close();
    }
  }

  char pfx[24];
  prefix(pfx, sizeof(pfx));
  File f = LittleFS.open(LOG_PATH, "a");
  if (f) {
    f.printf("%s %c %s: %s\n", pfx, level, mod, msg);
    f.close();
  }
}

void clear() {
  if (!s_fsOk) return;
  LittleFS.remove(LOG_PATH);
  LittleFS.remove(LOG_BAK_PATH);
  write('I', "SYS", "log cleared");
}

size_t size() {
  if (!s_fsOk) return 0;
  File f = LittleFS.open(LOG_PATH, "r");
  if (!f) return 0;
  size_t s = f.size();
  f.close();
  return s;
}

}  // namespace applog
