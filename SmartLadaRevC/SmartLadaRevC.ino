// SmartLadaRevC -- lamp firmware for the SmartLada Rev C board (ESP32-C6-WROOM-1).
// Thin wiring layer; logic lives in src/*. OLED menu (4 buttons via J4), 4 PWM channels
// (OUT0..3), pluggable animation effects, and Zigbee control (4 lamp EPs + a "Fara"
// master/effect EP). Rev C pin remap (channels {1,0,2,3}, buttons {23,22,21,20}).
// PD power-good/VBUS-sense gating IS implemented (src/power): no ~12V -> outputs held off.
// Build (Zigbee ED + USB CDC on native USB):
//   arduino-cli compile --fqbn esp32:esp32:esp32c6:ZigbeeMode=ed,PartitionScheme=zigbee_8MB,CDCOnBoot=cdc,FlashSize=16M SmartLadaRevC

#include "src/version.h"
#include "src/config/config.h"
#include "src/channels/channels.h"
#include "src/input/buttons.h"
#include "src/display/display.h"
#include "src/fx/effects.h"
#include "src/ui/menu.h"
#include "src/power/power.h"
#include <Zigbee.h>            // pulls the Zigbee library into the build (ED sdkconfig)
#include "src/net/zigbee.h"
#include "src/net/bleota.h"    // BLE firmware-OTA (advertised ONLY in the dedicated OTA boot mode)

// RTC_NOINIT, not RTC_DATA: .rtc.data is a load segment, so the bootloader restores it from the
// image on every reset and only a deep-sleep wake preserves writes. .rtc_noinit is left alone,
// which is what "survive ESP.restart()" needs -- at the price of holding garbage after a power
// cycle, hence the magic word that validates the pair below.
static constexpr uint32_t RTC_MAGIC = 0x5ADAC601;
RTC_NOINIT_ATTR uint32_t g_rtcMagic;      // == RTC_MAGIC once the two below are known-good
RTC_NOINIT_ATTR uint32_t g_bootCount;     // survives resets (not power loss) -> spot reboot loops
RTC_NOINIT_ATTR uint32_t g_otaReq;        // menu sets bleota::OTA_REQ_MAGIC, then reboots
static bool g_otaMode = false;            // this boot is a BLE-OTA session (Zigbee not started)

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif

  channels::begin(20000);            // force lamp outputs low first (safety)
  power::begin();

  if (g_rtcMagic != RTC_MAGIC) {           // cold boot: .rtc_noinit holds garbage -> seed it
    g_rtcMagic = RTC_MAGIC; g_bootCount = 0; g_otaReq = 0;
  }
  g_bootCount++;
  if (g_otaReq == bleota::OTA_REQ_MAGIC) { g_otaReq = 0; g_otaMode = true; }

  bool haveOled = display::begin();
  config::load();
  display::setBrightness(config::s.dispBri);
  buttons::begin();

  // --- BLE-OTA mode: Zigbee is NOT started, so the C6 radio is free for a reliable
  // BLE transfer. Reached only via System -> Update Over BLE (which sets the RTC flag). ---
  if (g_otaMode) {
    menu::begin();
    if (haveOled) menu::showOtaIdle();
    bleota::begin("SmartLada");
    Serial.println("BLE OTA mode: Zigbee OFF, radio free for the transfer");
    return;                          // skip Zigbee, effects, menu nav, boot ramp
  }

  if (haveOled) display::splash("SmartLADA", HW_REV "  v" FW_VERSION);

  // Boot diagnostics. A cold boot with lamps on the +12V/VBUS rail can brown out the
  // ESP (shared rail); a climbing bootCount + reset="BROWNOUT" confirms that reboot loop.
  Serial.printf("[boot #%lu] reset=%s VBUS=%umV PD12V=%d dieTemp=%.1fC\n",
                (unsigned long)g_bootCount, power::resetReason(),
                power::vbusMv(), power::good() ? 1 : 0, temperatureRead());

  fx::loadParams();                  // effect timings from NVS (defaults if absent)
  channels::setFreq(config::s.pwmFreq);
  channels::setCalib(config::s.gammaX10, config::s.minLvl, config::s.maxLvl);
  channels::setSoftMs(config::s.softMs);
  menu::begin();
  if (!zb::begin()) Serial.println("Zigbee start failed; running local-only");

  Serial.printf("%s %s (%s) ready\n", FW_NAME, FW_VERSION, HW_REV);

  // Boot soft-start: ramp the lamps up gently across the splash window instead of the
  // main loop slamming them to the saved level in softMs. A slow PWM ramp warms the cold
  // filaments gradually, spreading inrush so the shared +12V/VBUS rail (which also feeds
  // the ESP's buck) does not sag into a brownout reboot. Boot ramp time == splash time.
  static constexpr uint16_t BOOT_SOFT_MS = 1500;
  channels::setSoftMs(BOOT_SOFT_MS);
  uint32_t t0 = millis();
  channels::resetSmoothing(t0);   // start the ramp clock HERE, not back in channels::begin() before
                                  // OLED/NVS/Zigbee init -- else the first write's dt = whole setup
                                  // latency and the "ramp" is really a jump (defeats inrush spreading)
  do {
    uint32_t now = millis();
    uint8_t out[4];
    fx::compute(config::s.mode, now, config::s.master,
                config::s.lampOn, config::s.staticBri, out);
    if (!power::present12V() && !power::forced())   // gate the boot ramp too: no 12 V -> stay dark
      channels::inhibit(now);                       // (else lamps flash on a 5 V-only USB before NO 12V)
    else                                            // immediate, like loop(): feeding a zeroed target
      channels::write(now, out);                    // through write() would ramp DOWN over BOOT_SOFT_MS
    delay(5);
  } while (millis() - t0 < BOOT_SOFT_MS);
  channels::setSoftMs(config::s.softMs);   // restore the user's soft-start for normal ops
}

