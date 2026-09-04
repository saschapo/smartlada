# SmartLadaRevC

Firmware for the **SmartLada Rev C** board (ESP32-C6-WROOM-1) — the primary firmware of the
project. Controls four 12 V lamps of a VAZ-2106 tail light with PWM dimming, an OLED menu,
animation effects, and Zigbee control from a Yandex Station (Alice). Local (buttons/OLED)
and remote (Zigbee) control share one state, last-writer-wins.

Forked from `../SmartLadaRevB`; the `src/` layers are reused, plus a Zigbee layer.

## Features

- **4 PWM lamp channels** — LEDC, per-channel gamma + min/max window + soft-start slew.
- **OLED menu** (SSD1315, I²C) driven by 4 buttons: mode, brightness (per-channel + master),
  effect timings, lamp calibration (gamma/soft-start/levels/PWM freq), display, factory reset.
- **Effects**: Breathe, Blink, Chase, Fade (pure phase functions; editable timings, in NVS).
- **Zigbee end device** — five endpoints to Alice:
  - `EP10..13` — the four lamps (`ZigbeeDimmableLight`: on/off + brightness).
  - `EP14` "Fara" (`ZigbeeColorDimmableLight`) — **level = master brightness**, **hue = effect
    selector**, on/off = master power.
- Settings + effect timings persisted in NVS.

## Architecture

Thin `.ino` wiring; logic in `src/*` layers. **`config::s` is the single source of truth**;
the menu and the Zigbee callbacks both write it (last-writer-wins), and the loop composits it:

```
setup(): channels::begin -> display -> config::load -> fx::loadParams
         -> setFreq/Calib/SoftMs -> buttons -> menu -> zb::begin (non-blocking join)
loop():  buttons::poll -> menu::update -> zb::update -> [zb::consumeDirty -> menu redraw]
         fx::compute(mode, now, master, faraOn, lampOn, staticBri, out[4])   // compositor
         channels::write(now, out)                                            // slew -> gamma -> LEDC
         menu::render()                                                       // redraws only on change
```

| Layer | Role |
|---|---|
| `src/channels` | LEDC PWM (10-bit), soft-start slew, gamma/min-max LUT |
| `src/fx`       | effects + the compositor (`compute`) |
| `src/input`    | 4 buttons, debounce + accelerating repeat |
| `src/display`  | SSD1315 over Adafruit_SSD1306 |
| `src/ui`       | OLED menu state machine |
| `src/config`   | NVS-backed `Settings` (`config::s`) |
| `src/net`      | Zigbee endpoints + callbacks -> `config::s` |

### Compositor / control model

- **Fara on + effect mode** → animation frame × master, across all 4 channels.
- **Fara on + static (mode 0)** → per-channel `staticBri × master`, gated by the `lampOn` bitmask.
- **Fara off (passthrough)** → per-channel `staticBri` (no master), individual lamp control.
- A **lone** individual-lamp command exits an effect into static; a lamp command that is part
  of a **group** operation (Alice group blasts level to all endpoints at once) keeps the mode
  — distinguished by proximity to the last Fara command (`GROUP_WINDOW_MS`).
- The local menu keeps `faraOn = 1`, so `master` always applies to local control.

## Pin map (ground truth, from the PCB netlist)

```
Lamp channels:  OUT0=IO1  OUT1=IO0  OUT2=IO2  OUT3=IO3   (low-side AOD4184, gate active-HIGH)
Buttons (J4):   KEY1=IO23 KEY2=IO22 KEY3=IO21 KEY4=IO20  (active-LOW, INPUT_PULLUP)
I2C (OLED):     SCL=IO18  SDA=IO19   addr 0x3C
Native USB:     D-=IO12   D+=IO13    (CDCOnBoot=cdc)
UART debug J8:  TX=IO16   RX=IO17
PD power-good:  IO10  (open-drain, active-LOW: LOW = 12 V present)
VBUS sense:     IO4   (ADC; divider R7=100k/R8=33k)
Strapping:      BOOT=IO9 (SW2)   RESET=EN (SW1)
```

Note: Rev C has **no external gate pulldown** — `channels::begin()` runs first in `setup()`
to force the outputs low at boot.

## Build & flash

```sh
FQBN="esp32:esp32:esp32c6:ZigbeeMode=ed,PartitionScheme=zigbee_8MB,CDCOnBoot=cdc,FlashSize=16M"
arduino-cli compile -b "$FQBN" SmartLadaRevC          # always compile before uploading
arduino-cli upload  -b "$FQBN" -p /dev/cu.usbmodemXXXX SmartLadaRevC
```

- **`ZigbeeMode=ed` + the `zigbee_8MB` partition are required** (the firmware includes the
  Zigbee stack). `CDCOnBoot=cdc` routes `Serial` to native USB.
- **LEDC on the C6 clocks from 40 MHz**, so `RES_BITS = 10` (max PWM ≈ 40e6/2^10 ≈ 39 kHz;
  the 20 kHz default and the whole menu range fit). Higher resolution would cap below 20 kHz.
- Zigbee bonding lives in the `zb_storage` partition and survives reflash — **no need to
  re-pair**. Bumping `config::MAGIC` resets settings once on the next boot.
- Zigbee-stack logs go to **UART0 (J8)**, not USB-CDC; on USB you only see the firmware's own
  `Serial.printf` (`ZB Fara/lamp ...`). Headless capture: `stdbuf -oL cat /dev/cu.usbmodemXXXX > log`.

## Status & roadmap

Hardware fully brought up; Zigbee integrated and paired to a Yandex Station (5 devices,
effects by color, local menu in parallel). Open items (see [`DEV_PLAN.md`](DEV_PLAN.md) and
[`SESSION_HANDOFF.md`](SESSION_HANDOFF.md)):

1. **Report-back board→Alice** — reflect state changes (mode/brightness) back to the app.
2. **PD-gating** on PG (IO10) + a debug bypass to drive outputs at 5 V without lamps.
3. Effect set curation for the hue→effect map.
4. **Wi-Fi / web UI** (cineink-style: QR provisioning, `smartlada.local`) with a Wi-Fi↔Zigbee toggle.
