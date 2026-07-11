# DRIVE animation mode ("driving simulation")

Status: **implemented in fw 0.7.0-c6** (`src/anim/anim.cpp` → mode `DRIVE`, id 6).
Goal — a scenario mode that simulates a real driving session through the lamp's
signals as a coherent "play", not a periodic pattern.

> **History:** in fw 0.4.0 DRIVE was a fixed list of 9 phases cycled in order. In
> 0.7.0 it was rewritten as a **finite-state machine** — an endless, non-repeating
> but logical timeline.

## 1. Lamp roles (2106 tail light)

| Channel | Role |
|---------|------|
| `marker` (CH3) | marker / parking — background while moving (35%), 100% together with STOP |
| `stop` (CH0) | brake light (brake pedal) |
| `turn` (CH2) | turn signal / hazard (blink 1.5 Hz) |
| `reverse` (CH1) | reverse gear |

**Marker rule:** `mark = (stop>0) ? 100 : 35`. The marker mirrors stop (combined
marker/stop); when parked it is 0.

## 2. Model: finite-state machine

Instead of a fixed list — a **weighted random walk over a state graph** (Markov
chain). Each state = a set of lamps + a random duration from its own range. When the
duration elapses, the next state is chosen from the allowed ones (table `ADJ`, weighted
via `esp_random`). This yields an endless non-repeating scenario; the logic is
guaranteed by the **graph structure**, not by a script.

Only `turn` blinks (on the common `TURN_BLINK_MS = 333` ms beat). `stop` and `reverse`
are always steady (including the stop light during reverse — constant, not pulsed).

## 3. States (8)

| State | stop | turn | reverse | marker | Duration, ms |
|-------|:----:|:----:|:-------:|:------:|--------------|
| CRUISE (driving steady) | – | – | – | 35% | 3000–12000 |
| BRAKE (braking) | 100 | – | – | →100 | 1500–3500 |
| STOP (standing) | 100 | – | – | →100 | 1200–2500 |
| TURN (turning while moving) | – | blink | – | 35% | 1500–3000 |
| BRAKE_TURN (braking + turning) | 100 | blink | – | →100 | 1000–1500 |
| REVERSE (reverse gear) | **100 steady** | – | 100 | →100 | 2000–6000 |
| HAZARD (hazard lights) | – | blink | – | 35% | 2000–6000 |
| PARKED (parked) | – | – | – | 0 | 2000–4000 |

The mode starts from `STOP` (the car stands, then pulls away).

## 4. Transition graph

| From | Can go to (weight) |
|------|--------------------|
| CRUISE | BRAKE (3), BRAKE_TURN (2) |
| BRAKE | STOP (3), TURN (2), CRUISE (2) |
| STOP | CRUISE (2), TURN (2), REVERSE (1), HAZARD (1), PARKED (1) |
| TURN | CRUISE (3), BRAKE (1) |
| BRAKE_TURN | CRUISE (2), TURN (1), STOP (1) |
| REVERSE | STOP (2), CRUISE (1) |
| HAZARD | PARKED (2), STOP (1), CRUISE (1) |
| PARKED | CRUISE (1) |

## 5. Logical guarantees (baked into the graph)

- **Reverse only after a full stop.** `REVERSE` is reachable **only from `STOP`**, and
  `STOP` is reachable only after braking → always "braked → stopped → reverse".
- **Turn only after slowing down.** `TURN` is **not reachable directly from `CRUISE`** —
  only via `BRAKE`/`BRAKE_TURN`/`STOP`.
- **Parking only after stopping** — `PARKED` is reachable only from `STOP`/`HAZARD`.
- **No self-repeats** (no edge points to itself) → the timeline keeps changing.
- **Never stuck** — `PARKED → CRUISE` restarts the trip.

## 6. Integration

- `anim::Mode::DRIVE` (id 6). Tables `DSTATE[]` (durations + lamps), `ADJ[]` (graph),
  functions `pickDur()` / `pickNext()` in `anim.cpp`.
- Channels are driven through the shared `setPercent()` → soft-start gives a natural
  "warm-up" of the lamps.
- Respects the global cap and calibration (min_duty/gamma/master), like the other modes.
- Phase timings are its own (independent of the web CHASE/SEQ/PULSE/ALT timings).

## 7. NeoPixel in DRIVE (`status.cpp`)

Full sync with the lamp, priority **turn > reverse > stop > marker**:
turn/hazard → yellow (blinking), reverse → white, stop → red, driving with marker →
**steady dim white** (in 0.7.0 this replaced the "breathing").
