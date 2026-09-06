#include "zigbee.h"
#ifndef ZIGBEE_MODE_ED
#error "SmartLadaRevC needs the Zigbee ED build (FQBN ...:ZigbeeMode=ed)"
#endif
#include <Zigbee.h>   // angle brackets: the library header, NOT this dir's zigbee.h
                      // (macOS case-insensitive FS would match our own header on quotes)
#include "../config/config.h"
#include "../fx/effects.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <Preferences.h>
#include "radio.h"

// Arduino-ESP32 3.3.10 uses this APS hook to maintain its binding list. Our wrapper
// must always chain it; returning early would bypass normal Arduino processing.
extern bool zb_apsde_data_indication_handler(esp_zb_apsde_data_ind_t ind);

namespace zb {

static constexpr uint8_t NUM_CH = 4;
static const char* LAMP_NAME[NUM_CH] = {"Turn", "Marker", "Reverse", "Stop"};  // ch0..3
static constexpr uint8_t FARA_EP   = 14;
static constexpr uint8_t SAT_THRESH = 40;   // saturation below this = white = no effect

// Yandex compatibility workaround, verified with unchanged H/S bytes: a color report
// can replace the chosen preset with an unnamed color. Suppress automatic H/S echoes
// BEFORE a Color Control command updates the attributes. Merely avoiding setLightColor()
// is insufficient: the Zigbee stack has its own configured attribute reporting.
// Attribute values and Read Attributes responses stay intact. Brightness, on/off and
// other endpoints retain normal reporting. Local effect changes do not publish a color.
static void stopColorReports() {
  for (uint16_t id = 0; id < 2; ++id) {  // CurrentHue, CurrentSaturation only
    esp_zb_zcl_attr_location_info_t location = {};
    location.endpoint_id = FARA_EP;
    location.cluster_id = ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL;
    location.cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE;
    location.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;
    location.attr_id = id;
    // Stack callback already holds the Zigbee lock. Do not acquire it again.
    const auto* reporting = esp_zb_zcl_find_reporting_info(location);
    if (reporting && reporting->direction == ESP_ZB_ZCL_REPORT_DIRECTION_SEND &&
        esp_zb_zcl_stop_attr_reporting(location) != ESP_OK)
      Serial.printf("EP14: could not stop color reporting attr=%04x\n", id);
  }
}

static bool onApsIndication(esp_zb_apsde_data_ind_t ind) {
  const bool color = (ind.status == 0 && ind.dst_endpoint == FARA_EP && ind.profile_id == 0x0104 &&
                      ind.cluster_id == ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL &&
                      ind.asdu && ind.asdu_length >= 3);
  // ZCL frame control: bits 0-1 frame type, bit 2 manufacturer-specific, bit 3 direction.
  const bool colorCmd = color && (ind.asdu[0] & 0x0b) == 0x01;   // cluster cmd, client -> server
  // Configure Reporting (general command 0x06) re-arms precisely what we stop, so stopping
  // before the stack handles it would be undone -- that frame is serviced on the way OUT.
  // asdu[2] is the command id only when no manufacturer code is present (bit 2 clear).
  const bool cfgReport = color && (ind.asdu[0] & 0x0f) == 0x00 && ind.asdu[2] == 0x06;

  if (colorCmd) stopColorReports();          // before the command updates the attributes
  bool handled = zb_apsde_data_indication_handler(ind);
  if (cfgReport) {
    stopColorReports();                      // after the coordinator's request was applied
    Serial.println("EP14: color reporting re-armed by the coordinator -> stopped again");
  }
  return handled;
}

// s_dirty is set from the Zigbee task and cleared from the loop -> volatile.
static volatile bool s_dirty = false;
static bool s_wasConnected = false;
// Set while WE push EP14's own attributes back to the coordinator (report-back). The Fara
// setters call our onFaraHsv callback SYNCHRONOUSLY on this (loop) task, so we swallow that
// echo. A genuine inbound command runs onFaraHsv on the Zigbee stack task instead -- record
// which task is reporting so an inbound command during the report window is NOT dropped (R5).
static volatile bool s_reporting = false;
static volatile TaskHandle_t s_reportTask = nullptr;

// Alice's single "move to hue and saturation" reaches the device as TWO attribute writes, and
// the library fires the HSV callback on each, filling the other field by re-reading the
// attribute table (ZigbeeColorDimmableLight.cpp). The first callback of the pair therefore
// carries a HALF-UPDATED color: a new hue against the old saturation, or a new saturation
// against the old hue. Acting on it selects the wrong effect for a few ms.
// So latch the callback and apply it once the pair has settled.
//
// The deadline is taken from the FIRST pending sample and is NOT pushed back by later ones:
// a slider drag streams level updates continuously, and a refreshing deadline would stall
// every update until the user let go. This way lag is bounded at SETTLE and the newest
// values always win. The mode change runs on the loop task. Immediate applyLevel() still
// writes brightness from the callback; this remains separate from the color-report fix.
static constexpr uint32_t HSV_SETTLE_MS = 150;
static portMUX_TYPE s_hsvMux = portMUX_INITIALIZER_UNLOCKED;
static bool     s_hsvPend  = false;
static uint32_t s_hsvAt    = 0;
static bool     s_hsvState = false;
static uint8_t  s_hsvH = 0, s_hsvS = 0, s_hsvV = 0;

// hue (0..254) -> effect mode 1..COUNT. Quantise the hue wheel across the effects so
// rotating the Fara color scrolls through them. (White is handled before this is called.)
static uint8_t hueToMode(uint8_t hue) {
  if (fx::COUNT == 0) return 0;
  uint8_t idx = (uint8_t)((uint16_t)hue * fx::COUNT / 256);
  if (idx >= fx::COUNT) idx = fx::COUNT - 1;
  return (uint8_t)(1 + idx);
}

// One lamp endpoint (EP10-13, also driven by the Yandex "Fara" group, which fans out
// on/off + level to each lamp). Runs on the Zigbee task; every field it writes in
// config::s is a single byte (atomic on RISC-V), so the race with the loop is benign.
// Lamp commands set lampOn / staticBri only -- they NEVER change mode; the effect-vs-static
// choice belongs solely to EP14 (Fara). Level does not force the lamp on (bug B1): a group
// brightness op blasts CurrentLevel to every EP incl. ones shown off; on/off is the OnOff
// cluster's job (turn-on sends OnOff=1, so a real "turn on" still enables the channel).
class LampEP : public ZigbeeDimmableLight {
public:
  LampEP(uint8_t ep, uint8_t ch) : ZigbeeDimmableLight(ep), _ch(ch) {}
  void zbAttributeSet(const esp_zb_zcl_set_attr_value_message_t* m) override {
    const uint16_t cl = m->info.cluster, aid = m->attribute.id;
    if (cl == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF && aid == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID
        && m->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
      if (*(bool*)m->attribute.data.value) config::s.lampOn |= (1 << _ch);
      else                                 config::s.lampOn &= ~(1 << _ch);
      s_dirty = true;
    } else if (cl == ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL
               && aid == ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID
               && m->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U8) {
      config::s.staticBri[_ch] = *(uint8_t*)m->attribute.data.value;
      s_dirty = true;
    }
    Serial.printf("[%lu] ZB lamp EP%u ch%u cl=0x%04x attr=0x%04x -> lampOn=0x%X bri=%u\n",
                  (unsigned long)millis(), 10 + _ch, _ch, cl, aid,
                  config::s.lampOn, config::s.staticBri[_ch]);
  }
private:
  uint8_t _ch;
};

static LampEP  lamp0(10, 0), lamp1(11, 1), lamp2(12, 2), lamp3(13, 3);
static LampEP* lamps[NUM_CH] = {&lamp0, &lamp1, &lamp2, &lamp3};
static ZigbeeColorDimmableLight fara(FARA_EP);

// Fara (EP14) is the EFFECT layer, and ONLY that:
//   off / white -> no effect (mode 0). Lamps keep whatever the group or the individual lamp
//                  endpoints set; EP14 does not fan its level out to them. A second group
//                  dimmer here duplicated EP10-13, flattened per-lamp levels on every color
//                  change, and read as a half-finished effect in the app.
//   color       -> effect: hue picks the effect, level = effect brightness (master)
// Group brightness and "set the turn signal to 50%" both stay with EP10-13, which already
// handle group fan-out and per-lamp addressing.
// Level only: goes straight through, no latch. The app streams level updates while a slider is
// dragged, and holding them for the settle window collapses a smooth drag into one step per
// window. EP14's level is ALWAYS the effect brightness -- it never touches per-lamp levels, so
// it can never resurrect a stale effect the way a level command used to.
static void applyLevel(uint8_t value) {
  config::s.master = value;
  s_dirty = true;
}

static void onFaraHsv(bool state, uint8_t hue, uint8_t sat, uint8_t value) {
  // Swallow only OUR own report-back echo: it fires on the same task that is reporting. An
  // inbound Alice command arrives on the Zigbee task and must be honored even mid-report (R5).
  if (s_reporting && s_reportTask == xTaskGetCurrentTaskHandle()) return;
  applyLevel(value);                                               // brightness: never latched
  portENTER_CRITICAL(&s_hsvMux);
  s_hsvState = state; s_hsvH = hue; s_hsvS = sat; s_hsvV = value;   // newest values win
  if (!s_hsvPend) { s_hsvAt = millis(); s_hsvPend = true; }         // deadline from the first
  portEXIT_CRITICAL(&s_hsvMux);
  Serial.printf("[%lu] ZB Fara rx on=%d hue=%u sat=%u val=%u (lvl now, hs latched)\n",
                (unsigned long)millis(), state, hue, sat, value);
}

// Apply the settled Fara color. Runs on the loop task from update().
static void applyFara(bool state, uint8_t hue, uint8_t sat, uint8_t value) {
  if (!state || sat < SAT_THRESH) {
    config::s.mode = 0;              // off and white both mean the same: no effect running
  } else {
    config::s.mode = hueToMode(hue);
    config::s.master = value;        // effect brightness
  }
  s_dirty = true;
  Serial.printf("[%lu] ZB Fara on=%d hue=%u sat=%u val=%u -> mode=%u master=%u sB0=%u\n",
                (unsigned long)millis(), state, hue, sat, value,
                config::s.mode, config::s.master, config::s.staticBri[0]);
}

// Installing the hook needs the Zigbee lock, and a timeout there used to leave the firmware
// running WITHOUT the workaround after one line on a serial port nobody reads -- i.e. the color
// bug quietly returns for that boot. Keep retrying from update() until it takes, and surface
// the state on the Statistics screen.
static bool     s_fixOn = false;
static uint32_t s_fixRetryAt = 0;
static bool     s_fixWarned = false;

static bool installColorFix() {
  if (s_fixOn) return true;
  if (!esp_zb_lock_acquire(pdMS_TO_TICKS(50))) {
    if (!s_fixWarned) {                       // warn once, then keep trying quietly
      s_fixWarned = true;
      Serial.println("EP14: color-report workaround not installed yet (lock busy), retrying");
    }
    return false;
  }
  esp_zb_aps_data_indication_handler_register(onApsIndication);
  esp_zb_lock_release();
  s_fixOn = true;
  Serial.println("EP14: Yandex color-report workaround enabled");
  return true;
}

bool colorFixActive() { return s_fixOn; }

// Set once begin() has actually brought the stack up. Everything that would call into the
// stack -- update(), the status screen, re-pair -- must check this, or a disabled boot
// would query a stack that was never started.
static bool s_live = false;
bool enabledOnThisBoot() { return s_live; }

bool enabledPref() { return radio::mode() == radio::ZIGBEE; }

void setEnabled(bool on) { radio::setMode(on ? radio::ZIGBEE : radio::WIFI); }

bool begin() {
  Zigbee.setDebugMode(true);                 // raw ZCL logging on Serial for bring-up
  for (uint8_t i = 0; i < NUM_CH; i++) {
    lamps[i]->setManufacturerAndModel("SmartLada", LAMP_NAME[i]);
    Zigbee.addEndpoint(lamps[i]);
  }
  fara.setLightColorCapabilities(ZIGBEE_COLOR_CAPABILITY_HUE_SATURATION);  // send hue, not XY
  fara.onLightChangeHsv(onFaraHsv);
  fara.setManufacturerAndModel("SmartLada", "Fara");
  Zigbee.addEndpoint(&fara);
  const bool ok = Zigbee.begin();             // ED mode (from the FQBN); join runs in background
  if (ok) { s_live = true; installColorFix(); }
  return ok;
}

// ---- report-back board -> coordinator (Phase 2): reflect local/effect/Fara changes in the
// app. Lamps (EP10-13) have no callback -> safe to push. Fara (EP14) DOES call onFaraHsv, so
// its push is wrapped in s_reporting to swallow the echo. Debounced on settle so a held button
// / stepped stream does not flood the network.
static constexpr uint32_t REPORT_DEBOUNCE_MS = 400;
struct StateSnap { uint8_t on, bri[NUM_CH], mode, master; };
static StateSnap s_seen = {0xFF, {0}, 0xFF, 0xFF}, s_reported = {0xFE, {0}, 0xFE, 0xFE};
static uint32_t  s_stableSince = 0;

static bool snapEq(const StateSnap& a, const StateSnap& b) {
  if (a.on != b.on || a.mode != b.mode || a.master != b.master) return false;
  for (uint8_t i = 0; i < NUM_CH; i++) if (a.bri[i] != b.bri[i]) return false;
  return true;
}

static void doReport(uint32_t nowMs) {
  // EP10-13: on/off + level (no callback registered -> cannot loop).
  for (uint8_t i = 0; i < NUM_CH; i++)
    lamps[i]->setLight((config::s.lampOn >> i) & 1, config::s.staticBri[i]);

  // EP14: report level/on-off only. onApsIndication also suppresses the stack's automatic
  // H/S echoes of incoming colors (Yandex preset replacement bug). Do not derive a color
  // from the effect index: that would move the selected hue to a band centre.
  // setLight*() still invokes onFaraHsv, so guard with s_reporting to ignore our own echo.
  s_reportTask = xTaskGetCurrentTaskHandle();
  s_reporting = true;
  if (config::s.mode != 0 && config::s.mode <= fx::COUNT) fara.setLightState(true);
  fara.setLightLevel(config::s.master);             // EP14 level == effect brightness, always
  s_reporting = false;
}

static void reportState(uint32_t nowMs) {
  if (!Zigbee.connected()) return;
  StateSnap cur;
  cur.on = config::s.lampOn; cur.mode = config::s.mode; cur.master = config::s.master;
  for (uint8_t i = 0; i < NUM_CH; i++) cur.bri[i] = config::s.staticBri[i];

  if (!snapEq(cur, s_seen)) { s_seen = cur; s_stableSince = nowMs; return; }  // still changing
  if (snapEq(cur, s_reported)) return;                                        // already reported
  if (nowMs - s_stableSince < REPORT_DEBOUNCE_MS) return;                     // wait for settle

  doReport(nowMs);
  s_reported = cur;
  Serial.printf("[%lu] ZB report lamps on=0x%X bri=%u,%u,%u,%u  fara mode=%u master=%u\n",
                (unsigned long)nowMs, cur.on, cur.bri[0], cur.bri[1], cur.bri[2], cur.bri[3],
                cur.mode, cur.master);
}

void update(uint32_t nowMs) {
  if (!s_fixOn && (int32_t)(nowMs - s_fixRetryAt) >= 0) {   // rate-limited so a busy lock
    s_fixRetryAt = nowMs + 500;                             // cannot stall the loop
    installColorFix();
  }
  // Apply a settled Fara color before reporting, so the report reflects the final pair.
  bool ready = false, st = false;
  uint8_t h = 0, sa = 0, v = 0;
  portENTER_CRITICAL(&s_hsvMux);
  // nowMs was sampled before menu processing. A newer callback timestamp must not wrap
  // an unsigned subtraction and appear 49 days old (observed premature settling in logs).
  if (s_hsvPend && (int32_t)(nowMs - s_hsvAt) >= (int32_t)HSV_SETTLE_MS) {
    st = s_hsvState; h = s_hsvH; sa = s_hsvS; v = s_hsvV;
    s_hsvPend = false; ready = true;
  }
  portEXIT_CRITICAL(&s_hsvMux);
  if (ready) {
    applyFara(st, h, sa, v);
  }
  bool conn = Zigbee.connected();
  if (conn != s_wasConnected) {
    s_wasConnected = conn; s_dirty = true;
  }
  reportState(nowMs);
}

bool connected()    { return s_live && Zigbee.connected(); }
bool consumeDirty() { bool d = s_dirty; s_dirty = false; return d; }
void factoryReset() { if (s_live) Zigbee.factoryReset(); }   // erases Zigbee NVS + reboots

uint16_t panId()     { return esp_zb_get_pan_id(); }
uint8_t  channel()   { return esp_zb_get_current_channel(); }
uint16_t shortAddr() { return esp_zb_get_short_address(); }

bool parentLink(uint8_t& lqi, int8_t& rssi, uint16_t& parentAddr) {
  if (!connected()) return false;
  bool found = false;
  if (esp_zb_lock_acquire(pdMS_TO_TICKS(10))) {     // short, non-stalling
    esp_zb_nwk_info_iterator_t it = ESP_ZB_NWK_INFO_ITERATOR_INIT;
    esp_zb_nwk_neighbor_info_t nbr;
    while (esp_zb_nwk_get_next_neighbor(&it, &nbr) == ESP_OK) {
      if (nbr.relationship == ESP_ZB_NWK_RELATIONSHIP_PARENT) {
        lqi = nbr.lqi; rssi = nbr.rssi; parentAddr = nbr.short_addr; found = true; break;
      }
    }
    esp_zb_lock_release();
  }
  return found;
}

}  // namespace zb
