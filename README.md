# SmartLada — smart VAZ-2106 tail light

Firmware and custom hardware for a "smart" VAZ-2106 tail light: PWM dimming of the four
lamps (turn / brake / marker / reverse) with a local OLED menu, animation effects, and
**Zigbee control from a Yandex Station (Alice)**. Runs on a custom board that drops into
the light housing and is powered over **USB-C PD**.

The current, primary firmware is **[`SmartLadaRevC/`](SmartLadaRevC/)** — it targets the
Rev C product board and is what the project ships. Everything else in the tree is either
the hardware design or earlier development stages (see [Lineage](#lineage)).

## Main firmware — SmartLadaRevC

ESP32-C6-WROOM-1 firmware for the Rev C board. Four PWM lamp channels (low-side MOSFET,
gamma + soft-start), an OLED menu with four buttons, pluggable effects, and a Zigbee end
device exposing five endpoints to Alice (four individual lamps + a "Fara" master/effects
device). Local control and Zigbee share one state (`config::s`, last-writer-wins).

Full details: **[`SmartLadaRevC/README.md`](SmartLadaRevC/README.md)**.

### Hardware (Rev C board)

- **MCU:** ESP32-C6-WROOM-1-N16 (integrated on the board).
- **Power in:** USB-C PD, **12 V / 3 A trigger** (CH224K). 12 V feeds the lamps directly;
  a **TPS54202 buck** makes 3.3 V for the MCU/OLED. No 12 V → holds 5 V (logic alive, lamps dark).
  Recommended PSU: **COMMO Core** (a model with a 12 V/3 A PDO).
- **Lamp switches:** 4× AOD4184 N-MOSFET (low-side, gate active-high), outputs on **6.3 mm
  Faston tabs** with a common +12 V. Lamps: 12 V incandescent (e.g. 10+10+10+5 W).
- **UI:** SSD1315 OLED (I²C) + 4 buttons on connector J4, cabled out of the housing.
- **Diagnostics:** 6× red 0805 LEDs (4 channels + 12 V + 3.3 V); PG (power-good) → GPIO.

Board design (KiCad, BOM, fabrication): **[`custom_pcb/smartlada_revC/`](custom_pcb/smartlada_revC/)**.

### Build & flash

Zigbee end-device build, Serial over native USB:

```sh
FQBN="esp32:esp32:esp32c6:ZigbeeMode=ed,PartitionScheme=zigbee_8MB,CDCOnBoot=cdc,FlashSize=16M"
arduino-cli compile -b "$FQBN" SmartLadaRevC
arduino-cli upload  -b "$FQBN" -p /dev/cu.usbmodemXXXX SmartLadaRevC
```

Plan and resume notes: [`SmartLadaRevC/DEV_PLAN.md`](SmartLadaRevC/DEV_PLAN.md),
[`SmartLadaRevC/SESSION_HANDOFF.md`](SmartLadaRevC/SESSION_HANDOFF.md).

## Lineage

The firmware grew through several stages; the earlier ones are kept for reference:

- [`sketches/SmartLada/`](sketches/SmartLada/) — first bring-up bench on an **ESP8266**
  (calibration, OLED). Its `channels/` and `config/` layers seeded the ESP32 port.
- [`SmartLadaC6/`](SmartLadaC6/) — **ESP32-C6 on a MuseLab nanoESP32-C6** dev board:
  LEDC PWM, Wi-Fi AP + web UI, test modes. First C6 firmware.
- [`SmartLadaRevB/`](SmartLadaRevB/) — display-based firmware (OLED menu + effects) on the
  Rev B board; direct **predecessor to Rev C** (Rev C forks its `src/` layers).
- **[`SmartLadaRevC/`](SmartLadaRevC/)** — current product firmware (this board + Zigbee).

Hardware history lives under [`custom_pcb/`](custom_pcb/) (Rev A → Rev B → Rev C);
research and architecture notes under [`research/`](research/); experimental sketches
under [`sketches/`](sketches/).

## Project context (Russian)

Historical planning/context documents, kept in Russian on purpose:

- [`PROJECT_2106_SMART_LIGHT.md`](PROJECT_2106_SMART_LIGHT.md) — decisions, hardware, architecture, plan.
- [`VAZ2106_Smart_Lamp_Context.md`](VAZ2106_Smart_Lamp_Context.md) — earlier context document.
- [`TASK_smartlada_bench_esp8266.md`](TASK_smartlada_bench_esp8266.md) — ESP8266 bench spec (done).
- [`schematic_2106_ESP32.md`](schematic_2106_ESP32.md) — early wiring schematic.

> Chat and `.md` documentation are in Russian; firmware code, comments, and identifiers are in English.
