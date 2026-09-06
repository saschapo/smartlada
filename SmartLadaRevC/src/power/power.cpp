#include "power.h"
#include <esp_system.h>

namespace power {

static constexpr uint8_t PIN_VBUS = 4;    // ADC divider midpoint
static constexpr uint8_t PIN_PG   = 10;   // CH224K power-good, active-LOW

void begin() {
  pinMode(PIN_PG, INPUT);                 // external pull-up R9 -> 3V3
}

uint16_t vbusMv() {
  uint32_t pin = analogReadMilliVolts(PIN_VBUS);   // calibrated mV at the pin
  return (uint16_t)(pin * 133 / 33);               // undo R7 100k / R8 33k divider
}

bool good() { return digitalRead(PIN_PG) == LOW; }

static bool s_present = true;    // assume present until first read (avoid a boot blank)
static bool s_force   = false;

bool present12V() {
  uint16_t mv = vbusMv();
  if (s_present && mv < 7000)       s_present = false;   // clearly not 12 V (e.g. USB 5 V)
  else if (!s_present && mv > 8000) s_present = true;    // hysteresis back to present
  return s_present;
}
bool forced()          { return s_force; }
void setForce(bool on) { s_force = on; }

const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "POWERON";
    case ESP_RST_EXT:      return "EXT";
    case ESP_RST_SW:       return "SW";
    case ESP_RST_PANIC:    return "PANIC";
    case ESP_RST_INT_WDT:  return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT:      return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";   // <- suspected cold-boot lamp-inrush reboot
    case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
    case ESP_RST_SDIO:     return "SDIO";
    default:               return "UNKNOWN";
  }
}

}  // namespace power
