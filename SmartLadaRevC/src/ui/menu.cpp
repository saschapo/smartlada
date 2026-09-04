#include "menu.h"
#include "ui_font.h"
#include "../display/display.h"
#include "../config/config.h"
#include "../channels/channels.h"
#include "../fx/effects.h"
#include "../input/buttons.h"
#include "../net/zigbee.h"
#include "../version.h"
#include <Preferences.h>

using display::oled;

namespace menu {

enum Screen { SC_IDLE, SC_MENU, SC_MODE, SC_BRIGHT, SC_TIMINGS, SC_LAMP,
              SC_SETTINGS, SC_DISP, SC_NET, SC_QR, SC_INFO, SC_FACTORY };
static constexpr uint8_t NSCREEN = 12;

static Screen  screen = SC_IDLE;
static Screen  modeReturn = SC_MENU;     // where Mode returns to (set on entry)
static int8_t  cursor = 0;
static int8_t  screenCursor[NSCREEN] = {0};   // per-screen saved cursor (restored on return)
static bool    editing = false;
static bool    dirty = true;
static bool    pendingSave = false;      // flush config NVS on release
static bool    pendingFxSave = false;    // flush effect params NVS on release
static uint8_t factorySel = 0;           // Factory Reset confirm: 0 = No, 1 = Yes
static uint32_t s_now = 0;

// display power management (idle dim / off)
static uint32_t lastAct = 0;
static uint8_t  powerState = 0;          // 0 = on, 1 = dim, 2 = off

// carousel slide animation
static bool     animActive = false;
static uint32_t animStart = 0;
static int8_t   animDir = 0;
static constexpr uint32_t ANIM_MS = 180;
static constexpr int16_t  CY = 37, SLOT = 24, BAND_Y = 24, BAND_H = 24;  // pages 3..5

static const char* MAIN_ITEMS[] = {"Mode", "Brightness", "Timings", "Lamp Setup", "Settings"};
static constexpr uint8_t MAIN_N = sizeof(MAIN_ITEMS) / sizeof(MAIN_ITEMS[0]);
static const Screen MAIN_TARGET[MAIN_N] = {SC_MODE, SC_BRIGHT, SC_TIMINGS, SC_LAMP, SC_SETTINGS};

// Settings sub-menu (navigation)
static const char* SET_ITEMS[] = {"Display", "WiFi", "WiFi QR", "Factory Reset", "Info"};
static constexpr uint8_t SET_N = sizeof(SET_ITEMS) / sizeof(SET_ITEMS[0]);
static const Screen SET_TARGET[SET_N] = {SC_DISP, SC_NET, SC_QR, SC_FACTORY, SC_INFO};

static const uint8_t ARROW_UP[]   = {0x18, 0x3C, 0x7E, 0xFF};
static const uint8_t ARROW_DOWN[] = {0xFF, 0x7E, 0x3C, 0x18};

// ---------- adjust helpers ----------
// Acceleration: hold longer -> bigger step (rate is capped at ~1 frame, so speed
// past that comes from step size, keeping every jump visible).
static int accelMult() {
  uint32_t h = buttons::heldMs(buttons::UP), h2 = buttons::heldMs(buttons::DOWN);
  if (h2 > h) h = h2;
  if (h >= 3000) return 10;
  if (h >= 1200) return 3;
  return 1;
}
static void adjPct(uint8_t& v255, int dpct) {
  int p = (v255 * 100 + 127) / 255;
  p += dpct; if (p < 0) p = 0; if (p > 100) p = 100;
  v255 = (uint8_t)((p * 255 + 50) / 100);
}
static void adjInt(int32_t& v, int dir, int32_t lo, int32_t hi, int32_t step) {
  v += dir * step; if (v < lo) v = lo; if (v > hi) v = hi;
}
// off-after: 30s steps up to 1 min, then 1-min steps (0=off, 30s, 1m, 2m, ...)
static void adjOff(int32_t& v, int dir, int m) {
  for (int i = 0; i < m; i++) {
    if (dir > 0) v += (v < 60) ? 30 : 60;
    else         v -= (v <= 60) ? 30 : 60;
    if (v < 0) v = 0; if (v > 3600) v = 3600;
  }
}
static uint8_t pct(int32_t v) { return (uint8_t)((v * 100 + 127) / 255); }

// UP/DOWN adjust a 0..255 value in whole-percent steps; returns true if changed.
static bool adjPctBtn(uint8_t& v, int m) {
  bool ch = false;
  if (buttons::repeat(buttons::UP))   { adjPct(v, +m); ch = true; }
  if (buttons::repeat(buttons::DOWN)) { adjPct(v, -m); ch = true; }
  return ch;
}

static void fmtParam(const fx::Param& p, char* buf, size_t n) {
  if (p.type == fx::PT_TIME_MS)      snprintf(buf, n, "%ld.%lds", p.value / 1000, (p.value % 1000) / 100);
  else if (p.type == fx::PT_PERCENT) snprintf(buf, n, "%ld%%", (long)p.value);
  else                               snprintf(buf, n, "%ld", (long)p.value);
}

static void printCentered(const char* s, int16_t Y) {
  int16_t bx, by; uint16_t bw, bh;
  oled.getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
  oled.setCursor((128 - (int16_t)bw) / 2 - bx, Y - by - (int16_t)bh / 2);
  oled.print(s);
}
static void printRight(const char* s, int16_t xr, int16_t yBase) {
  int16_t bx, by; uint16_t bw, bh;
  oled.getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
  oled.setCursor(xr - (int16_t)bw - bx, yBase);
  oled.print(s);
}
static void header(const char* title) {
  oled.setFont(&dweep);
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(2, 9);
  oled.print(title);
  const char* leg = "#ok *back";
  int16_t bx, by; uint16_t bw, bh;
  oled.getTextBounds(leg, 0, 0, &bx, &by, &bw, &bh);
  oled.setCursor(126 - (int16_t)bw - bx, 9);
  oled.print(leg);
  oled.drawLine(0, 11, 127, 11, SSD1306_WHITE);
}
static void bar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t val) {
  oled.drawRect(x, y, w, h, SSD1306_WHITE);
  int16_t iw = (int16_t)((int32_t)(w - 2) * val / 255);
  if (iw > 0) oled.fillRect(x + 1, y + 1, iw, h - 2, SSD1306_WHITE);
}

