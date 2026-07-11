# SmartLada C6 — hardware verification checklist (fw 0.7.0-c6)

Verification of the new functionality: BOOT button, master brightness across all modes,
gamma NeoPixel indication, DRIVE as a finite-state machine, editable timings.
For base calibration/power see the historical [CHECKLIST.md](CHECKLIST.md).

Fill in: **Actual** — measurement/result, **OK?** — `✅`/`❌`, **Notes** — what you
noticed. ⚠️ = critical.

| Field | Value |
|-------|-------|
| Test date | `` |
| Firmware | `0.7.0-c6` |
| Flash port | `` |
| Tested by | `saschapo` |

---

## 1. BOOT button — short press (mode switching)

| # | Check | Expected | Actual | OK? | Notes |
|---|-------|----------|--------|-----|-------|
| 1.1 | Short press cycles modes | MANUAL→TURN→CHASE→SEQ→PULSE→ALT→DRIVE→MANUAL | | | |
| 1.2 | Mode on the web UI updates in step with presses | active-mode highlight in sync | | | |
| 1.3 | NeoPixel color changes with the active mode | visible feedback | | | |
| 1.4 | Entering MANUAL turns on all 4 lamps (at the current master brightness) | all lit together | | | |
| 1.5 | Debounce: one press = one step | no "double" jumps | | | |

## 2. BOOT button — dimmer (hold ≥0.5 s)

| # | Check | Expected | Actual | OK? | Notes |
|---|-------|----------|--------|-----|-------|
| 2.1 | Hold starts a smooth dimmer | brightness ramps smoothly | | | |
| 2.2 | Full travel 0↔100% ≈ 5 s | time it with a stopwatch | | | |
| 2.3 | Direction alternates from hold to hold | down → up → down … | | | |
| 2.4 | At a limit (0/100%) the next hold goes back into range | no "empty" hold | | | |
| 2.5 | Dimming to 0% fully turns the lamp off | lamps off | | | |
| 2.6 | From 0% a hold brings brightness back up | smoothly up | | | |
| 2.7 | Press/hold threshold (~0.5 s) feels right | no accidental dimming on a click | | | |

## 3. Master brightness across all modes

| # | Check | Expected | Actual | OK? | Notes |
|---|-------|----------|--------|-----|-------|
| 3.1 | Hold during an animation (TURN/CHASE/SEQ/ALT) dims the whole scene | animation continues but dimmer | | | |
| 3.2 | Hold in PULSE scales the amplitude | breathing dims/brightens | | | |
| 3.3 | Hold in DRIVE dims all signals | scenario runs, overall brightness lower | | | |
| 3.4 | Master level persists across mode changes | brightness doesn't jump | | | |
| 3.5 | Startup = 100% (animations visible immediately) | not a dark screen | | | |

## 4. NeoPixel — brightness indication while dimming

| # | Check | Expected | Actual | OK? | Notes |
|---|-------|----------|--------|-----|-------|
| 4.1 | 0% marked with bright red | clear red | | | |
| 4.2 | 100% marked with full green | clear green | | | |
| 4.3 | Middle — white with gamma correction | brightness changes evenly to the eye | | | |
| 4.4 | ⚠️ Range 70–100% is now distinguishable (not "the same") | steps visible | | | |
| 4.5 | Indicator fades ~0.5 s after release | returns to normal indication | | | |

## 5. DRIVE — finite-state machine (transition logic)

Watch for 3–5 min, note any logic violations.

| # | Check | Expected | Actual | OK? | Notes |
|---|-------|----------|--------|-----|-------|
| 5.1 | ⚠️ There is always a stop before reverse | REVERSE only after STOP (stop lamp on, standing) | | | |
| 5.2 | ⚠️ Slowing down before a turn | TURN only after BRAKE/BRAKE_TURN/STOP, not "on the fly" from CRUISE | | | |
| 5.3 | In REVERSE the stop lamp is STEADY (not blinking) | steady light + reverse | | | |
| 5.4 | Turn signal/hazard blinks at 1.5 Hz | 333/333 ms | | | |
| 5.5 | Marker — background while moving, 100% while braking | mark=35%, stop→100% | | | |
| 5.6 | Parking (all off) only after a stop | PARKED from STOP/HAZARD | | | |
| 5.7 | Timeline doesn't repeat, looks "alive" | no looped sequence | | | |
| 5.8 | Phase durations within the set ranges | plausible by eye | | | |
| 5.9 | No "getting stuck" in one state | scenario keeps developing | | | |

## 6. Web — editable timings and new ranges

| # | Check | Expected | Actual | OK? | Notes |
|---|-------|----------|--------|-----|-------|
| 6.1 | CHASE/SEQ/PULSE/ALT fields visible and populated with current values | pulled from /config.json | | | |
| 6.2 | Changing CHASE applies on the fly | "run" speed changes | | | |
| 6.3 | Changing SEQ applies | fill step changes | | | |
| 6.4 | Changing PULSE applies | breathing period changes | | | |
| 6.5 | Changing ALT applies | even/odd step changes | | | |
| 6.6 | Values outside 100–10000 are clamped | doesn't break, returns into range | | | |
| 6.7 | PWM is now limited to 10–30000 (field and clamp) | >30000 not accepted | | | |
| 6.8 | soft_start accepts up to 5000 ms | large ramp works | | | |
| 6.9 | Export/Import JSON contains the new fields | chase_ms/seq_ms/pulse_ms/alt_ms in JSON | | | |
| 6.10 | Timings survive a reboot (in firmware defaults) | values correct after reboot | | | |

## 7. Regression (did anything basic break)

| # | Check | Expected | Actual | OK? | Notes |
|---|-------|----------|--------|-----|-------|
| 7.1 | ⚠️ Power-cycle → all lamps OFF at startup | force-safe holds | | | |
| 7.2 | Web channel sliders and master work | as before | | | |
| 7.3 | duty 0 = full OFF | filaments don't glow | | | |
| 7.4 | NeoPixel: green with a client / blue breathing without a client (MANUAL) | normal indication intact | | | |
| 7.5 | Indication frame rate ~60 fps (UPDATE_MS=17) | smoother, no flicker | | | |

---

**Overall conclusion:** ``
