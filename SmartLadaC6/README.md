# SmartLada C6 — PWM bring-up bench (ESP32-C6)

Port of the [SmartLada (ESP8266)](../sketches/SmartLada/) bench to the ESP32-C6 N16. The goal of
this stage is to verify 4 channels on **hardware PWM (LEDC)** without a Zigbee
coordinator. Control is temporarily via Wi-Fi AP + web UI (the same as on ESP8266). The
final target is Zigbee on the C6.

> **Status: fw 0.7.0-c6, flashed and working (2026-07-11).** Verified on live lamps:
> 4 channels, master, 5 dynamics test modes + DRIVE (driving-simulation state machine),
> NeoPixel indication, BOOT button (mode switching + smooth master-brightness dimmer),
> web-editable mode timings. Calibration baked into defaults. Next — Zigbee.

## What was ported, what was rewritten

| Layer | Status on C6 |
|-------|--------------|
| `src/channels/` | ported **unchanged** (pure C++: gamma, min/max, cap, soft-start) |
| `src/config/` | ported; GPIO 0..3, defaults = captured calibration, PWM range 10–30000 Hz |
| `src/platform_pwm/` | **rewritten** from ESP8266 `analogWrite` to **LEDC** (10 bit, `ledcAttach/ledcWrite`) |
| `src/anim/` | **new** — mode engine: 4 PWM tests with web timings (CHASE/SEQ/PULSE/ALT) + TURN + DRIVE (driving-simulation state machine) |
| `src/web/` | `WebServer`/`WiFi` (arduino-esp32); UI rewritten (dark theme, master, modes, version) |
| `src/status/` | **new** — NeoPixel: network status in MANUAL, active-mode color in animations |
| `src/button/` | **new** — BOOT button: mode switch + master-brightness dimmer |
| `src/version.h` | `FW_VERSION` — Serial at startup, `/status`, UI header |

## Channel map (ESP32-C6)

| Idx | Key       | Name    | Power  | GPIO   |
|-----|-----------|---------|--------|--------|
| CH0 | `stop`    | Stop    | 10 W   | GPIO0  |
| CH1 | `reverse` | Reverse | 10 W   | GPIO1  |
| CH2 | `turn`    | Turn    | 10 W   | GPIO2  |
| CH3 | `marker`  | Marker  | 10 W   | GPIO3  |

Lamps — **R10W 12V BA15S** on all channels. 40 W / 3.33 A total at 12 V.

**Why these pins.** The C6 strapping pins — `GPIO4, 5, 8, 9, 15` — are off-limits for
channels. Also taken: `GPIO8` = onboard NeoPixel, `GPIO12/13` = native USB, `GPIO24..30`
= SPI flash. `GPIO0..3` — not strapping, ADC1, can do LEDC; at boot they are inputs with
no pull, so the D4184 module's gate pulldown holds the low-side channel off. Pins are set
in `src/config/config.cpp` (`CHANNEL_DEFS`); the `gpio` field from imported JSON is
ignored (live re-pinning on a power bench is dangerous).

Board and full GPIO map (strapping, USB, flash) — [docs/board_nanoESP32-C6.md](docs/board_nanoESP32-C6.md).

## Indication (onboard NeoPixel, GPIO8)

`neopixelWrite` is built into the arduino-esp32 core (RMT), no external library needed.

In **MANUAL** — network status; in animations — the mode's signature color.

| State | Color |
|-------|-------|
| MANUAL, AP client present | steady green |
| MANUAL, AP with no clients | slow blue "breathing" |
| TURN | amber, blinking |
| CHASE | cyan, blinking |
| SEQ | green, blinking |
| PULSE | violet, "breathing" |
| ALT | red, blinking |
| DRIVE | in sync with the lamp: turn→yellow, reverse→white, stop→red, driving with marker→steady dim white |

## Build and flash (arduino-cli)

Core `esp32:esp32` 3.3.x. Everything is in the stock core (LEDC, WebServer, NeoPixel) —
no extra libraries.

```sh
FQBN="esp32:esp32:esp32c6:FlashSize=16M,CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB"
arduino-cli compile -b "$FQBN" SmartLadaC6
arduino-cli upload  -b "$FQBN" -p /dev/cu.usbmodemXXXX SmartLadaC6
arduino-cli monitor -p /dev/cu.usbmodemXXXX -c baudrate=115200
```

