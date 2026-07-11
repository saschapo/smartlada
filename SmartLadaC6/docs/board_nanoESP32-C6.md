# Board — MuseLab nanoESP32-C6

The project's controller module: **MuseLab nanoESP32-C6** (wuxx).
Repository: <https://github.com/wuxx/nanoESP32-C6/>

A small (nano form factor, breadboard-friendly) dev board built on the **ESP32-C6**
with two USB-C ports and an onboard full-color LED. Used in this project as a
temporary controller for the PWM bench (Wi-Fi AP + web UI); the final target is
Zigbee on the same chip.

## Key specs

| Parameter | Value | How it is used in the project |
|-----------|-------|-------------------------------|
| Chip | ESP32-C6 (RISC-V, QFN40, rev v0.2) | arduino-esp32 core 3.3.x |
| Flash | 16 MB (N16 variant) | partition `app3M_fat9M_16MB` |
| PSRAM | none (C6 has no PSRAM) | not needed |
| Radio | Wi-Fi 6 (2.4 GHz), BLE 5, **802.15.4 (Zigbee/Thread)** | now Wi-Fi AP; later Zigbee |
| USB-C #1 | **CH343** USB↔UART (flashing/log) | alternative flashing port |
| USB-C #2 | **native USB** of C6 (Serial/JTAG, `GPIO12/13`) | working port `/dev/cu.usbmodem*`, `CDCOnBoot=cdc` |
| RGB LED | onboard WS2812 on **GPIO8** | `status/` layer (mode/network indication) |
| Buttons | BOOT (`GPIO9`) and RESET | standard flashing/reset |

> Power in the project is from USB (logic); the 12 V power comes separately from the
> Ecola PSU to the D4184 modules. A **common ground with the board is mandatory** (see
> [mosfet_switch_D4184.md](mosfet_switch_D4184.md)).

## GPIO map and why channels are on GPIO0..3

The channel pins were chosen to avoid every occupied/dangerous C6 line. Full layout
below (verified against the Espressif datasheet).

| GPIO | Function on C6 | Free for a channel? |
|------|----------------|:-------------------:|
| **0, 1, 2, 3** | ADC1, plain GPIO, **not strapping** | ✅ **channels CH0..CH3** |
| 4, 5 | **strapping** (MTMS/MTDI, JTAG/SDIO) | ❌ |
| 8 | **strapping** + onboard WS2812 | ❌ |
| 9 | **strapping** (boot mode) + BOOT button | ❌ |
| 12, 13 | native USB (D−/D+) | ❌ |
| 15 | **strapping** (JTAG source) | ❌ |
| 24…30 | SPI flash (in-package) | ❌ |

**Result:** `GPIO0..3` is the only convenient contiguous group that:
- is not strapping → does not affect boot mode;
- can do LEDC (hardware PWM);
- at boot, before firmware init, is an input with no active pull, so the **external
  100K pull-down on the D4184 gate** holds the switch off (lamp off) in the window
  between reset and `platform_pwm::forceAllLow()`.

The pins are fixed by firmware (`src/config/config.cpp` → `CHANNEL_DEFS`); the `gpio`
field from imported JSON is **intentionally ignored** — live re-pinning on a power
bench is dangerous.

## Note on the two USB-C ports

- **Native port** (Serial/JTAG straight from C6): monitor and flashing on one port
  with `CDCOnBoot=cdc`. The project's working setup.
- **CH343 port**: a classic USB-UART, works even if the native USB is busy/unavailable.
  A fallback in case of native-CDC issues.

---

**Sources:** [wuxx/nanoESP32-C6](https://github.com/wuxx/nanoESP32-C6/),
[ESP32-C6 Datasheet](https://documentation.espressif.com/esp32-c6_datasheet_en.html),
[ESP-IDF GPIO (C6)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/gpio.html).
Project specifics — README.md, `src/config/config.cpp`.
