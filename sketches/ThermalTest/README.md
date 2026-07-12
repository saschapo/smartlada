# ThermalTest — DS18B20 validation bench (ESP8266 / HW-364A)

Standalone test sketch that validates **2× DS18B20** (PSU, BODY) on one 1-Wire
bus, plus the phone time-sync, the cineink-style event log and the dark web view —
before the thermal layer is ported into `SmartLadaC6`. It does **not** drive
lamps/PWM. (The ESP/die point comes from the C6 internal sensor on the target;
room temp ~22.5 °C is the ambient reference.)

Full spec: [docs/TZ_thermal_test.md](docs/TZ_thermal_test.md).

## Wiring

| Signal | Pin | Notes |
|--------|-----|-------|
| 1-Wire data | **GPIO13 (D7)** | one bus, both sensors |
| Pull-up | 4.7 kΩ | data → **3.3 V**, one for the whole bus |
| Sensor power | 3.3 V | normal (non-parasite), common ground |
| OLED | SDA=GPIO14, SCL=GPIO12, `0x3C` | onboard SSD1306 |
| Button | FLASH=GPIO0 | cycles OLED pages |

## Layers

```
ThermalTest.ino        thin: setup/loop + temperature-logging policy
src/thermal/           2x DS18B20, non-blocking 12-bit, ROM->point map
src/rtc/               wall clock seeded by the phone (/api/time)
src/log/               LittleFS event log (/log.txt, rotates 512 KB)
src/stats/             running min/max/avg + recent-sample ring (OLED pages 4-5)
src/display/           OLED: temps / status / ROMs / history / min-max-avg (5 pages)
src/web/               SoftAP + HTTP: UI, /api/state, /api/time, /log, /clearlog
```

## Build & flash

Libraries: `OneWire`, `DallasTemperature`, `Adafruit SSD1306` (+ Adafruit GFX).
Board: NodeMCU 1.0 (HW-364A compatible), flash layout with a filesystem for the log.

```sh
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2:eesz=4M2M ThermalTest.ino
arduino-cli upload  --fqbn esp8266:esp8266:nodemcuv2:eesz=4M2M -p <PORT> ThermalTest.ino
```

## Bring-up

1. On boot the serial log prints every ROM address on the bus (`dumpBus`). The two
   bench sensors are already pinned in `CONFIGURED[]` in `src/thermal/thermal.cpp`
   (psu / body — provisional; heat one point to confirm and swap if needed).
2. Connect to Wi-Fi AP **ThermalTest** (password `thermaltest`), open
   `http://192.168.4.1`. The page sets the device clock from your phone and shows
   the three live temperatures.
3. Logs: **DOWNLOAD LOG** / **LOG.BAK** / **CLEAR LOG** on the page. Sample policy:
   a control stamp every 60 s + a `Δ`-marked line whenever any sensor moves > 2 °C.