// ---------- carousel ----------
static int16_t slotOffset(int8_t d) { return (d == -1) ? 4 : (d == 0) ? -2 : (d == 1) ? -5 : 0; }

static void carousel(const char* title, const char* const* items, uint8_t n, int8_t sel) {
  float p = 1.0f;
  if (animActive) {
    uint32_t dt = s_now - animStart;
    if (dt >= ANIM_MS) animActive = false;
    else { float t = (float)dt / ANIM_MS; p = t * t * (3 - 2 * t); }
  }
  int16_t yshift = animActive ? (int16_t)((1.0f - p) * SLOT * animDir) : 0;

  // Big (x2) items live INSIDE the white rectangle (magnifier): the incoming /
  // settled centre (d=0) and, while animating, the outgoing centre (d=-animDir).
  int8_t bigA = 0;
  int8_t bigB = animActive ? (int8_t)(-animDir) : 0;

  // pass 1: EVERY item at x1 -- this is what shows in the black zone (an item's
  // x1 form is what emerges once it leaves the rectangle).
  oled.setFont(&orp_medium); oled.setTextSize(1); oled.setTextColor(SSD1306_WHITE);
  for (int8_t d = -2; d <= 2; d++) {
    int16_t y = CY + d * SLOT + yshift + slotOffset(d);
    if (y < 2 || y > 66) continue;
    printCentered(items[(int8_t)(((sel + d) % n + n) % n)], y);
  }

  // pass 2: big (x2) items, clipped to the band via a page-aligned snapshot so
  // they never appear in the black zone (the x1<->x2 swap is hidden by the edge).
  static uint8_t scratch[1024];
  uint8_t* buf = oled.getBuffer();
  memcpy(scratch, buf, sizeof(scratch));
  oled.fillRect(0, BAND_Y, 128, BAND_H, SSD1306_BLACK);   // clear band for the x2 draw
  oled.setFont(&orp_medium); oled.setTextSize(2); oled.setTextColor(SSD1306_WHITE);
  for (int8_t d = -2; d <= 2; d++) {
    if (d != bigA && d != bigB) continue;
    int16_t y = CY + d * SLOT + yshift + slotOffset(0);   // centre baseline
    printCentered(items[(int8_t)(((sel + d) % n + n) % n)], y);
  }
  memcpy(buf + 0 * 128, scratch + 0 * 128, 3 * 128);      // restore pages 0..2 (above band)
  memcpy(buf + 6 * 128, scratch + 6 * 128, 2 * 128);      // restore pages 6..7 (below band)

  display::invertRect(0, BAND_Y, 128, BAND_H);   // band -> white, x2 text -> black
  oled.fillRect(0, 0, 128, 12, SSD1306_BLACK);   // clip above the top line
  header(title);
}

