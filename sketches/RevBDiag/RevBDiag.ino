// RevBDiag -- diagnostic for the Rev B OLED + buttons bring-up.
// Build with USB CDC On Boot = Enabled so Serial appears on the native USB port:
//   arduino-cli compile --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc sketches/RevBDiag
//
// Prints an I2C bus scan (is the OLED there? at what address?) and the raw level
// of each button pin once per second (independent of any debounce logic).
// Rev B pinout: SCL=GPIO18 SDA=GPIO19  K1..K4 = GPIO20..23.

#include <Wire.h>

static constexpr uint8_t PIN_SCL = 18;
static constexpr uint8_t PIN_SDA = 19;
static const uint8_t KEY_PINS[4] = {20, 21, 22, 23};
static const char*    KEY_NAME[4] = {"K1", "K2", "K3", "K4"};

static void scanOnPins(uint8_t sda, uint8_t scl) {
  Wire.end();
  Wire.begin(sda, scl);
  delay(5);
  Serial.printf("I2C scan SDA=%u SCL=%u: ", sda, scl);
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("0x%02X ", addr);
      found++;
    }
  }
  if (!found) Serial.print(F("(nothing)"));
  Serial.println();
}

static void i2cScan() {
  scanOnPins(PIN_SDA, PIN_SCL);   // expected wiring
  scanOnPins(PIN_SCL, PIN_SDA);   // swapped SDA/SCL
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 3000) { delay(10); }  // wait for USB CDC
  delay(200);
  Serial.println();
  Serial.println(F("=== RevBDiag ==="));
  Serial.println(F("I2C SDA=GPIO19 SCL=GPIO18  keys K1..K4=GPIO20..23 (INPUT_PULLUP)"));

  for (uint8_t i = 0; i < 4; i++) pinMode(KEY_PINS[i], INPUT_PULLUP);

  Wire.begin(PIN_SDA, PIN_SCL);
  i2cScan();
}

void loop() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last >= 1000) {
    last = now;
    i2cScan();
    Serial.print(F("keys (1=released 0=pressed): "));
    for (uint8_t i = 0; i < 4; i++) {
      Serial.printf("%s=%d  ", KEY_NAME[i], digitalRead(KEY_PINS[i]));
    }
    Serial.println();
  }
}
