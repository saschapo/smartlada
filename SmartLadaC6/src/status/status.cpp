#include "status.h"

#include <Arduino.h>
#include <WiFi.h>
#include <math.h>

#include "../anim/anim.h"

namespace status {

static constexpr uint8_t NEOPIXEL_PIN = 8;  // onboard WS2812 on ESP32-C6
static constexpr uint8_t MAX_BRIGHT = 40;   // brightness ceiling (the LED is very bright)
static constexpr uint16_t UPDATE_MS = 17;   // frame update rate (~59 fps)

static constexpr uint16_t OVERLAY_HOLD_MS = 500;  // how long to hold the brightness overlay

static channels::Channel* s_chans = nullptr;  // in DRIVE we read channel percents
static uint32_t s_last = 0;
static uint8_t s_ovlPct = 0;      // brightness level for the overlay
static uint32_t s_ovlUntil = 0;   // until when the overlay is active

void begin(channels::Channel* chans) {
  s_chans = chans;
  neopixelWrite(NEOPIXEL_PIN, 0, 0, 0);  // start dark
}

void brightnessOverlay(uint8_t pct, uint32_t now_ms) {
  s_ovlPct = pct > 100 ? 100 : pct;
  s_ovlUntil = now_ms + OVERLAY_HOLD_MS;
}

// Base mode color at full MAX_BRIGHT (index = anim::Mode, except MANUAL).
static void modeColor(uint8_t m, uint8_t& r, uint8_t& g, uint8_t& b) {
  switch (m) {
    case anim::TURN:  r = MAX_BRIGHT; g = MAX_BRIGHT * 45 / 100; b = 0; break;  // amber
    case anim::CHASE: r = 0; g = MAX_BRIGHT; b = MAX_BRIGHT; break;             // cyan
    case anim::SEQ:   r = 0; g = MAX_BRIGHT; b = 0; break;                      // green
    case anim::PULSE: r = MAX_BRIGHT * 60 / 100; g = 0; b = MAX_BRIGHT; break;  // violet
    case anim::ALT:   r = MAX_BRIGHT; g = 0; b = 0; break;                      // red
    default:          r = g = b = 0; break;
  }
}

void tick(uint32_t now_ms) {
  if (now_ms - s_last < UPDATE_MS) return;
  s_last = now_ms;

  // Brightness overlay (button dimming) — takes priority over everything else.
  // Endpoints are color-coded: 0% — bright red, 100% — full green. The middle is
  // white with gamma correction: linear duty barely changes to the eye in the top
  // third (70..100 look "the same"), so we gamma-encode the scale (b ∝ x^2.2) to
  // make perceived brightness change evenly across the whole 0..100% range.
  if ((int32_t)(s_ovlUntil - now_ms) > 0) {
    if (s_ovlPct == 0) {
      neopixelWrite(NEOPIXEL_PIN, MAX_BRIGHT, 0, 0);  // 0% — bright red
    } else if (s_ovlPct >= 100) {
      neopixelWrite(NEOPIXEL_PIN, 0, MAX_BRIGHT, 0);  // 100% — full green
    } else {
      float f = powf((float)s_ovlPct / 100.0f, 2.2f);
      uint8_t b = (uint8_t)(f * MAX_BRIGHT + 0.5f);
      if (b == 0) b = 1;  // don't go dark at the low percents
      neopixelWrite(NEOPIXEL_PIN, b, b, b);  // white by gamma-corrected level
    }
    return;
  }

  const uint8_t m = anim::mode();

  if (m == anim::MANUAL) {
    // Network status: client — steady green, otherwise blue "breathing" (3 s)
    if (WiFi.softAPgetStationNum() > 0) {
      neopixelWrite(NEOPIXEL_PIN, 0, MAX_BRIGHT, 0);
    } else {
      uint16_t phase = now_ms % 3000;
      uint16_t tri = phase < 1500 ? phase : (3000 - phase);  // 0..1500..0
      uint8_t b = (uint8_t)((uint32_t)tri * MAX_BRIGHT / 1500);
      neopixelWrite(NEOPIXEL_PIN, 0, b / 2, b);
    }
    return;
  }

  if (m == anim::DRIVE) {
    // Full sync with the lamp: color = dominant signal (like in a car).
    // Priority: turn/hazard > reverse > stop > marker background.
    uint8_t stp = s_chans[0].percent(), rev = s_chans[1].percent();
    uint8_t trn = s_chans[2].percent(), mrk = s_chans[3].percent();
    if (trn > 0) {  // yellow (blinks in step with the lamp)
      neopixelWrite(NEOPIXEL_PIN, MAX_BRIGHT, MAX_BRIGHT * 80 / 100, 0);
    } else if (rev > 0) {  // white (reverse)
      neopixelWrite(NEOPIXEL_PIN, MAX_BRIGHT, MAX_BRIGHT, MAX_BRIGHT);
    } else if (stp > 0) {  // red (stop)
      neopixelWrite(NEOPIXEL_PIN, MAX_BRIGHT, 0, 0);
    } else if (mrk > 0) {  // driving with marker — steady dim white (below reverse-full)
      neopixelWrite(NEOPIXEL_PIN, MAX_BRIGHT / 3, MAX_BRIGHT / 3, MAX_BRIGHT / 3);
    } else {
      neopixelWrite(NEOPIXEL_PIN, 0, 0, 0);
    }
    return;
  }

  // Other animations — color by mode, brightness by pattern
  uint8_t r, g, b;
  modeColor(m, r, g, b);
  uint8_t br;  // 0..MAX_BRIGHT
  if (m == anim::PULSE) {
    uint16_t phase = now_ms % 2000;             // "breathing" in step with the lamps
    uint16_t tri = phase < 1000 ? phase : (2000 - phase);
    br = (uint8_t)((uint32_t)tri * MAX_BRIGHT / 1000);
  } else {
    br = ((now_ms / 250) % 2 == 0) ? MAX_BRIGHT : 0;  // blink ~2 Hz
  }
  neopixelWrite(NEOPIXEL_PIN, (uint8_t)((uint16_t)r * br / MAX_BRIGHT),
                (uint8_t)((uint16_t)g * br / MAX_BRIGHT),
                (uint8_t)((uint16_t)b * br / MAX_BRIGHT));
}

}  // namespace status