`CDCOnBoot=cdc` routes `Serial` to the native USB (monitor on the same port).

## Usage

1. After startup all 4 channels = 0% (cap removed by default: `max_duty_cap = 100%`).
2. Connect to the Wi-Fi AP **SmartLada** (password `smartlada`), open **http://192.168.4.1**.
3. **MASTER** — a shared slider + ON 100%/OFF for all channels.
4. **MODE** — MANUAL + 5 PWM dynamics test modes (CHASE/SEQ/PULSE/ALT timings are
   editable in the "MODE TIMINGS" section, 100–10000 ms) + **DRIVE** (driving-simulation
   scenario, see [docs/TZ_drive_mode.md](docs/TZ_drive_mode.md)). Switching is instant;
   manual sliders are locked during an animation.
5. **CHANNELS** — per-channel sliders.
6. **GLOBAL** — `pwm_freq_hz` (10–30000), `soft_start_ms` (0–5000), cap,
   mode timings. **CONFIG JSON** — Export/Import.

### BOOT button (local control, GPIO9)

The onboard BOOT button acts as a local remote (`src/button/` layer):

| Gesture | Action |
|---------|--------|
| Short press | next mode, wrapping (MANUAL → … → DRIVE → MANUAL) |
| Hold (≥0.5 s) | smooth master-brightness dimmer, 0↔100% over 5 s |

**Master brightness** is a global multiplier on the final duty (`channels::setMasterPct`),
active **in all modes** including animations: holding during an animation smoothly
dims/brightens the whole scene. In **MANUAL** all lamps turn on at 100% base, so the
master dims them together (MANUAL is fully part of the mode ring). The dimmer direction
**alternates** from hold to hold; at a limit (0/100%) it is forced back into range.
Startup level is 100% (so animations are visible immediately), the first hold goes down.
Off/on is just dimming down to 0 / up from 0.

The level is visualized on the onboard NeoPixel: **0% — bright red**, **100% — full
green**, in between — white with **gamma correction** (b ∝ level^2.2), so brightness
changes evenly to the eye across the whole range (a linear scale was barely
distinguishable in the top third).

> GPIO9 is strapping only at boot (do not hold the button while powering up — it enters
> download mode); at runtime it is read as a regular input.

HTTP API: `GET /`, `GET /config.json`, `POST /config`, `POST /set?ch=N&pct=P`,
`POST /all?pct=P` (master), `POST /mode?id=N` (0=MANUAL..6=DRIVE), `GET /status`
(fw, ssid, clients, heap, freq, cap, mode, pct[]).

Animation modes (`src/anim/`): any manual action (`/set`, `/all`) resets the mode to
MANUAL. Brightness mapping (logic in `channels/`, shared with ESP8266):
`slider% → gamma → [min_duty..max_duty] → min(duty, cap) → soft-start ramp → LEDC`.

## ⚠️ When connecting power hardware

Power switch schematic (D4184, low-side) — [docs/mosfet_switch_D4184.md](docs/mosfet_switch_D4184.md).

Lamp source — **Ecola B2L080ESB**: regulated DC 12 V, 80 W, max 6.6 A, short-circuit /
overload protection. It is a DC block (not an electronic transformer), so low-side PWM on
the D4184 is correct. The 40 W / 3.33 A load is ~50% of the PSU, fine even with the cap
removed.

- **Common ground** C6 ↔ PSU GND is mandatory (PWM reference).
- **Fuse** on the 12 V output — **T4A slow-blow** (rated 3.33 A, below the PSU's 6.6 A
  ceiling, tolerates the soft-started inrush). The former T6.3–8 A was for 68 W — too much now.
- **Inrush.** A cold R10W filament is ≈ 1–1.5 Ω → ~8–10 A/lamp inrush. Absorbed by the
  soft-start ramp; **do not set `soft_start_ms=0` with these lamps**, especially when
  channels turn on simultaneously, or the PSU may trip (hiccup).
- The gate pulldown on each D4184 is mandatory; pins `GPIO0..3` are boot-safe, but that
  does not replace it. On first power-up verify "duty 0 = lamp off".

## Next stage

Zigbee firmware on the C6 (`ZigbeeDimmableLight`, 4 dimmable endpoints). The core already
has `PartitionScheme=zigbee*` options. The `channels/` layer and the calibration numbers
carry over unchanged; `web/` and `status/` (the AP part) are replaced by Zigbee logic.