// ---------- screens ----------
static void renderIdle() {
  // "brightness" caption, then the arrows + % at the top-menu-item coordinates
  oled.setFont(&dweep);
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  printCentered("brightness", 5);

  char pcs[8]; snprintf(pcs, sizeof(pcs), "%u%%", pct(config::s.master));
  oled.setFont(&orp_medium);
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  int16_t bx, by; uint16_t bw, bh;
  oled.getTextBounds(pcs, 0, 0, &bx, &by, &bw, &bh);
  int16_t pcx = (128 - (int16_t)bw) / 2 - bx, pcy = CY - SLOT + slotOffset(-1);  // = top menu item y
  oled.setCursor(pcx, pcy - by - (int16_t)bh / 2);
  oled.print(pcs);
  oled.drawBitmap(pcx - 14, pcy - 2, ARROW_UP, 8, 4, SSD1306_WHITE);
  oled.drawBitmap(pcx + (int16_t)bw + 6, pcy - 2, ARROW_DOWN, 8, 4, SSD1306_WHITE);

  // fx name at the Mode-carousel selected coordinates (seamless *), with the
  // brightness fill on the band (name inverts at the fill boundary)
  oled.setFont(&orp_medium);
  oled.setTextSize(2);
  oled.setTextColor(SSD1306_WHITE);
  printCentered(fx::modeName(config::s.mode), CY + slotOffset(0));
  oled.drawRect(0, BAND_Y, 128, BAND_H, SSD1306_WHITE);
  int16_t fillW = (int16_t)((int32_t)config::s.master * 126 / 255);
  if (fillW > 0) display::invertRect(1, BAND_Y + 1, fillW, BAND_H - 2);

  oled.setFont(&dweep);
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(2, 62);
  oled.print("#:MENU");
  printRight("*:FX", 126, 62);
}

// one list row: bold when selected, inverted when editing (replaces the frame)
static void row(int16_t y, bool sel, const char* label, const char* value,
                bool hasBar, uint8_t barVal) {
  oled.setFont(sel ? &orp_bold : &orp_medium);
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(2, y + 8);
  oled.print(label);
  if (hasBar) bar(44, y, 56, 9, barVal);
  oled.setFont(&dweep);
  oled.setTextColor(SSD1306_WHITE);
  printRight(value, 126, y + 7);
  if (sel && editing) display::invertRect(0, y - 1, 128, 11);
}

static void renderBright() {
  header("Brightness");
  if (config::s.mode != 0) {                       // animation: one common bar (= master)
    bar(8, 23, 112, 16, config::s.master);
    oled.setFont(&orp_medium);
    oled.setTextSize(2);
    oled.setTextColor(SSD1306_WHITE);
    char b[8]; snprintf(b, sizeof(b), "%u%%", pct(config::s.master));
    printCentered(b, 52);
    return;
  }
  const char* lab[5] = {"CH1", "CH2", "CH3", "CH4", "Master"};
  uint8_t val[5] = {config::s.staticBri[0], config::s.staticBri[1],
                    config::s.staticBri[2], config::s.staticBri[3], config::s.master};
  for (uint8_t i = 0; i < 5; i++) {
    char v[6]; snprintf(v, sizeof(v), "%u", pct(val[i]));
    row(14 + i * 10, i == cursor, lab[i], v, true, val[i]);
  }
}

