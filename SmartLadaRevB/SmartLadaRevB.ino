// SmartLadaRevB -- lamp firmware for the SmartLada Rev B carrier (ESP32-C6).
// Thin wiring layer; logic lives in src/*. OLED menu (4 buttons), 4 PWM channels,
// pluggable animation effects. Build with USB CDC for Serial on native USB:
//   arduino-cli compile --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc SmartLadaRevB

#include "src/version.h"
#include "src/config/config.h"
#include "src/channels/channels.h"
#include "src/input/buttons.h"
#include "src/display/display.h"
#include "src/fx/effects.h"
#include "src/ui/menu.h"

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif

  channels::begin(20000);            // force lamp outputs low first (safety)

  bool haveOled = display::begin();
  if (haveOled) display::splash("SmartLADA", HW_REV "  v" FW_VERSION);

  config::load();
  fx::loadParams();                  // effect timings from NVS (defaults if absent)
  channels::setFreq(config::s.pwmFreq);
  channels::setCalib(config::s.gammaX10, config::s.minLvl, config::s.maxLvl);
  channels::setSoftMs(config::s.softMs);
  display::setBrightness(config::s.dispBri);
  buttons::begin();
  menu::begin();

  Serial.printf("%s %s (%s) ready\n", FW_NAME, FW_VERSION, HW_REV);
  if (haveOled) delay(3000);          // hold the boot splash ~3 s before the UI

}

void loop() {
  uint32_t now = millis();

  buttons::poll(now);
  menu::update(now);

  uint8_t out[4];                    // lamp output runs every loop, independent of UI
  fx::compute(config::s.mode, now, config::s.master, config::s.staticBri, out);
  channels::write(now, out);

  menu::render();                    // redraws only when the UI changed
}
