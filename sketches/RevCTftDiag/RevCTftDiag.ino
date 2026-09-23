// RevCTftDiag - find why the ST7789 on Rev C J4 (+J8) glitches / goes white.
// J4-6 SCL/IO18=SCLK  J4-5 SDA/IO19=MOSI  J4-4 IO20=DC  J8-2 RX/IO17=CS  J8-3 TX/IO16=RES (BLK open)
// Each pass, HW SPI @1 MHz:
//   A: default pad drive (~20 mA)  colours red / green / blue / black
//   B: weakest pad drive (~5 mA)   colours yellow / cyan / magenta / black
// After begin() it dumps IO_MUX function + GPIO-matrix output select for 16..20, proving
// whether the UART0 pins are really GPIO-owned (mcu_sel=1, out_sel=128 or the SPI signal).
// Same partition table as the firmware (NVS untouched):
// arduino-cli compile -b esp32:esp32:esp32c6:PartitionScheme=zigbee_8MB,CDCOnBoot=cdc,FlashSize=16M sketches/RevCTftDiag
#include <Arduino_GFX_Library.h>
#include <driver/gpio.h>
#include <soc/gpio_periph.h>
#include <soc/gpio_reg.h>

enum { SCLK = 18, MOSI_ = 19, DC = 20, CS = 17, RES = 16 };
static const uint8_t PINS[] = {RES, CS, SCLK, MOSI_, DC};
static const char* NAMES[] = {"RES ", "CS  ", "SCLK", "MOSI", "DC  "};

Arduino_DataBus* bus = new Arduino_ESP32SPI(DC, CS, SCLK, MOSI_, GFX_NOT_DEFINED);
Arduino_GFX* tft = new Arduino_ST7789(bus, RES, 1, false, 240, 320);

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 5000) delay(10);
}

void dump() {
  for (int i = 0; i < 5; i++) {
    uint8_t n = PINS[i];
    uint32_t mux = REG_READ(GPIO_PIN_MUX_REG[n]);
    uint32_t sel = REG_READ(GPIO_FUNC0_OUT_SEL_CFG_REG + 4 * n) & 0xFF;
    Serial.printf("  %s IO%-2u mcu_sel=%lu drv=%lu out_sel=%lu\n", NAMES[i], n,
                  (unsigned long)((mux >> 12) & 7), (unsigned long)((mux >> 10) & 3), (unsigned long)sel);
  }
}

void pass(bool weak) {
  static const uint16_t cA[] = {RGB565_RED, RGB565_GREEN, RGB565_BLUE, RGB565_BLACK};
  static const uint16_t cB[] = {RGB565_YELLOW, RGB565_CYAN, RGB565_MAGENTA, RGB565_BLACK};
  const char* tag = weak ? "B weak drive" : "A default drive";
  bool ok = tft->begin(1000000);
  for (uint8_t p : PINS) gpio_set_drive_capability((gpio_num_t)p, weak ? GPIO_DRIVE_CAP_0 : GPIO_DRIVE_CAP_2);
  Serial.printf("[%s] begin=%d\n", tag, ok);
  dump();
  for (int i = 0; i < 4; i++) {
    tft->fillScreen(weak ? cB[i] : cA[i]);
    delay(1200);
  }
}

void loop() {
  pass(false);
  pass(true);
}