void loop() {
  uint32_t now = millis();

  // BLE-OTA mode: Zigbee is off, lamps held dark. Show progress while flashing, an idle
  // banner otherwise, and let any button reboot back to normal (Zigbee) operation.
  if (g_otaMode) {
    buttons::poll(now);
    using namespace buttons;
    // A SEL held from the "Update Over BLE -> Yes" confirm is still down at boot; its first
    // debounced edge would instantly exit OTA. Arm the exit only after all keys have released,
    // then require a fresh press. (down() reads the raw line, unlike heldMs()'s post-debounce state.)
    static bool exitArmed = false;
    if (!exitArmed) {
      if (!down(UP) && !down(DOWN) && !down(SEL) && !down(BACK)) exitArmed = true;
    } else if (pressed(UP) || pressed(DOWN) || pressed(SEL) || pressed(BACK)) {
      esp_restart();
    }
    static uint32_t lastU = 0;
    if (bleota::active()) { if (now - lastU >= 200) { lastU = now; menu::showUpdating(bleota::progress()); } }
    else                  { if (now - lastU >= 500) { lastU = now; menu::showOtaIdle(); } }
    return;
  }

  buttons::poll(now);
  menu::update(now);
  zb::update(now);
  if (zb::consumeDirty()) menu::notifyExternalChange();   // Alice/Zigbee changed state

  // Effect brightness (EP14 "Fara" level) arrives from the app in coarse steps, and effect
  // mode deliberately keeps the channel smoothing short (FX_SMOOTH_MS below) so chase/drive
  // stay crisp -- which leaves every one of those steps visible as a jump. Static does not
  // show it: there the same stepped stream is eased by the user's soft-start tau. So ease the
  // MASTER with that tau instead: the brightness envelope is smooth while the effect keeps
  // its own dynamics. Same exponential form as channels::write().
  static float    masterSm = 0;
  static uint32_t masterMs = 0;
  {
    uint32_t dt = now - masterMs; masterMs = now;
    float tgt = (float)config::s.master;
    float a = (config::s.softMs == 0) ? 1.0f
                                      : (1.0f - expf(-(float)dt / (float)config::s.softMs));
    masterSm += (tgt - masterSm) * a;
    if (fabsf(tgt - masterSm) < 0.5f) masterSm = tgt;   // snap, no asymptotic crawl
  }

  uint8_t out[4];                    // lamp output runs every loop, independent of UI
  fx::compute(config::s.mode, now, (uint8_t)(masterSm + 0.5f),
              config::s.lampOn, config::s.staticBri, out);
  static constexpr uint16_t FX_SMOOTH_MS = 20;
  if (!power::present12V() && !power::forced()) {  // PD gating: no 12 V -> hold outputs off
    channels::inhibit(now);          // protective off is IMMEDIATE, not smoothed by the user's tau
  } else {                           // (debug "Force outputs" bypasses the gate)
    // Smoothing: heavy tau (config::softMs) is for Alice's stepped static dimming; effects
    // define their own dynamics and stay crisp (else a large tau smears chase/drive). After an
    // inhibit s_actual is 0, so supply return eases up from zero via tau.
    channels::setSoftMs(config::s.mode == 0 ? config::s.softMs : FX_SMOOTH_MS);
    channels::write(now, out);
  }

  menu::render();                    // redraws only when the UI changed
}
