#include "rtc.h"

#include <string.h>
#include <sys/time.h>
#include <time.h>

namespace rtc {

// Past 2021-01-01 means a real wall clock was set (the chip boots near epoch 0),
// which cleanly distinguishes "set by a browser" from "unset".
static const time_t RTC_EPOCH_MIN = 1609459200;   // 2021-01-01 00:00:00

void setLocalEpoch(uint32_t localEpoch) {
  if ((time_t)localEpoch < RTC_EPOCH_MIN) return;   // ignore obviously bogus values
  struct timeval tv;
  tv.tv_sec = (time_t)localEpoch;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

bool isSet() { return time(nullptr) >= RTC_EPOCH_MIN; }

size_t format(char* out, size_t n, bool withDate) {
  if (!n) return 0;
  time_t t = time(nullptr);
  if (t < RTC_EPOCH_MIN) {
    strlcpy(out, withDate ? "-------/-- --:--:--" : "--:--:--", n);
    return strlen(out);
  }
  struct tm tmv;
  gmtime_r(&t, &tmv);   // stored value is LOCAL epoch -> gmtime gives local wall-clock
  return strftime(out, n, withDate ? "%Y-%m-%d %H:%M:%S" : "%H:%M:%S", &tmv);
}

}  // namespace rtc
