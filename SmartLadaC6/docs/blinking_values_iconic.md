# Classic Automotive Turn Signal Timing

## Canonical ("iconic") timing

The timing most people instinctively recognize as a standard automotive turn signal.

| Parameter | Value |
|-----------|------:|
| ON time | **333 ms** |
| OFF time | **333 ms** |
| Full cycle | **666 ms** |
| Frequency | **1.5 Hz** |
| Duty cycle | **50%** |

## Alternative classic values

Electromechanical flasher relays vary slightly.

| ON | OFF | Cycle | Frequency |
|---:|----:|------:|----------:|
| 320 ms | 320 ms | 640 ms | 1.56 Hz |
| 330 ms | 330 ms | 660 ms | 1.52 Hz |
| **333 ms** | **333 ms** | **666 ms** | **1.50 Hz** |
| 350 ms | 350 ms | 700 ms | 1.43 Hz |
| 375 ms | 375 ms | 750 ms | 1.33 Hz |

## Hazard lights

Hazards use **exactly the same timing** as the turn signal.

- ON: **333 ms**
- OFF: **333 ms**
- Cycle: **666 ms**
- Frequency: **1.5 Hz**

---

**Use in firmware:** `anim.cpp` → `TURN_BLINK_MS = 333` (half-period) for the TURN
mode, ALT-like blinking, and all DRIVE blink phases (turn signal / hazard). The
alternative values above are for eyeballing; they change with a single constant.