static void renderTimings() {
  header("Timings");
  if (config::s.mode == 0) {
    oled.setFont(&orp_medium);
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    printCentered("No Timings (Static)", 36);
    return;
  }
  const fx::Effect& e = fx::EFFECTS[config::s.mode - 1];
  for (uint8_t i = 0; i < e.nparams; i++) {
    const fx::Param& p = e.params[i];
    uint8_t fill = (p.max > p.min)
        ? (uint8_t)((int32_t)(p.value - p.min) * 255 / (p.max - p.min)) : 0;
    char v[16]; fmtParam(p, v, sizeof(v));
    row(14 + i * 12, i == cursor, p.name, v, true, fill);
  }
  // action row: Reset To Defaults (inverted when selected)
  int16_t y = 14 + e.nparams * 12;
  bool sel = (cursor == e.nparams);
  oled.setFont(sel ? &orp_bold : &orp_medium);
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(4, y + 8);
  oled.print("Reset To Defaults");
  if (sel) display::invertRect(0, y - 1, 128, 11);
}

static void renderLamp() {
  header("Lamp Setup");
  const char* lab[5] = {"Gamma", "Soft Start", "Min Level", "Max Level", "PWM Freq"};
  for (uint8_t i = 0; i < 5; i++) {
    char v[10];
    switch (i) {
      case 0:  snprintf(v, sizeof(v), "%u.%u", config::s.gammaX10 / 10, config::s.gammaX10 % 10); break;
      case 1:  snprintf(v, sizeof(v), "%ums", config::s.softMs); break;
      case 2:  snprintf(v, sizeof(v), "%u%%", config::s.minLvl); break;
      case 3:  snprintf(v, sizeof(v), "%u%%", config::s.maxLvl); break;
      default:
        if (config::s.pwmFreq >= 1000)
          snprintf(v, sizeof(v), "%u.%ukHz", config::s.pwmFreq / 1000, (config::s.pwmFreq % 1000) / 100);
        else
          snprintf(v, sizeof(v), "%uHz", config::s.pwmFreq);
    }
    row(14 + i * 10, i == cursor, lab[i], v, false, 0);
  }
}

static void renderSettings() {
  header("Settings");
  for (uint8_t i = 0; i < SET_N; i++) {
    int16_t y = 14 + i * 10;
    bool sel = (i == cursor);
    oled.setFont(sel ? &orp_bold : &orp_medium);
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(4, y + 8);
    oled.print(SET_ITEMS[i]);
    if (sel) display::invertRect(0, y - 1, 128, 11);
  }
}

static void renderFactory() {
  header("Factory Reset");
  oled.setFont(&orp_medium);
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  printCentered("Erase all settings?", 26);
  oled.setFont(&orp_bold);
  // No
  if (factorySel == 0) oled.fillRect(24, 44, 30, 15, SSD1306_WHITE);
  oled.setTextColor(factorySel == 0 ? SSD1306_BLACK : SSD1306_WHITE);
  oled.setCursor(31, 55); oled.print("No");
  // Yes
  if (factorySel == 1) oled.fillRect(74, 44, 34, 15, SSD1306_WHITE);
  oled.setTextColor(factorySel == 1 ? SSD1306_BLACK : SSD1306_WHITE);
  oled.setCursor(80, 55); oled.print("Yes");
}

static void renderDisplay() {
  header("Display");
  const char* lab[3] = {"Brightness", "Dim After", "Off After"};
  for (uint8_t i = 0; i < 3; i++) {
    char v[10];
    if (i == 0)       snprintf(v, sizeof(v), "%u%%", pct(config::s.dispBri));
    else if (i == 1)  { if (config::s.dimSec) snprintf(v, sizeof(v), "%us", config::s.dimSec); else strcpy(v, "off"); }
    else {
      uint16_t os = config::s.offSec;
      if (!os)            strcpy(v, "off");
      else if (os < 60)   snprintf(v, sizeof(v), "%us", os);        // 30s
      else if (os % 60)   snprintf(v, sizeof(v), "%u.5m", os / 60); // e.g. 90s -> 1.5m
      else                snprintf(v, sizeof(v), "%um", os / 60);
    }
    row(15 + i * 13, i == cursor, lab[i], v, false, 0);   // no bars in Display
  }
}

