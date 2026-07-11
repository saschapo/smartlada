# SmartLada — smart VAZ-2106 tail light

Firmware for a "smart light" built from a stock VAZ-2106 tail light: PWM dimming of 4
lamps (stop / reverse / turn / marker) with calibration, web control, and dynamic modes.

The main project is **[`SmartLadaC6/`](SmartLadaC6/)** — the ESP32-C6 firmware. The
earlier ESP8266 calibration bench is archived under [`sketches/`](sketches/).

## Main project — SmartLadaC6 (ESP32-C6)

Current firmware on the ESP32-C6 (MuseLab nanoESP32-C6): hardware PWM (LEDC), Wi-Fi AP +
web UI, BOOT button, 5 test modes + DRIVE, NeoPixel indication. Next — Zigbee.
Full details in **[`SmartLadaC6/README.md`](SmartLadaC6/README.md)**.

### Hardware

- **Controller:** ESP32-C6 (nanoESP32-C6), channels on `GPIO0..3` — see the [board doc](SmartLadaC6/docs/board_nanoESP32-C6.md).
- **Power switch:** 4× D4184 module (N-MOSFET, low-side) — see the [switch schematic](SmartLadaC6/docs/mosfet_switch_D4184.md).
- **Lamps:** R10W 12V BA15S ×4 (40 W / 3.33 A). **PSU:** Ecola B2L080ESB (DC 12 V, 80 W).

### Build and flash

```sh
FQBN="esp32:esp32:esp32c6:FlashSize=16M,CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB"
arduino-cli compile -b "$FQBN" SmartLadaC6
arduino-cli upload  -b "$FQBN" -p /dev/cu.usbmodemXXXX SmartLadaC6
```

## Archived sketches

- [`sketches/SmartLada/`](sketches/SmartLada/) — the first bring-up bench on the ESP8266
  (finished 2026-07-06). PWM `analogWrite`, OLED, lamp calibration. Its `channels/` and
  `config/` layers were the base ported to the C6.

## Project context (Russian)

Historical handoff/planning documents, kept in Russian on purpose:

- [`PROJECT_2106_SMART_LIGHT.md`](PROJECT_2106_SMART_LIGHT.md) — project context: decisions, hardware, architecture, plan.
- [`VAZ2106_Smart_Lamp_Context.md`](VAZ2106_Smart_Lamp_Context.md) — earlier context document.
- [`TASK_smartlada_bench_esp8266.md`](TASK_smartlada_bench_esp8266.md) — ESP8266 bench spec (done).
- [`schematic_2106_ESP32.md`](schematic_2106_ESP32.md) — wiring schematic.

> Everything else (firmware code, comments, docs, UI) is in English.
