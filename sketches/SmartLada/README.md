# SmartLada — PWM calibration bench (ESP8266)

Hardware bring-up for the smart light built from a VAZ-2106 tail light: 4× 12 V
incandescent lamps through 4 D4184 modules (N-MOSFET, low-side PWM). **This is NOT the
final firmware** — it's a bench for capturing calibration numbers while the ESP32-C6 is
in transit. The final target is Zigbee on the C6.

> **Status: prototype working (2026-07-06).** The wiring below is verified on real tail
> light lamps: 4 channels dim independently, no flicker.
> The captured calibration is in [calibration/](calibration/).

## ⚠️ Power bench — read before powering up

- **[CRITICAL] Common signal ground.** The ESP8266 GND (USB-powered) and the 12 V PSU
  GND must be tied together — otherwise PWM has no reference and switching is
  unpredictable. Note: "common ground" applies to the ESP and PSU, but NOT to the lamp
  negatives — the switched lamp terminals must be separate (see the schematic).
- **[CRITICAL] Fuse.** Slow-blow **T6.3–8 A** at the 12 V PSU output. 68 W total plus
  the cold-filament inrush — this is not a desktop toy.
- **[CRITICAL] The tail-light board is live.** In the verified wiring the common
  tail-light board is the +12 V bus (always "hot"). Do not let it touch anything tied to
  GND: the fuse will catch a short, but better without surprises.
- **D4184 polarity.** On first power-up, verify duty 0 = lamp OFF (no inversion). After a
  flash/reboot all lamps must be off.

## Wiring (verified on hardware)

A quirk of the 2106 tail light: the socket board structurally ties together the
"negatives" of all lamps (in the car the positive is switched, the negative is the
body). For low-side switching the switched terminals must be separate, so the wiring is
**re-polarized** — an incandescent lamp doesn't care about polarity. Only the side we do
NOT switch can be common:

```
12 V PSU (+) ──[fuse T6.3–8 A]──┬── common tail-light board (common POSITIVE of all lamps)
                                │        │ each lamp's filament
                                │   individual socket contact CHx
                                │        │
                                │   OUT− of its D4184 module (drain) — SEPARATE
                                │
                                └── VIN+ of the D4184 modules
12 V PSU GND ── VIN− of the D4184 modules ── ESP8266 GND   (common signal ground)
ESP8266 GPIO4/5/15/13 ── PWM inputs of the D4184 modules
ESP8266 power — separate, via USB
```

If the lamp negatives are joined before the switches (as they are stock in the tail
light), any open switch shorts the loop of all lamps at once — they all light and dim
together. Verified.

## Channel map: section → GPIO

| Idx | Key       | Name    | Power  | GPIO   | Board label |
|-----|-----------|---------|--------|--------|-------------|
| CH0 | `stop`    | Stop    | 21 W   | GPIO4  | D2          |
| CH1 | `reverse` | Reverse | 21 W   | GPIO5  | D1          |
| CH2 | `turn`    | Turn    | 21 W   | GPIO15 | D8          |
| CH3 | `marker`  | Marker  | 5 W    | GPIO13 | D7          |

Bench board — **HW-364A** (ESP8266 + onboard OLED). GPIO12/GPIO14 are taken by the
display's I2C, so CH2 sits on GPIO15 (D8) rather than GPIO12 from the spec.

**GPIO15 catch:** it is a strapping pin that must be LOW at boot (the board already has a
pulldown) — which is a plus for a low-side switch: the channel is guaranteed off at
startup. But nothing external must pull GPIO15 high at power-up, or the ESP won't boot.
The D4184 module's gate pulldown satisfies this; don't add external pullups on that line.

Selection criteria for the other pins: not strapping (GPIO0/2), not GPIO16 (no PWM);
channel off at boot (module gate pulldown, or add an external one). Pins are set in
`src/config/config.cpp` (`CHANNEL_DEFS`); on JSON import the `gpio` field is ignored —
live re-pinning on a power bench is dangerous.

## OLED display and FLASH button (HW-364A board)