static void renderStub(const char* t, const char* msg) {
  header(t);
  oled.setFont(&orp_medium);
  oled.setTextColor(SSD1306_WHITE);
  printCentered(msg, 36);
}

void render() {
  if (!dirty) return;
  dirty = false;
  oled.clearDisplay();
  oled.setTextSize(1);              // every screen starts at x1 unless it opts into x2
  switch (screen) {
    case SC_IDLE:   renderIdle(); break;
    case SC_MENU:   carousel("MENU", MAIN_ITEMS, MAIN_N, cursor); break;
    case SC_MODE: {
      static constexpr uint8_t MODE_MAX = 1 + 8;    // Static + up to 8 effects
      static const char* names[MODE_MAX];
      uint8_t n = fx::modeCount();
      if (n > MODE_MAX) n = MODE_MAX;
      for (uint8_t i = 0; i < n; i++) names[i] = fx::modeName(i);
      carousel("MODE", names, n, cursor);
      break;
    }
    case SC_BRIGHT:   renderBright(); break;
    case SC_TIMINGS:  renderTimings(); break;
    case SC_LAMP:     renderLamp(); break;
    case SC_SETTINGS: renderSettings(); break;
    case SC_DISP:     renderDisplay(); break;
    case SC_NET:      renderStub("WiFi", "SoftAP: TODO"); break;
    case SC_QR:       renderStub("WiFi QR", "AP QR: TODO"); break;
    case SC_INFO:     renderStub("Info", HW_REV "  fw " FW_VERSION); break;
    case SC_FACTORY:  renderFactory(); break;
  }
  oled.setFont(nullptr);
  oled.display();
  if (animActive) dirty = true;
}

// ---------- power management ----------
static void applyPower(uint8_t st) {
  powerState = st;
  if (st == 0)      { display::power(true);  display::setBrightness(config::s.dispBri); dirty = true; }
  else if (st == 1) { display::power(true);  display::setBrightness(3); }   // ~1%
  else              { display::power(false); }
}
static void updatePower(uint32_t now) {
  uint32_t idle = now - lastAct;
  uint8_t want = 0;
  if (config::s.offSec && idle >= (uint32_t)config::s.offSec * 1000UL)       want = 2;
  else if (config::s.dimSec && idle >= (uint32_t)config::s.dimSec * 1000UL)  want = 1;
  if (want != powerState) applyPower(want);
}

// ---------- navigation ----------
static void go(Screen s) {
  screenCursor[screen] = cursor;
  screen = s;
  cursor = screenCursor[s];
  editing = false;
  animActive = false;
  if (s == SC_MODE) cursor = config::s.mode;
  if (s == SC_FACTORY) factorySel = 0;
  dirty = true;
}
static void slide(int8_t dir, uint8_t n) {
  cursor = (int8_t)(((cursor + dir) % n + n) % n);
  animActive = true; animStart = s_now; animDir = dir;
  dirty = true;
}

