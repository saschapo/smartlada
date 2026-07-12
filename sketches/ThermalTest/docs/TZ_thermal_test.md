# TZ — ThermalTest: DS18B20 validation bench (ESP8266 / HW-364A)

Standalone test sketch that validates two DS18B20 sensors (PSU, BODY) and the
supporting plumbing (phone time-sync, event log, web view) **before** the thermal
layer is ported into `SmartLadaC6`. It does **not** drive lamps/PWM.

> A third DS18B20 for the ESP point was dropped: on the C6 target that reading
> comes from the internal die sensor, not a DS18B20. Room temperature (~22.5 °C
> measured on the bench) is taken as the **ambient reference**.

- **Board:** ESP8266 HW-364A (ESP-12F + onboard SSD1306 OLED + FLASH button).
- **Sketch path:** `sketches/ThermalTest/` (thin `.ino` + `src/*/{h,cpp}` layers,
  mirroring `sketches/SmartLada/`).
- **Design lineage:** OLED/button pattern from `sketches/SmartLada/src/display/`;
  time-sync + event log + dark web view ported from the `cineink` project
  (`firmware/cineink/{rtc,log,web}.cpp`, `web_view/device.css`).

> Language: all code, comments and docs in English (project convention).

---

## 1. Goal & scope

**In scope (v1):**
- Read 3× DS18B20 on one 1-Wire bus, non-blocking, 12-bit.
- Show live temperatures on the OLED (paged, FLASH button cycles pages).
- Serve a dark cineink-style web page with the 3 live values + wall clock.
- Sync wall clock from the phone browser on page load (`POST /api/time`).
- Persist a rolling event log on LittleFS (cineink mechanics) with temperature
  samples carrying real time; downloadable/clearable from the web.

**Out of scope (v1):** any control action (no PWM, no thermal derating), sensor
calibration, plotting. The C6 port replaces OLED with web + NeoPixel.

---

## 2. Hardware

| Node | Value | Notes |
|---|---|---|
| MCU | ESP8266 (HW-364A) | ESP-12F |
| OLED | SSD1306 128×64, I²C **SDA=GPIO14, SCL=GPIO12**, `0x3C` | verified pins, see SmartLada bench |
| Button | FLASH = **GPIO0** (LOW = pressed) | cycles OLED pages; 30 ms debounce |
| **1-Wire bus** | **GPIO13 (D7)** | non-strapping; broken out on the HW-364A header (used as a channel in the SmartLada bench) |
| Sensors | 2× DS18B20, 12-bit, normal (non-parasite) power | unique 64-bit ROM each |
| Pull-up | one **4.7 kΩ** DATA → 3.3 V for the whole bus | not one per sensor |
| Power | 3.3 V, common ground | |

**Mounting (sensor reads its own case → thermal contact matters):**
- **PSU** — thermal-glue/clamp to the DC-DC case near the hot spot.
- **BODY** — tail-light housing near the lamps.

Bench addresses (from `dumpBus`, provisional PSU/BODY assignment — heat one point
to confirm, swap the two lines in `CONFIGURED[]` if needed):
`psu = 28 1D 1B 74 00 00 00 A5`, `body = 28 DF 2D 6D 00 00 00 E7`.

---

## 3. Firmware layers

Thin `ThermalTest.ino` + these `src/` layers:

### 3.1 `src/thermal/{thermal.h, thermal.cpp}`
- Libraries: `OneWire` + `DallasTemperature`.
- 12-bit resolution; **non-blocking** state machine in `tick(now)`:
  `requestTemperatures()` (async, `setWaitForConversion(false)`) → wait ~750 ms
  → read. Live sample period **2 s**.
- Hard-coded ROM→label table (like `CHANNEL_DEFS`): index 0..2 → `psu/body/esp`.
- Fault handling: a reading outside the DS18B20 spec range (−55…125 °C) →
  **not present** (UI shows `—`). This covers `DEVICE_DISCONNECTED_C` (−127); the
  full 750 ms conversion wait rules out the false `85.0` power-on value.
- API:
  ```cpp
  namespace thermal {
    void  begin();                 // bus init, 12-bit, async mode
    void  tick(uint32_t now_ms);   // request → wait → read
    float celsius(uint8_t idx);    // NaN if !present
    bool  present(uint8_t idx);
    void  dumpBus(Print& out);     // list all found ROM addresses + temps
  }
  ```
- Bring-up: `dumpBus(Serial)` at boot prints found addresses so the ROM→label
  table can be populated. Acceptance expects **exactly 3** addresses, CRC ok.

### 3.2 `src/rtc/{rtc.h, rtc.cpp}` (port of cineink `rtc`)
- `setLocalEpoch(uint32_t)`, `isSet()`, `format(out,n,withDate)`.
- Stores the phone's **local** epoch via `settimeofday()`; `gmtime_r()` then
  yields local wall-clock. `isSet()` = clock ≥ 2021-01-01 (distinguishes
  "set by browser" from cold boot). Works on ESP8266 `time.h`.

### 3.3 `src/log/{log.h, log.cpp}` (port of cineink `log`, simplified)
- LittleFS `/log.txt`, rotates at **512 KB** → `/log.bak.txt` (one backup kept).
- **ESP8266 deltas vs cineink:** no FreeRTOS mutex (single core);
  `LittleFS.begin()` (no format-arg); no panic-breadcrumb subsystem.
