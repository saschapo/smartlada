#include "eventlog.h"
#include <LittleFS.h>
#include <stdarg.h>

namespace evlog {

static constexpr const char* CUR = "/log.txt";
static constexpr const char* BAK = "/log.bak.txt";
static constexpr size_t      ROTATE_AT = 192 * 1024;   // two of these fit the 1.4 MB partition

static bool s_ready = false;

bool ready() { return s_ready; }

void begin() {
  // Format on first use rather than fail: an unformatted partition is the normal state of a
  // fresh board, and a missing log must never stop the lamp from working.
  if (!LittleFS.begin(true)) { Serial.println("[log] LittleFS unavailable; serial only"); return; }
  s_ready = true;
  LOGI("boot", "---- log opened ----");
}

size_t size() {
  if (!s_ready) return 0;
  File f = LittleFS.open(CUR, "r");
  size_t n = f ? f.size() : 0;
  if (f) f.close();
  return n;
}

static void rotate() {
  LittleFS.remove(BAK);
  LittleFS.rename(CUR, BAK);
}

void write(char level, const char* tag, const char* fmt, ...) {
  char msg[192];
  va_list ap; va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  uint32_t ms = millis();
  char line[256];
  snprintf(line, sizeof(line), "%lu.%03lu %c %s: %s\n",
           (unsigned long)(ms / 1000), (unsigned long)(ms % 1000), level, tag, msg);
  Serial.print(line);
  if (!s_ready) return;

  if (size() > ROTATE_AT) rotate();
  File f = LittleFS.open(CUR, "a");
  if (!f) return;
  f.print(line);
  f.close();
}

}  // namespace evlog
