# SmartLada C6 — bench verification checklist

Fill in as you go: the **Actual** column — write the measurement/result, **OK?** —
`✅`/`❌`, **Notes** — what you noticed. ⚠️ = critical.

| Field | Value |
|-------|-------|
| Test date | `10 July 2026` |
| Firmware (date/commit) | `no version (add versions)` |
| Port | `what does this field mean?` |
| Tested by | `saschapo` |

---

## 1. PWM frequency

| # | Check | Expected | Actual | OK? | Notes |
|---|-------|----------|--------|-----|-------|
| 1.1 | Flicker threshold from below | steady from ~60–100 Hz | `no flicker even at 10 Hz (the camera does catch it slightly; on camera the strobe is gone above 1000 Hz)` |✅ | |
| 1.2 | ⚠️ D4184 heating at 20–78 kHz under load | switches don't heat up | `temperature unchanged` | ✅| |
| 1.3 | Quietest frequency (PSU whine / buzz) | — | `my ear hears the whine even at 25000 Hz; at 30000 Hz it's gone` |✅ | |
| 1.4 | `applied` in Serial near the ceiling (request 78000) | ≈78125 Hz | `not tested, and not needed` |✅ | |
| 1.5 | Chosen working frequency | — | `30000 Hz` |✅| |

## 2. Calibration / brightness (cap removed, max_duty=100)

| # | Check | Expected | Actual | OK? | Notes |
|---|-------|----------|--------|-----|-------|
| 2.1 | ⚠️ duty 0 = full OFF on all channels | filaments don't glow | `checked with a voltmeter: at duty=0 the voltage is 0V; at 1% duty and min_duty=20 the lamps read about 230mV each` | ✅| |
| 2.2 | Brightness linearity across the whole slider | smooth 0→100% | `visually linear` | ✅| |
| 2.3 | `min_duty` CH0 stop (glow threshold) | bottom of slider visible | `min=20` | ✅| |
| 2.4 | `min_duty` CH1 reverse | —//— | `min=20` | ✅| |
| 2.5 | `min_duty` CH2 turn | —//— | `min=20` |✅ | |
| 2.6 | `min_duty` CH3 marker | —//— | `min=20` | ✅| |
| 2.7 | gamma: 4 channels subjectively equal | even | `γ=1.9` | ✅| |
| 2.8 | Export JSON saved | file captured | `json export/import works` | ✅| |

## 3. Power / heat (lamps under 12 V)

| # | Check | Expected | Actual | OK? | Notes |
|---|-------|----------|--------|-----|-------|
| 3.1 | 12 V sag with all 4 at 100% | ≈12 V, no collapse | `see notes` |✅ | `PSU output no-load — 11.23V, under 100% load (all lamps on) — 11.23V`|
| 3.2 | Ecola case temperature after 5 min | warm, not hot | `case barely warmed, slightly above room temperature` | ✅| |
| 3.3 | D4184 switch temperature | cold | `cold` | ✅| |
| 3.4 | Simultaneous channel start 0→100% | PSU without hiccup | `no problems or glitches`| ✅| |
| 3.5 | Min `soft_start_ms` without the PSU tripping | — | `PSU doesn't trip even when constantly hard-toggling all lamps on/off; it doesn't care about soft_start at all` | ✅| |

## 4. ⚠️ Safety (lamps connected)

| # | Check | Expected | Actual | OK? | Notes |
|---|-------|----------|--------|-----|-------|
| 4.1 | ⚠️ Power-cycle the whole bench → lamps OFF at startup | all off | | ✅| |
| 4.2 | Fuse on 12 V = T4A slow-blow | present | `fuse removed, relying on PSU protection`| ✅| |
| 4.3 | Common ground C6 ↔ PSU GND | connected | | ✅| |
| 4.4 | Gate pulldown on each D4184 | present | |✅ | |

## 5. System

| # | Check | Expected | Actual | OK? | Notes |
|---|-------|----------|--------|-----|-------|
| 5.1 | HTTP flood (fast slider) — no crash | stable | |✅ | `text fields apply slowly (about 2-3 s), json imports in about 3 s, while slider responsiveness is basically realtime`|
| 5.2 | NeoPixel: amber ∝ brightness | tracks | | ❌| `the visualization is awkward right now — it shows the sum of brightnesses, and if one lamp -> 100% it won't get brighter from the others and gets stuck at its max.`|
| 5.3 | NeoPixel: green with a client, channels 0 | ok | | ✅| |
| 5.4 | NeoPixel: blue "breathing" with no client | ok | | ✅| |
| 5.5 | AP survives phone sleep / reconnect | ok | | ✅| |

---

## Final calibration (transfer to Export JSON)

```json
{
  "pwm_freq_hz": 30000,
  "max_duty_cap_pct": 100,
  "channels": {
    "stop": { "gpio": 0, "gamma": 1.90, "min_duty": 20, "max_duty": 1023, "soft_start_ms": 30 },
    "reverse": { "gpio": 1, "gamma": 1.90, "min_duty": 20, "max_duty": 1023, "soft_start_ms": 30 },
    "turn": { "gpio": 2, "gamma": 1.90, "min_duty": 20, "max_duty": 1023, "soft_start_ms": 30 },
    "marker": { "gpio": 3, "gamma": 1.90, "min_duty": 20, "max_duty": 1023, "soft_start_ms": 30 }
  }
}
```

**Overall conclusion:** `the device works, passed almost all the tests in the table; the
remaining issue is the PSU whine at pwm freq below 30000 Hz (likely fixed by swapping the
PSU for a better one). Also need to rethink the NeoPixel behavior, rework the web view
with the taste-skill (take only the essential fixes), add a Master Slider, add a Master
On/Off button, add a few lamp-animation scenarios (e.g. a turn-signal imitation, or
sequential switching of each lamp — need about 5 different modes to test dynamic PWM
switching; these can be technical tests, not visually pretty ones)`
