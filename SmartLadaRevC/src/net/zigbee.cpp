#include "zigbee.h"
#ifndef ZIGBEE_MODE_ED
#error "SmartLadaRevC needs the Zigbee ED build (FQBN ...:ZigbeeMode=ed)"
#endif
#include <Zigbee.h>   // angle brackets: the library header, NOT this dir's zigbee.h
                      // (macOS case-insensitive FS would match our own header on quotes)
#include "../config/config.h"
#include "../fx/effects.h"

namespace zb {

static constexpr uint8_t NUM_CH = 4;
static const char* LAMP_NAME[NUM_CH] = {"Turn", "Brake", "Marker", "Reverse"};
static constexpr uint8_t FARA_EP   = 14;
static constexpr uint8_t SAT_THRESH = 40;   // saturation below this = white = no effect

// s_dirty is set from the Zigbee task and cleared from the loop -> volatile.
static volatile bool s_dirty = false;
static bool s_wasConnected = false;

// Group operations (Yandex group brightness) blast a LEVEL command to every endpoint at
// once, so a lamp command arriving right after a Fara command is part of a group op, not a
// lone individual touch. Track the last Fara time to tell them apart. s_lastHue/Sat detect
// a real COLOR change (vs a level-only Fara update that must not re-select the effect).
static uint32_t s_lastFaraMs = 0;
static uint8_t  s_lastHue = 255, s_lastSat = 255;
static constexpr uint32_t GROUP_WINDOW_MS = 300;

// hue (0..254) -> fx mode. White / low saturation = Static (0); otherwise quantise the
// hue wheel across the available effects so rotating the color scrolls through them.
static uint8_t hueToMode(uint8_t hue, uint8_t sat) {
  if (sat < SAT_THRESH || fx::COUNT == 0) return 0;
  uint8_t idx = (uint8_t)((uint16_t)hue * fx::COUNT / 256);
  if (idx >= fx::COUNT) idx = fx::COUNT - 1;
  return (uint8_t)(1 + idx);
}

// One lamp endpoint. Runs on the Zigbee task; every field it writes in config::s is a
// single byte (atomic on RISC-V), so the race with the loop's read is benign.
class LampEP : public ZigbeeDimmableLight {
public:
  LampEP(uint8_t ep, uint8_t ch) : ZigbeeDimmableLight(ep), _ch(ch) {}
  void zbAttributeSet(const esp_zb_zcl_set_attr_value_message_t* m) override {
    const uint16_t cl = m->info.cluster, aid = m->attribute.id;
    // A LONE individual-lamp command (not part of a group op) drops out of an effect into
    // static: touching one lamp means the user wants direct per-channel control.
    bool lone = (millis() - s_lastFaraMs) > GROUP_WINDOW_MS;
    if (cl == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF && aid == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID
        && m->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
      if (*(bool*)m->attribute.data.value) config::s.lampOn |= (1 << _ch);
      else                                 config::s.lampOn &= ~(1 << _ch);
      if (lone) config::s.mode = 0;             // lone touch -> static; group op keeps the mode
      s_dirty = true;
    } else if (cl == ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL
               && aid == ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID
               && m->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U8) {
      config::s.staticBri[_ch] = *(uint8_t*)m->attribute.data.value;
      config::s.lampOn |= (1 << _ch);          // adjusting a level re-enables that channel
      if (lone) config::s.mode = 0;             // lone touch -> static; group op keeps the mode
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

// Fara: on/off = master power, level = master brightness, hue = effect selector.
static void onFaraHsv(bool state, uint8_t hue, uint8_t sat, uint8_t value) {
  s_lastFaraMs = millis();
  config::s.faraOn = state ? 1 : 0;
  config::s.master = value;
  // Re-select the effect ONLY on a real color change; a level-only update (group brightness
  // re-sending the same hue) must not flip the mode.
  bool nowWhite = sat < SAT_THRESH, wasWhite = s_lastSat < SAT_THRESH;
  if (hue != s_lastHue || nowWhite != wasWhite) config::s.mode = hueToMode(hue, sat);
  s_lastHue = hue; s_lastSat = sat;
  s_dirty = true;
  Serial.printf("[%lu] ZB Fara on=%d hue=%u sat=%u val=%u -> mode=%u master=%u\n",
                (unsigned long)millis(), state, hue, sat, value, config::s.mode, config::s.master);
}

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
  return Zigbee.begin();                      // ED mode (from the FQBN); join runs in background
}

void update(uint32_t) {
  bool now = Zigbee.connected();
  if (now != s_wasConnected) { s_wasConnected = now; s_dirty = true; }
}

bool connected()    { return Zigbee.connected(); }
bool consumeDirty() { bool d = s_dirty; s_dirty = false; return d; }
void factoryReset() { Zigbee.factoryReset(); }   // erases Zigbee NVS + reboots

}  // namespace zb
