#include "bleota.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_ota_ops.h>
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

namespace bleota {

// Basic gate so a stray BLE writer can't flash us. Not real security -- change if it matters.
static constexpr uint32_t OTA_TOKEN = 0x5A5AA5A5;

#define SVC_UUID  "5ada0a70-0000-4a5a-b1ed-5a5aa5a50001"
#define CTRL_UUID "5ada0a70-0001-4a5a-b1ed-5a5aa5a50001"
#define DATA_UUID "5ada0a70-0002-4a5a-b1ed-5a5aa5a50001"
#define STAT_UUID "5ada0a70-0003-4a5a-b1ed-5a5aa5a50001"

static BLECharacteristic*     s_status  = nullptr;
static esp_ota_handle_t       s_handle  = 0;
static const esp_partition_t* s_part    = nullptr;
static volatile bool          s_receiving = false;   // set on START, cleared on end/abort
static volatile bool          s_finishReq = false;   // FINISH requested; writer flushes then ends
static volatile bool          s_abortReq  = false;   // ABORT/disconnect requested; writer aborts
static volatile bool          s_sessionOpen = false; // esp_ota_begin succeeded, not yet ended/aborted
static volatile bool          s_finalizing  = false; // commit in flight (esp_ota_end .. reboot): no
                                                     // new session, no cancellation past this point
static volatile uint32_t      s_recv = 0, s_total = 0;

// Firmware bytes flow BLE-callback -> stream buffer -> writer task. Keeping esp_ota_write
// (which erases flash ~30 ms per sector) OFF the NimBLE callback keeps the link alive.
static StreamBufferHandle_t   s_sb = nullptr;
static constexpr size_t       SB_SIZE = 32 * 1024;

static void notifyStatus(uint8_t state) {
  if (!s_status) return;
  uint8_t buf[5];
  buf[0] = state;
  uint32_t r = s_recv;
  memcpy(buf + 1, &r, 4);
  s_status->setValue(buf, 5);
  s_status->notify();
}

// Dedicated flash-writer task: drains the stream buffer into the OTA slot, and finalises
// when FINISH was requested and the buffer has drained.
static void writerTask(void*) {
  static uint8_t buf[1024];
  for (;;) {
    size_t n = xStreamBufferReceive(s_sb, buf, sizeof(buf), pdMS_TO_TICKS(100));

    // This task is the SOLE owner of esp_ota_write/end/abort. Callbacks (ABORT, disconnect)
    // only REQUEST an abort; we service it here so esp_ota_abort never races an in-flight write
    // on another task. A write already in progress simply completes, then we abort next loop.
    if (s_abortReq) {
      s_abortReq = false; s_finishReq = false; s_receiving = false;
      if (s_sessionOpen) { esp_ota_abort(s_handle); s_sessionOpen = false; }
      xStreamBufferReset(s_sb);
      Serial.println("[BLEOTA] aborted (writer)");
      notifyStatus(0);
      continue;
    }

    if (n && s_receiving) {
      if (esp_ota_write(s_handle, buf, n) != ESP_OK) {
        if (s_sessionOpen) { esp_ota_abort(s_handle); s_sessionOpen = false; }
        s_receiving = false; s_finishReq = false; notifyStatus(3);
        continue;
      }
      uint32_t before = s_recv; s_recv += n;
      if ((before / 4096) != (s_recv / 4096)) notifyStatus(1);   // ack every ~4 KB (client flow control)
    }
    // Cancellation outranks a pending FINISH: an ABORT that landed while the last write was in
    // flight is still unserviced here, so don't commit -- the abort block runs next loop.
    if (s_finishReq && !s_abortReq && xStreamBufferIsEmpty(s_sb)) {
      s_finalizing = true;      // from here to the reboot the session is busy: callbacks may not
      s_finishReq = false;      // start a new one nor cancel this one (s_receiving goes false below)
      s_receiving = false;
      esp_err_t e = esp_ota_end(s_handle); s_sessionOpen = false;
      if (e == ESP_OK) e = esp_ota_set_boot_partition(s_part);
      Serial.printf("[BLEOTA] FINISH recv=%u res=%d\n", (unsigned)s_recv, (int)e);
      notifyStatus(e == ESP_OK ? 2 : 3);
      if (e == ESP_OK) { delay(400); esp_restart(); }
      s_finalizing = false;     // commit failed -> release the gate so the host can retry
    }
  }
}

// On connect, widen the supervision timeout so brief BLE/Zigbee coexistence gaps in the
// radio don't drop the link mid-transfer (interval 15-30 ms, latency 0, timeout 6 s).
class SrvCB : public BLEServerCallbacks {
  void onConnect(BLEServer* s, ble_gap_conn_desc* desc) override {
    s->updateConnParams(desc->conn_handle, 12, 24, 0, 600);
    Serial.println("[BLEOTA] connected; conn params set (sup timeout 6s)");
  }
  void onDisconnect(BLEServer* s, ble_gap_conn_desc*) override {
    // A disconnect right after FINISH is normal -- the host drops the link once every byte is
    // sent -- so it must not cancel the commit. Only a transfer still in flight is aborted.
    if (!s_finalizing && !s_finishReq && (s_receiving || s_sessionOpen)) {
      s_receiving = false;                // (writer owns esp_ota_abort; don't call it from here)
      s_abortReq = true; xStreamBufferReset(s_sb);
      Serial.println("[BLEOTA] disconnected mid-transfer -> abort requested");
    }
    BLEDevice::startAdvertising();   // re-advertise for the next attempt
  }
};

class CtrlCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    uint8_t* d = c->getData();
    size_t   n = c->getLength();
    if (n < 1) return;
    switch (d[0]) {
      case 0x01: {                                   // START [token u32][size u32]
        if (n < 9) { notifyStatus(3); return; }
        uint32_t tok; memcpy(&tok, d + 1, 4);
        if (tok != OTA_TOKEN) { Serial.println("[BLEOTA] bad token"); notifyStatus(3); return; }
        // Reject a second START over a live session, a pending abort, or a commit that has not
        // rebooted yet (esp_ota_end has run but the image is not booted -- still not idle).
        if (s_receiving || s_sessionOpen || s_abortReq || s_finalizing || s_finishReq) {
          Serial.println("[BLEOTA] START ignored: session active"); notifyStatus(3); return;
        }
        memcpy((void*)&s_total, d + 5, 4);
        s_part = esp_ota_get_next_update_partition(NULL);
        if (!s_part || esp_ota_begin(s_part, OTA_SIZE_UNKNOWN, &s_handle) != ESP_OK) {
          notifyStatus(3); return;
        }
        s_sessionOpen = true;
        xStreamBufferReset(s_sb);
        s_recv = 0; s_finishReq = false; s_receiving = true;
        Serial.printf("[BLEOTA] START size=%u -> %s\n", (unsigned)s_total, s_part->label);
        notifyStatus(1);
        break;
      }
      case 0x02:                                     // FINISH (writer task finalises after drain)
        if (!s_receiving) { notifyStatus(3); return; }
        s_finishReq = true;
        break;
      case 0x03:                                     // ABORT (writer performs esp_ota_abort + notifies)
        if (s_finalizing) {                          // past the point of no return: image committed
          Serial.println("[BLEOTA] ABORT ignored: commit in progress"); notifyStatus(3); return;
        }
        if (s_receiving || s_sessionOpen) { s_receiving = false; s_abortReq = true; xStreamBufferReset(s_sb); }
        else notifyStatus(0);
        break;
    }
  }
};

class DataCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    if (!s_receiving) return;
    uint8_t* d = c->getData();
    size_t   n = c->getLength();
    if (n) xStreamBufferSend(s_sb, d, n, pdMS_TO_TICKS(500));   // backpressure if writer is behind
  }
};

void begin(const char* devName) {
  s_sb = xStreamBufferCreate(SB_SIZE, 1);
  xTaskCreate(writerTask, "bleota_wr", 4096, nullptr, 5, nullptr);

  BLEDevice::init(devName);
  BLEDevice::setMTU(517);
  BLEServer*  srv = BLEDevice::createServer();
  srv->setCallbacks(new SrvCB());
  BLEService* svc = srv->createService(BLEUUID(SVC_UUID), 24);

  BLECharacteristic* ctrl = svc->createCharacteristic(CTRL_UUID, BLECharacteristic::PROPERTY_WRITE);
  ctrl->setCallbacks(new CtrlCB());
  // WRITE (with response) gives per-packet flow control -> reliable stream; keep _NR too.
  BLECharacteristic* data = svc->createCharacteristic(
      DATA_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  data->setCallbacks(new DataCB());
  s_status = svc->createCharacteristic(STAT_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  s_status->addDescriptor(new BLE2902());

  svc->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(BLEUUID(SVC_UUID));
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.printf("[BLEOTA] advertising as '%s'\n", devName);
}

bool active() { return s_receiving; }

uint8_t progress() {
  uint32_t t = s_total, r = s_recv;
  if (!t || r > t) return 0;
  return (uint8_t)(r * 100 / t);
}

}  // namespace bleota
