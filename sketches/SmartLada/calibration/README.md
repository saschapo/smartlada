# Bench calibration results

`2026-07-06_bench.json` — captured on real VAZ-2106 tail light lamps, tuned by eye.

## Run observations (2026-07-06)

- **Flicker:** the difference between 100 and 1000 Hz is invisible to the eye — the
  thermal inertia of the filaments smooths PWM completely. 100 Hz was chosen.
- **Glow threshold:** `min_duty` 50 (21 W) / 100 (5 W marker) — below that the filament
  visibly doesn't light. The thin marker filament needs a larger share of the scale.
- **Gamma:** 2.0 for the 21 W lamps, 2.2 for the 5 W marker.
- **Cap removed** (100%) deliberately after testing; the cap is not carried over to C6.
- **Tail-light wiring:** the common tail-light board is re-polarized to +12 (common
  positive), individual socket contacts → D4184 drains (separate switched terminals).
  Incandescent lamps don't care about polarity. The wiring is verified — channels are
  fully independent.

## On soft_start_ms = 30 (a deliberate decision)

The cold-filament inrush (~10× nominal for the first tens of ms) is dangerous not to the
lamp (automotive lamps are designed for cold starts — there is no soft-start in a car)
and not to the switch (the D4184 handles tens of amps), but to the switching PSU
(over-current protection tripping) and the fuse (false blows). The bench showed
empirically: the 100 W PSU digests a start with a 30 ms ramp without the cap — protection
does not trip. So 30 ms is validated by hardware; longer is not needed. Caveat: recheck
this if the PSU is swapped for a weaker/more nervous one.

## For porting to the ESP32-C6

- The `gamma` / `min_duty` / `max_duty` / `soft_start_ms` numbers — into the `channels`
  layer unchanged.
- `gpio` — a bench binding (ESP8266/HW-364A); on the C6 the pins will be different.
- `pwm_freq_hz: 100` — chosen for the ESP8266 softPWM; LEDC on the C6 is hardware, and
  the frequency is visually irrelevant (100–1000 Hz identical), so it can be raised for a
  quieter-singing PSU.
