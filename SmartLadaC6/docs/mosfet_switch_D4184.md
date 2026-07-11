# Power switch — D4184 module (low-side)

Each of SmartLada's 4 channels is switched by an off-the-shelf **"High power MOS
Driver/Switch"** module built on a dual N-channel MOSFET **D4184** (low-side, current
sink). The ESP32-C6 feeds an LEDC signal into the `TRIG/PWM` input, and the module
switches the lamp's negative to ground.

## Module datasheet

| Parameter | Value |
|-----------|-------|
| Transistors | 2× **D4184** (N-channel), wired **in parallel** |
| Switch type | **Low-side** (current sink), `+Vin` and `+Vout` tied on the board |
| Load supply | 5–36 V DC |
| Current | 15 A (400 W); up to 30 A with a heatsink |
| Trigger level | 3.3–20 V, **logic** (3.3 V from ESP32-C6 turns it on) |
| Rds(on) | ~0.007 Ω |
| **Max PWM frequency** | **20 kHz** *(see warning below)* |
| Operating temp | −40…+85 °C |

## Schematic

**Input (gate) network.** Between `TRIG/PWM` and the gate there is a 100 Ω (gate
charge-current limit). At the gate: a **100K pull-down** (holds the switch off while
no input is applied) and a **1.0K + LED** indicator (lit when the switch is on).

```
  ESP32-C6         100 Ω          gate_node ── gates of D4184 ×2
  GPIOx ──[TRIG]───[███]──────────┬──────────┬─────────────
 (LEDC PWM)                       │          │
                               [100K]     [1.0K]
                             pull-down       │
                                  │         LED (ON indicator)
                                  │          │
  GND ────────────────────────────┴──────────┴──────────────  VIN− / GND
```

**Power path (low-side).** The lamp's positive sits on the common +12 V (jumper
`+Vin = +Vout` on the board), and the lamp's negative (`VOUT−`) is switched to ground
by the transistor:

```
                       +12 V  (Ecola B2L080ESB, DC · fuse T4A slow-blow)
                          │
          ┌───────────────┴───────────────┐
       VIN+                            VOUT+   ← board jumper: +Vin = +Vout
          │                               │
      [ module ]                      ┌────┴────┐
          │                           │  R10W   │  channel lamp
          │                           │  12 V   │  (stop / reverse / turn / marker)
          │                           └────┬────┘
          │                                │ VOUT−  ← lamp negative is switched
          │                           drain │
          │                           ┌─────┴─────┐
          │                           │ D4184 ×2  │  N-channel, in parallel
          │                           └─────┬─────┘
          │                          source │
       VIN− ─────────────────────────────────┴──────  GND
          │
  GND ────┴────────────────────  common ground with ESP32-C6 (PWM reference, required)
```

## Project mapping

| Item | Value | Source |
|------|-------|--------|
| `TRIG/PWM` inputs | `GPIO0..3` (CH0 stop, CH1 reverse, CH2 turn, CH3 marker) | `src/config/config.cpp` |
| PWM driver | hardware LEDC, 10 bit | `src/platform_pwm/` |
| Default frequency | 30,000 Hz | `FREQ_DEFAULT` |
| Lamps | R10W 12 V BA15S, 40 W / 3.33 A total | README |
| +12 V source | Ecola B2L080ESB (DC, 80 W, 6.6 A, short-circuit protection) | README |

Why low-side works: the source is a regulated DC block (not an electronic
transformer), so switching the negative is correct. The lamps are **re-polarized to a
common +12 V** — which is exactly what the low-side module requires (`+Vin = +Vout`).

## ⚠️ Warnings

- **PWM frequency above the rated 20 kHz — but not a problem at this load.**
  Firmware default is 30 kHz (also the setting ceiling), the module is rated max 20 kHz.
  The rating limit comes from switching losses (weak gate drive: 100 Ω from 3.3 V, no
  gate driver), but those losses are **linear in current**, and the current here is
  small: R10W = ~0.83 A/channel, split across two parallel D4184 (~0.42 A per MOSFET).
  Estimated per-channel loss at 30 kHz: conduction ~0.003 W, switching ~0.15–0.45 W —
  i.e. ~0.1–0.2 W per MOSFET, which the package dissipates without heating. Confirmed:
  **3 h of continuous 30 kHz operation — neither the MOSFETs, the lamp, the ESP32, nor
  the PSU heat up** (2026-07-11); thermal equilibrium is reached in minutes, so this is
  steady state. The 20 kHz rating would matter only with a substantial rise in
  per-channel current (higher-power lamps, LED strips, many channels at 100% at once) —
  then watch the temperature or lower `pwm_freq_hz`.
- **The gate pull-down is mandatory** and already present on the module (100K). It
  holds the switch off while `GPIO0..3` are not yet configured at boot. On first
  power-up, verify "duty 0 = lamp off". The firmware also does `forceAllLow()` before
  any init.
- **Common ground** ESP32-C6 ↔ PSU GND — the PWM reference; without it the switch is
  unstable.
- **Fuse** on +12 V — **T4A slow-blow** (rated 3.33 A, tolerates the cold-filament
  soft-start inrush of ~8–10 A/lamp; do not set `soft_start_ms=0` with these lamps).

---

**Data source:** the off-the-shelf "High power MOS Driver/Switch (D4184×2)" module
datasheet + project wiring/warnings (README.md, `src/config/config.cpp`).
