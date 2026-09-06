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
- **Effects**: Breathe, Turn (turn-signal-only blink, 1.5 Hz), Chase, Fade, Drive (a random
  but logical driving-scenario FSM: cruise/brake/stop/turn/reverse/hazard/parked).
  **Chase** runs on a trigger clock: every `Step` a lamp is lit and runs its own
  `Fade In -> On -> Fade Out` envelope, which may outlive later triggers — that overlap is
  what makes lamps dissolve into each other. `Random` picks the next lamp instead of walking
  in order (never the one just lit), turning the same envelope into scattered flicker.
- **Zigbee end device** — five endpoints to Alice:
  - `EP10..13` — the four lamps by function: **Turn / Marker / Reverse / Stop** (`ZigbeeDimmableLight`).
  - `EP14` "Fara" (`ZigbeeColorDimmableLight`) — the **effect layer and nothing else**:
    **color = effect selector**, **level = effect brightness**; **off and white both mean
    "no effect"**. EP14 never touches per-lamp levels.
- Settings + effect timings persisted in NVS, and **restored on power-on**: any change —
  local or remote — is written once it has stopped moving for 4 s.

## Architecture

Thin `.ino` wiring; logic in `src/*` layers. **`config::s` is the single source of truth**;
the menu and the Zigbee callbacks both write it (last-writer-wins), and the loop composits it:

```
setup(): channels::begin -> display -> config::load -> fx::loadParams
         -> setFreq/Calib/SoftMs -> buttons -> menu -> zb::begin (non-blocking join)
loop():  buttons::poll -> menu::update -> zb::update -> [zb::consumeDirty -> menu redraw]
         fx::compute(mode, now, master, lampOn, staticBri, out[4])           // compositor
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

The `mode` alone picks the layer; EP14 (Fara) is the effect layer, EP10-13 (lamps/group) the
static picture:

- **Effect (mode ≠ 0)** → animation frame × `master` (effect brightness), across all 4 channels.
- **Static (mode 0)** → per-channel `staticBri`, gated by `lampOn` — **no master scaling**.
  The static "master" is the group dimmer (and the local idle screen), which fans out into
  `staticBri`, so it is applied once, not twice (no double-dimming).
- **EP14 (Fara)**: off **and** white → no effect (mode 0); color → effect (hue selects it).
  Its level is **always** the effect brightness and never fans out to `staticBri`. Fara never
  turns lamps off. A second group dimmer used to live here (white fanned its level out to
  every lamp); it duplicated EP10-13, flattened per-channel levels on every color change and
  read as a half-finished effect, so it was removed. Group brightness and "set the turn signal
  to 50%" both belong to EP10-13, which already handle group fan-out and per-lamp addressing.
- **Timing**: one time constant shapes everything — Lamp Setup's soft start (`softMs`) is the
  channel slew for both static dimming and effect edges, which is what gives Turn its ramp.
  Effect brightness additionally eases on its own constant (`MASTER_SMOOTH_MS` in the .ino),
  because the app delivers level in bursts of ~10 commands and then holds for about a second.
- **EP10-13 (lamps/group)**: on/off → `lampOn`, level → `staticBri`. Lamp commands never change
  the mode (a level does not force the lamp on, so an Alice group dim leaves off lamps dark).
- The local menu writes `mode` / `staticBri` directly, so it is authoritative (last-writer-wins).

### Yandex color preset compatibility

EP14 receives hue/saturation unchanged, but suppresses automatic reports of those two
attributes before processing color commands. It continues to report brightness/on-off;
EP10–13 reporting is unaffected. Local effect changes do not update the app's color selector.
This is a compatibility workaround, not a correction to the color values.

Hardware investigation on 2026-09-06 (Arduino-ESP32 **3.3.10**) found that even a single
combined H/S report, identical to the received command, could replace a Yandex preset with
an unnamed color. With color reports disabled, white/color transitions, reopening the
device card and changing brightness preserved the preset. Sending one unchanged white
report reproduced the replacement. The precise station/app conversion causing this remains
unknown; no saturation scaling or preset-specific offsets are applied.

Implementation: `onApsIndication()` in `src/net/zigbee.cpp`, chained to Arduino's existing
APS handler so binding-list processing continues. Incoming color attributes remain readable.

Known limits, all still open:

- The hook chains `zb_apsde_data_indication_handler()`, an **internal symbol of Arduino-ESP32
  3.3.10** — not public API. A rename breaks the build loudly; a change in *when* the core
  calls the APS handler would break the workaround silently. Recheck after any core upgrade.
- Installation can fail quietly: if the Zigbee lock is not acquired at start-up the firmware
  logs one line and runs **without** the workaround.
- Reporting is stopped on an incoming Color Control command, so a `Configure Reporting` from
  the coordinator (re-pairing, re-initialisation) re-arms it until the next such command.
- Re-pairing and other controllers are unvalidated. A cross-check on zigbee2mqtt or ZHA would
  say whether this is Yandex-specific interpretation or genuine attribute incoherence
  (`ColorMode` 0x0008 vs `EnhancedColorMode` 0x4001 vs the unmaintained `CurrentX/Y`).

## Pin map (ground truth, from the PCB netlist)

```
Lamp channels:  turn=IO0  marker=IO1  reverse=IO2  stop=IO3   (low-side AOD4184, gate active-HIGH)
                (ch index = function; those GPIOs land on Fastons OUT1/OUT0/OUT2/OUT3)
