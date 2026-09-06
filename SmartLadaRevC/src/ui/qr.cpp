#include "qr.h"
#include "qrcode.h"
#include "../display/display.h"
#include <string.h>

using display::oled;

namespace qr {

// v3 / ECC_LOW byte-mode capacity. Guarded explicitly so an over-long AP password fails
// visibly (nothing drawn) instead of overflowing the generator's buffer.
static constexpr size_t MAX_BYTES = 53;

static QRCode  s_code;
// qrcode_getBufferSize() is a runtime call; the locked version lets us size it statically.
#define QR_BUF_BYTES (((QR_MODULES * QR_MODULES) + 7) / 8)
static uint8_t s_buf[QR_BUF_BYTES];

void joinString(char* out, size_t n, const char* ssid, const char* pass) {
  size_t o = 0;
  auto put = [&](char c) { if (o + 1 < n) out[o++] = c; };
  auto puts_ = [&](const char* s) { while (*s) put(*s++); };
  auto escaped = [&](const char* s) {
    for (; *s; s++) { if (strchr("\\;,:\"", *s)) put('\\'); put(*s); }
  };
  puts_("WIFI:T:WPA;S:"); escaped(ssid);
  puts_(";P:");           escaped(pass);
  puts_(";;");
  if (n) out[o < n ? o : n - 1] = 0;
}

bool draw(const char* text, int16_t cx, int16_t cy, uint8_t px) {
  if (strlen(text) > MAX_BYTES) return false;
  if (qrcode_initText(&s_code, s_buf, QR_VERSION, ECC_LOW, text) != 0) return false;

  const int16_t side = (int16_t)s_code.size * px;
  const int16_t pad  = 3;                       // quiet border; the panel is only 64 px tall
  const int16_t x0 = cx - side / 2, y0 = cy - side / 2;
  oled.fillRect(x0 - pad, y0 - pad, side + 2 * pad, side + 2 * pad, SSD1306_WHITE);
  for (uint8_t my = 0; my < s_code.size; my++)
    for (uint8_t mx = 0; mx < s_code.size; mx++)
      if (qrcode_getModule(&s_code, mx, my))
        oled.fillRect(x0 + mx * px, y0 + my * px, px, px, SSD1306_BLACK);
  return true;
}

}  // namespace qr
