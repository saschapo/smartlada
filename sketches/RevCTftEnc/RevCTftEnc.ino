// RevCTftEnc - minimal bring-up of the 2.4" ST7789 + EC11 module on the Rev C J4 header.
// CS/RES from J8 (RX/IO17, TX/IO16): the panel stays white with CS->GND / RES->3V3. BLK, K0 open.
// J4: 6 SCL/IO18=SCLK  5 SDA/IO19=MOSI  4 KEY4/IO20=DC  3 KEY3/IO21=A  2 KEY2/IO22=B  1 KEY1/IO23=PUSH
// Long PUSH (>1 s) = ESP.restart(): checks the panel comes back after a soft reset.
// arduino-cli compile -b esp32:esp32:esp32c6:CDCOnBoot=cdc sketches/RevCTftEnc
#include <Arduino_GFX_Library.h>
#include <ESP32Encoder.h>

#define SPI_HZ 20000000L         // conservative over the J4 cable; raise after it proves stable

Arduino_DataBus* bus = new Arduino_ESP32SPI(20, 17, 18, 19, GFX_NOT_DEFINED);
Arduino_GFX* gfx = new Arduino_ST7789(bus, 16, 1, false, 240, 320);  // ST7789 forces SPI_MODE3
ESP32Encoder enc;
RTC_NOINIT_ATTR uint32_t boots;

void setup() {
  if (esp_reset_reason() == ESP_RST_POWERON) boots = 0;
  boots++;
  pinMode(23, INPUT);                       // module has 10k pull-up + RC
  ESP32Encoder::useInternalWeakPullResistors = puType::none;
  enc.attachFullQuad(22, 21);
  enc.setFilter(1023);
  gfx->begin(SPI_HZ);
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextSize(3);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->drawRect(0, 0, gfx->width(), gfx->height(), RGB565_RED);   // edges visible = no offset
}

void loop() {
  static int32_t last = INT32_MIN;
  static bool lastBtn = true;
  static uint32_t down = 0;
  int32_t c = (int32_t)enc.getCount() / 4;
  bool btn = digitalRead(23);
  if (!btn && lastBtn) down = millis();
  if (!btn && millis() - down > 1000) ESP.restart();
  if (c == last && btn == lastBtn) return;
  last = c; lastBtn = btn;
  gfx->setCursor(20, 40);  gfx->printf("enc  %-6ld", (long)c);
  gfx->setCursor(20, 90);  gfx->printf("push %s", btn ? "up  " : "DOWN");
  gfx->setCursor(20, 140); gfx->printf("boot %-4lu", (unsigned long)boots);
}
