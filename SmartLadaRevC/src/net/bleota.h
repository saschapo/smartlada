#pragma once
#include <Arduino.h>

// BLE GATT firmware-OTA service. Advertised ONLY in the dedicated OTA boot mode (Zigbee is
// NOT started then, so the C6 radio is free -> reliable transfer; bulk BLE over a live Zigbee
// link was unreliable). A host client (tools/ble_ota.py, bleak) streams a .bin into the
// inactive OTA slot; on FINISH the device sets it bootable and reboots.
//
// GATT (service 5ada0a70-...-0001):
//   CTRL (write):        [0x01][token u32][size u32] START | [0x02] FINISH | [0x03] ABORT
//   DATA (write-no-rsp): raw firmware bytes, streamed
//   STATUS (notify):     [state u8][received u32]  (state: 0 idle,1 recv,2 done,3 error)
namespace bleota {

// Written to an RTC_DATA_ATTR word by the menu, then the device reboots; setup() sees it and
// enters BLE-OTA mode (Zigbee NOT started -> the C6 radio is free for a reliable transfer).
constexpr uint32_t OTA_REQ_MAGIC = 0xB1E00A7A;

void begin(const char* devName);   // init BLE, register the OTA service, start advertising
bool active();                     // true while a transfer is in progress (freeze the main loop)
uint8_t progress();                // 0..100 percent received (for the UPDATING screen)

}  // namespace bleota