- Macros `LOGI/LOGW/LOGE`.
- **Line format** (unified rule for the whole log): prefix is wall-clock
  `HH:MM:SS` when `rtc::isSet()`, else `<sec>s` (`millis()/1000`). Body keeps
  cineink's `<LEVEL> <TAG>: <msg>`. Session anchor `==== boot @ <ts> ====` and a
  one-shot `clock set <ts>` when the phone first sets the time.

### 3.4 Temperature logging policy
Live reading (2 s) is separate from what is persisted. A line is written when:
1. **Control stamp — every 60 s** (always, even if steady).
2. **Change trigger** — any sensor's current reading differs from its
   **last-logged** value by **> 2.0 °C** (up or down); the compared baseline for
   that sensor is then updated. Change-triggered lines use the tag `TEMP*` (ASCII
   only — no non-standard glyphs in the log).

Line examples (both values per line):
```
20:15:03 I TEMP: psu=47.2 body=39.1
20:15:31 I TEMP*: psu=49.4 body=39.3
20:16:03 I TEMP: psu=49.6 body=39.2
```
Before the phone sets the clock the prefix falls back to `<sec>s`.

### 3.5 `src/stats/{stats.h, stats.cpp}`
Fed from the main loop (reads the thermal layer directly). Keeps, per sensor,
running **min / max / avg** since boot (valid readings only) plus a short ring
buffer of recent samples (5 entries at 30 s) for the history page. Pure bookkeeping
— no display or storage dependency.

### 3.6 `src/display/{display.h, display.cpp}` (OLED)
FLASH button cycles **5 pages** (pattern from the SmartLada bench). This board is a
two-colour panel — **top 16 px yellow, rest blue** — so page content keeps the
yellow strip for the title/clock and all data rows sit at `y ≥ 16` (blue).
1. **Temps** — yellow strip: `THERMAL  HH:MM:SS`; blue: two big rows
   `PSU 47.2` / `BODY 39.1` (`--` for a missing sensor).
2. **Status** — IP, SSID, clients, free heap, log size, `clock: set/unset`.
3. **Sensors** — ROM address tails + present/CRC per sensor.
4. **History** — table of recent samples (age / psu / body), 30 s cadence,
   newest first; `collecting...` until the first entry.
5. **Min/Max/Avg** — per-sensor min/max/avg since boot + sample count and uptime.

### 3.7 `src/web/{web_server.h, web_server.cpp}`
SoftAP + HTTP. Endpoints:
| Method | Path | Purpose |
|---|---|---|
| GET | `/` | dark SPA (cineink style) |
| GET | `/api/state` | JSON: `{temp:[..], present:[..], time, clients, heap, logBytes}` |
| POST | `/api/time` | phone hands local epoch (`epoch=…`) |
| GET | `/log`, `/log.1` | download current / backup log (dated filename) |
| POST | `/clearlog` | wipe both log files (confirm-guarded) |

Web view: black bg, system font, uppercase letter-spaced labels, `.btn/.line/
.danger` buttons, `.ok/.warn/.err` colors, 440 px width (from
`cineink/web_view/device.css`). Live values poll `/api/state` every 2 s. On page
load the SPA fires the one-shot `POST /api/time` with
`epoch = Math.floor(Date.now()/1000) − new Date().getTimezoneOffset()*60`.

### 3.8 `ThermalTest.ino`
Thin: `Serial.begin`, `LittleFS`/`log::begin`, `rtc` implicit, `thermal::begin`,
`thermal::dumpBus(Serial)`, `stats::begin`, SoftAP + `web::begin`, `display::begin`.
In `loop()`: `web::handle()`, `thermal::tick(now)`, `stats::tick(now)`,
log-policy tick, `display::tick(now)`.

---

## 4. Acceptance

1. `arduino-cli compile` passes (build-before-upload rule). ✅
2. `dumpBus(Serial)` finds **both** addresses, CRC ok. ✅ (`28…A5`, `28…E7`)
3. Two plausible values on OLED **and** in `/api/state`; they diverge under
   local heating (finger/hot-air on one sensor).
4. Unplugging one sensor → `present=false`, UI/OLED `—`, firmware does not hang,
   the other keeps reading.
5. Phone loads the page → log shows `clock set <ts>`, boot header carries the
   date, downloaded file name is dated.
6. Log policy verified: 60 s control stamps present; a `Δ` line appears within
   one sample after a >2 °C swing; rotation at 512 KB works.
7. `loop()` never blocks (OLED refresh + web stay responsive during the 750 ms
   conversion window).

---

## 5. Risks

- Wrong ROM→point mapping → mitigated by `dumpBus` + labelling the pigtails.
- Poor thermal contact → low/lagging readings (esp. PSU/board points).
- Long/branched 1-Wire runs → keep normal power + correct pull-up, no star topology.
- `85.0` raw value mistaken for a real reading → explicitly filtered as not-present.

---

## 6. Port to SmartLadaC6 (after validation)

`thermal` + `rtc` + `log` layers and the `/api/time` `/log` `/clearlog`
endpoints port almost verbatim. On C6: bus on **GPIO10** (confirmed on the
nanoESP32-C6 header); **2×** external DS18B20 (PSU, BODY) + the **C6 internal
sensor** for the die (`temperatureRead()`), reported as `esp (die)`; the third
DS18B20 stays in spares. OLED/button are test-only and are replaced by the web
UI + NeoPixel indication. `/status` JSON gains `temp:[psu,body,esp]`.