Buttons (J4):   KEY1=IO23 KEY2=IO22 KEY3=IO21 KEY4=IO20  (active-LOW, INPUT_PULLUP)
I2C (OLED):     SCL=IO18  SDA=IO19   addr 0x3C
Native USB:     D-=IO12   D+=IO13    (CDCOnBoot=cdc)
UART debug J8:  TX=IO16   RX=IO17
PD power-good:  IO10  (open-drain, active-LOW: LOW = 12 V present)
VBUS sense:     IO4   (ADC; divider R7=100k/R8=33k)
Strapping:      BOOT=IO9 (SW2)   RESET=EN (SW1)
```

Note: each MOSFET gate has a **100 k pulldown** (Rpd1..4) + 100 R series (Rg1..4) in
hardware; `channels::begin()` also runs first in `setup()` to force the outputs low at boot.
The USB-C **VBUS pins are the +12 V net** — it feeds both the buck (→3.3 V for the ESP) and
the lamp common (J7) + indicator LEDs, so a cold-boot lamp inrush can brown out the ESP.

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
- **Opening the USB serial port resets the board** (it shows up as `rst USB` on the next boot
  line), even with DTR/RTS cleared beforehand. So starting a capture reboots the device, and
  the serial log cannot be watched during a BLE session at all -- the port open would kick the
  board out of OTA mode.

### Firmware over BLE

No USB needed: **Settings -> System -> Update Over BLE -> Yes** reboots into a dedicated OTA
mode (Zigbee is not started, so the radio is free) and advertises as `SmartLada`. Then

```sh
uv run --with bleak python3 tools/ble_ota.py path/to/SmartLadaRevC.ino.bin
```

- **Run this with the board on PD 12 V.** Powered from USB 5 V the transfer degrades to
  ~6.5 KB/s and stalls partway through (`stalled (no ack)` -- the link stays up but the board
  stops acknowledging blocks). On 12 V the same image goes through at 19-23 KB/s.
- Any button leaves OTA mode. The exit only arms once every key has been released, so the
  button held from the confirm screen does not immediately cancel.
- A link lost *after* `FINISH` does not cancel the update: the image is complete by then and
  only an explicit ABORT stops the commit. `--drop-after-finish` exercises exactly that.

## Status & roadmap

Hardware fully brought up; Zigbee integrated and paired to a Yandex Station (5 devices,
effects by color, local menu in parallel). Open items (see [`DEV_PLAN.md`](DEV_PLAN.md) and
[`SESSION_HANDOFF.md`](SESSION_HANDOFF.md)):

1. **Report-back board→Alice** — reflect state changes (mode/brightness) back to the app.
2. **PD-gating** on PG (IO10) + a debug bypass to drive outputs at 5 V without lamps.
3. Effect set curation for the hue→effect map.
4. **Wi-Fi / web UI** (cineink-style: QR provisioning, `smartlada.local`) with a Wi-Fi↔Zigbee toggle.