void update(uint32_t now) {
  using namespace buttons;
  s_now = now;
  bool up = pressed(UP), down = pressed(DOWN), ok = pressed(SEL), back = pressed(BACK);
  bool act = up || down || ok || back || heldMs(UP) || heldMs(DOWN) || heldMs(SEL) || heldMs(BACK);
  if (act) lastAct = now;

  // when the panel is OFF, the first press only wakes it (consumed). When merely
  // dimmed the UI is still visible, so let the press act normally (updatePower
  // below restores full brightness the same frame).
  if ((up || down || ok || back) && powerState == 2) { applyPower(0); return; }

  int m = accelMult();

  switch (screen) {
    case SC_IDLE:
      if (adjPctBtn(config::s.master, m)) { pendingSave = true; dirty = true; }
      if (ok)   go(SC_MENU);
      if (back) { modeReturn = SC_IDLE; go(SC_MODE); }   // *:FX (returns to idle)
      break;

    case SC_MENU:
      if (up)   slide(-1, MAIN_N);
      if (down) slide(+1, MAIN_N);
      if (ok)   { if (MAIN_TARGET[cursor] == SC_MODE) modeReturn = SC_MENU; go(MAIN_TARGET[cursor]); }
      if (back) go(SC_IDLE);
      break;

    case SC_MODE: {
      uint8_t n = fx::modeCount();
      if (up)   slide(-1, n);
      if (down) slide(+1, n);
      if (ok)   { config::s.mode = cursor; config::save(); go(modeReturn); }
      if (back) go(modeReturn);
      break;
    }

    case SC_BRIGHT:
      if (config::s.mode != 0) {
        if (adjPctBtn(config::s.master, m)) { pendingSave = true; dirty = true; }
        if (back || ok) go(SC_MENU);
      } else if (!editing) {
        if (up)   { cursor = (cursor + 5 - 1) % 5; dirty = true; }
        if (down) { cursor = (cursor + 1) % 5; dirty = true; }
        if (ok)   { editing = true; dirty = true; }
        if (back) go(SC_MENU);
      } else {
        uint8_t* tgt = (cursor < 4) ? &config::s.staticBri[cursor] : &config::s.master;
        if (adjPctBtn(*tgt, m)) { pendingSave = true; dirty = true; }
        if (ok || back) { editing = false; dirty = true; }
      }
      break;

    case SC_TIMINGS: {
      if (config::s.mode == 0) { if (back || ok) go(SC_MENU); break; }
      fx::Effect& e = fx::EFFECTS[config::s.mode - 1];
      uint8_t nItems = e.nparams + 1;              // params + "Reset To Defaults"
      if (cursor >= nItems) cursor = 0;
      if (!editing) {
        if (up)   { cursor = (cursor + nItems - 1) % nItems; dirty = true; }
        if (down) { cursor = (cursor + 1) % nItems; dirty = true; }
        if (ok) {
          if (cursor == e.nparams) { fx::resetParams(config::s.mode); fx::saveParams(); dirty = true; }
          else                     { editing = true; dirty = true; }
        }
        if (back) go(SC_MENU);
      } else {
        fx::Param& p = e.params[cursor];
        int32_t v = p.value;
        if (repeat(UP))   { adjInt(v, +1, p.min, p.max, p.step * m); p.value = v; pendingFxSave = true; dirty = true; }
        if (repeat(DOWN)) { adjInt(v, -1, p.min, p.max, p.step * m); p.value = v; pendingFxSave = true; dirty = true; }
        if (ok || back) { editing = false; dirty = true; }
      }
      break;
    }

    case SC_LAMP:
      if (!editing) {
        if (up)   { cursor = (cursor + 5 - 1) % 5; dirty = true; }
        if (down) { cursor = (cursor + 1) % 5; dirty = true; }
        if (ok)   { editing = true; dirty = true; }
        if (back) go(SC_MENU);
      } else {
        bool ch = false; int32_t v = 0;
        switch (cursor) {
          case 0: v = config::s.gammaX10;
            if (repeat(UP))   { adjInt(v, +1, 10, 30, 1 * m); ch = true; }
            if (repeat(DOWN)) { adjInt(v, -1, 10, 30, 1 * m); ch = true; }
            if (ch) { config::s.gammaX10 = v; channels::setCalib(config::s.gammaX10, config::s.minLvl, config::s.maxLvl); }
            break;
          case 1: v = config::s.softMs;
            if (repeat(UP))   { adjInt(v, +1, 0, 3000, 50 * m); ch = true; }
            if (repeat(DOWN)) { adjInt(v, -1, 0, 3000, 50 * m); ch = true; }
            if (ch) { config::s.softMs = v; channels::setSoftMs(config::s.softMs); }
            break;
          case 2: v = config::s.minLvl;
            if (repeat(UP))   { adjInt(v, +1, 0, 50, 1 * m); ch = true; }
            if (repeat(DOWN)) { adjInt(v, -1, 0, 50, 1 * m); ch = true; }
            if (ch) { config::s.minLvl = v; channels::setCalib(config::s.gammaX10, config::s.minLvl, config::s.maxLvl); }
            break;
          case 3: v = config::s.maxLvl;
            if (repeat(UP))   { adjInt(v, +1, 50, 100, 1 * m); ch = true; }
            if (repeat(DOWN)) { adjInt(v, -1, 50, 100, 1 * m); ch = true; }
            if (ch) { config::s.maxLvl = v; channels::setCalib(config::s.gammaX10, config::s.minLvl, config::s.maxLvl); }
            break;
          default: v = config::s.pwmFreq;          // PWM Freq applied on exit (re-attach is heavy)
            if (repeat(UP))   { adjInt(v, +1, 100, 30000, 500 * m); ch = true; }
            if (repeat(DOWN)) { adjInt(v, -1, 100, 30000, 500 * m); ch = true; }
            if (ch) config::s.pwmFreq = v;
            break;
        }
        if (ch) { pendingSave = true; dirty = true; }
        if (ok || back) {
          if (cursor == 4) channels::setFreq(config::s.pwmFreq);
          editing = false; dirty = true;
        }
      }
      break;

    case SC_SETTINGS:
      if (up)   { cursor = (cursor + SET_N - 1) % SET_N; dirty = true; }
      if (down) { cursor = (cursor + 1) % SET_N; dirty = true; }
      if (ok)   go(SET_TARGET[cursor]);
      if (back) go(SC_MENU);
      break;

    case SC_FACTORY:
      if (up || down) { factorySel ^= 1; dirty = true; }
      if (back) go(SC_SETTINGS);
      if (ok) {
        if (factorySel == 1) {                     // Yes -> wipe config + leave Zigbee, reboot
          Preferences pr; pr.begin("smartlada", false); pr.clear(); pr.end();
          delay(50);
          zb::factoryReset();                      // erases Zigbee NVS + reboots
          ESP.restart();                           // fallback if factoryReset() returned
        } else go(SC_SETTINGS);
      }
      break;

    case SC_DISP:
      if (!editing) {
        if (up)   { cursor = (cursor + 3 - 1) % 3; dirty = true; }
        if (down) { cursor = (cursor + 1) % 3; dirty = true; }
        if (ok)   { editing = true; dirty = true; }
        if (back) go(SC_SETTINGS);
      } else if (cursor == 0) {
        if (adjPctBtn(config::s.dispBri, m)) {
          if (config::s.dispBri < 3) config::s.dispBri = 3;   // display brightness >= ~1%
          display::setBrightness(config::s.dispBri);
          pendingSave = true; dirty = true;
        }
        if (ok || back) { editing = false; dirty = true; }
      } else if (cursor == 1) {
        int32_t v = config::s.dimSec;
        if (repeat(UP))   { adjInt(v, +1, 0, 120, 5 * m); config::s.dimSec = v; pendingSave = true; dirty = true; }
        if (repeat(DOWN)) { adjInt(v, -1, 0, 120, 5 * m); config::s.dimSec = v; pendingSave = true; dirty = true; }
        if (ok || back) { editing = false; dirty = true; }
      } else {
        int32_t v = config::s.offSec;
        if (repeat(UP))   { adjOff(v, +1, m); config::s.offSec = v; pendingSave = true; dirty = true; }
        if (repeat(DOWN)) { adjOff(v, -1, m); config::s.offSec = v; pendingSave = true; dirty = true; }
        if (ok || back) { editing = false; dirty = true; }
      }
      break;

    case SC_NET: case SC_INFO: case SC_QR:
      if (back || ok) go(SC_SETTINGS);
      break;
  }

  bool released = (heldMs(UP) == 0 && heldMs(DOWN) == 0);
  if (pendingSave && released)   { config::save();   pendingSave = false; }
  if (pendingFxSave && released) { fx::saveParams(); pendingFxSave = false; }
  updatePower(now);
}

void begin() { screen = SC_IDLE; cursor = 0; dirty = true; lastAct = millis(); powerState = 0; }

void notifyExternalChange() { dirty = true; }   // Zigbee/network wrote config::s -> redraw

}  // namespace menu
