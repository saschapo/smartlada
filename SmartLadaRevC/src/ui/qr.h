#pragma once
#include <stdint.h>
#include <stddef.h>

// QR painter for the Wi-Fi screen, over the ricmoo QR core (qrcode.{h,c}, MIT).
// The core is LOCKED to version 3 (LOCK_VERSION in qrcode.h) so its working buffer is a
// compile-time constant -- no heap churn on a long-running device. v3 / ECC_LOW holds up to
// 53 bytes in byte mode, which covers both payloads we draw: the Wi-Fi join string (~38 B)
// and "http://smartlada.local" (22 B).
#define QR_VERSION 3
#define QR_MODULES (4 * QR_VERSION + 17)   // 29 modules per side

namespace qr {

// Standard Wi-Fi join string, read natively by the iOS camera and Android:
//   WIFI:T:WPA;S:<ssid>;P:<pass>;;
// Characters \ ; , : " inside the SSID or password are backslash-escaped.
void joinString(char* out, size_t n, const char* ssid, const char* pass);

// Paint `text` centred on the OLED: a white card with a quiet border, dark modules on top.
// Returns false without drawing if the payload exceeds the locked version's capacity.
bool draw(const char* text, int16_t cx, int16_t cy, uint8_t px);

}  // namespace qr