Onboard SSD1306 128x64, I2C `0x3C`: **SDA = GPIO14 (D6), SCL = GPIO12 (D5)** — pinout
verified against [peff74/esp8266_OLED_HW-364A](https://github.com/peff74/esp8266_OLED_HW-364A).
The **FLASH = GPIO0** button switches pages (don't hold it on reset — that enters the
bootloader). Screen refresh — 2×/s; ASCII font, channel labels in Latin.

| Page | Contents |
|------|----------|
| 1/3 Status | IP, MAC, AP clients, heap, PWM frequency, cap state, loop/s |
| 2/3 Channels | per channel: GPIO, % slider, current duty (soft-start visible) |
| 3/3 Calibration | per channel: gamma, min..max duty, soft_start_ms |

`loop/s` — the main-loop rate: dips show up during HTTP requests and display redraws,
useful for judging softPWM jitter. If the display is not found, the bench runs without
indication (`OLED not found` in Serial).

## Build and flash

Arduino IDE + ESP8266 core. Libraries: **Adafruit SSD1306** (+ Adafruit GFX, Adafruit
BusIO deps) — for the OLED only; PWM and web use the stock core. Open `SmartLada.ino`,
board NodeMCU 1.0 (HW-364A compatible), flash.

## Usage

1. After startup all 4 channels = 0%, cap `max_duty_cap = 60%` active.
2. Connect to the Wi-Fi AP **SmartLada** (password `smartlada`), open **http://192.168.4.1**.
   The page is fully self-contained (no internet/CDN).
3. 4 sliders — independent channel brightness; 0% = guaranteed physical off.
4. Per-channel calibration fields: `gamma` (1.8–2.6), `min_duty`/`max_duty` (0–1023),
   `soft_start_ms` (0–1000) — the "Apply" button.
5. Global: `pwm_freq_hz` (100–1000, default 150 — to find the softPWM ceiling) and the cap.
   "Remove cap" — a separate button with confirmation; the transition to full brightness
   goes through soft-start.
6. **Export JSON** — capture the current config for porting to the ESP32-C6.
   **Import JSON** — paste and apply a saved config.

Brightness mapping (order fixed):
`slider% → gamma → [min_duty..max_duty] → min(duty, cap) → soft-start ramp → PWM`.

**Config is not saved to flash** (intentional: force-safe start with defaults).
Save captured calibration numbers via Export JSON.

## Note on softPWM jitter

PWM on the ESP8266 is software-based. With active HTTP requests (slider movement, status
polling) brightness jitter is possible — that was an expected thing to check.

**Run result (2026-07-06):** no jitter observed by eye; the difference between 100 and
1000 Hz is indistinguishable — the thermal inertia of the filaments smooths everything.
100 Hz was chosen. Side observation: the switching PSU "sings" at the PWM frequency
(magnetostriction under pulsed load) — normal; higher frequency = higher and quieter tone.

## Architecture (layers)

```
SmartLada.ino        thin glue: force-safe start, setup/loop
src/channels/        [PORTED to C6]  pure duty math: gamma, min/max, cap, soft-start.
                     No Arduino/ESP8266/Wi-Fi includes at all.
src/config/          [PORTED to C6]  settings struct + JSON (pure C++).
src/platform_pwm/    [DISCARDED]     ESP8266 analogWrite*; on C6 → LEDC.
src/web/             [DISCARDED]     AP + HTTP + UI; on C6 → Zigbee.
src/display/         [DISCARDED]     OLED SSD1306 + FLASH button (HW-364A).
```

HTTP API: `GET /` (UI), `GET /config.json`, `POST /config` (same JSON),
`POST /set?ch=N&pct=P`, `GET /status` (MAC, heap, frequency, cap, percents).

## Next stage (outside this bench)

Zigbee firmware on the ESP32-C6 (`ZigbeeDimmableLight` / `HA_dimmable_light`),
4 dimmable endpoints, paired with a Yandex Station Midi. The `channels` layer and the
numbers from `config.json` carry over unchanged.
