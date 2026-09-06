# SmartLadaRevC — compact review handoff

Reviewed 2026-09-06: **1.0.6-revC**, working tree including uncommitted changes; Arduino-ESP32 **3.3.10**. Findings are review evidence, not instructions to implement every suggestion. Original scope: report only; no firmware edits or flashing. Follow the user's current task. Revalidate affected code before editing; line numbers describe this snapshot. Read targeted functions first; historical docs contain obsolete requirements.

**Evidence:** HOST = reproduced using actual project C++ with simulated hardware/time/Preferences; STATIC = source/library analysis, not device reproduction. No hardware/radio/electrical tests performed. P1 = prioritize; P2 = functional/reliability issue; P3 = minor. Hardware consequences remain unmeasured.

**Paths:** relative to `SmartLadaRevC/` (parent of this review directory). `INO=SmartLadaRevC.ino`, `M=src/ui/menu.cpp`, `Z=src/net/zigbee.cpp`, `B=src/net/bleota.cpp`, `C=src/channels/channels.cpp`, `F=src/fx/effects.cpp`, `K=src/input/buttons.cpp`, `CFG=src/config/config.cpp`.

## Preserve this control model

- Indoor decorative lamp, not automotive safety firmware. ESP32-C6-WROOM-1-N16, USB-C PD 12 V, incandescent loads and MCU buck share VBUS. Prior cold-start brownouts documented.
- Channels/GPIO: turn=0, marker=1, reverse=2, stop=3. PCB Fastons OUT0/OUT1 cross GPIO1/GPIO0; do not revert the function-based GPIO map.
- `config::s`: shared mutable state; menu runs in loop, Zigbee callbacks in another task. Intended last-writer-wins is not sufficient synchronization.
- `mode`: 0 Static; 1 Breathe; 2 Turn; 3 Chase; 4 Fade; 5 Drive.
- Static: `out[i] = lampOn[i] ? staticBri[i] : 0`; **no master multiplier**. Effects render all channels using `master`, intentionally ignoring static `lampOn/staticBri`.
- EP10–13 change static levels/on-mask, never mode; Level alone must not enable an off channel.
- EP14 Fara: off → Static; on+white (`sat<40`) → Static + level fan-out; on+color → hue-selected effect + effect brightness. Fara off does not turn all lamps off. Local static master enables all lamps and fans out brightness.
- Output: exponential smoothing → shared gamma/min/max LUT → 10-bit LEDC. `softMs` is τ, not ramp completion time. Static uses configured τ; effects use 20 ms; boot uses 1500 ms temporarily.
- Power gate: ADC GPIO4, divider 100k/33k, hysteresis off<7 V/on>8 V; PG GPIO10 diagnostic. Force Out bypass resets on reboot.
- BLE starts only in dedicated OTA boot mode; Zigbee does not start there. RTC request flag selects mode. Two OTA app partitions exist.

## Findings / acceptance targets

### R1 — P1 / HOST: remote effect change invalidates Timings editor

`M:650–668 SC_TIMINGS; M:822 notifyExternalChange`.
Repro: Chase → Timings → edit Fade (`cursor=1, editing=true`); change mode remotely to Drive or Turn. Redraw notification preserves editor state. Drive has `params=nullptr`; Turn has one param but `cursor=1` passes `cursor<nItems` because nItems includes Reset. `e.params[cursor]` reads invalid memory even before the next adjustment; adjustment can write it.
HOST: Drive → UBSan null reference + SEGV; Turn → ASan global-buffer-overflow.
Target: end/revalidate editing when mode changes; coherent mode snapshot; enforce `params!=nullptr && cursor<nparams` at access. Test remote changes during editing, including effects with zero/fewer params. Reset row is never a Param.

### R2 — P1 / HOST: boot smoothing includes pre-ramp initialization time

`C:40–69 begin/write; INO:65–86 setup`.
`s_lastMs` is set in channels::begin, before OLED/NVS/Zigbee initialization. Setting boot τ does not reset this clock. First write uses the entire initialization delay. Zigbee.begin can wait for stack startup; network joining later continues in background.
HOST counterexample: 2000 ms delay, target=255, τ=1500, default calibration → first write logical=187.78, PWM=578/1023 (56.5%), from previously zero output. **2 s is injected, not measured startup latency.** Brownout recurrence is a risk, not reproduced.
Target: initialize ramp time/state at actual ramp start; verify first PWM after short/long initialization. Also test 12 V arriving after setup: boot ramp is not restarted then. Document τ correctly: one τ reaches ~63% of logical target, not 100%.

### R3 — P2 / HOST: power inhibit is smoothed rather than immediate

`INO:115–118; C:64–69`.
Power gate zeroes target only; s_actual retains output. HOST: starting at full output with allowed τ=3000 ms, one second after inhibit PWM=549/1023 (53.7%). This is duty, not lamp power at the changed voltage.
Target: inhibit immediately zeroes PWM and smoothing state, independent of user τ; define safe ramp on power restoration. Test loss/return of supply and disabling Force Out while supply is low.

### R4 — P2 / HOST: held confirmation key exits OTA after reboot

`M:774–780; K:22–40; INO:94–101`.
Confirm OTA Yes and hold SEL through reboot. buttons::begin assumes released keys; held SEL becomes a fresh pressed edge after debounce. OTA loop interprets it as exit/reboot. HOST confirms edge at 26 ms after first held sample. **Possible explanation of handoff's failed menu entry, not proven cause of that incident.**
Target: arm OTA exit only after all keys are released; subsequently require a new press. Test held SEL through entry and ordinary exit afterward.

### R5 — P2 / STATIC: global echo guard discards genuine Zigbee commands

`Z:75–76 onFaraHsv; Z:132–157 doReport`.
Interleaving: loop sets s_reporting=true, waits for SDK lock in a setter; Zigbee task handles a remote command, calls onFaraHsv, which returns because of that same flag. Endpoint internal state changes but config::s does not. Device occurrence rate unknown.
Target: distinguish local setter callbacks from inbound commands; serialize application state updates or queue commands to one owner. Avoid lock-order deadlocks. `volatile`/atomic byte loads do not make multi-field updates or lampOn read-modify-write safe. **SDK setClusterAttribute already acquires Zigbee lock**; do not report all setters as unlocked. Test inbound commands during reporting.

### R6 — P2 / STATIC: OTA cancellation/restart races flash writer

`B:45–64 writerTask; B:76–81 disconnect; B:91–111 START/ABORT`.
Writer uses s_handle while disconnect/ABORT calls esp_ota_abort on it without waiting. s_receiving=false does not cancel an in-flight write. START accepts an already active session and replaces shared state/handle. OTA abort frees session resources: [Espressif API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/system/ota.html).
Target: one owner executes begin/write/end/abort; callbacks enqueue requests; acknowledge cancellation after writer stops; reject concurrent START. Test disconnect/ABORT during write/finalization, then retry.
Related: xStreamBufferSend return ignored; declared total used only for progress, no exact byte-count check before FINISH. Validate these. **esp_ota_end does validate image; do not claim arbitrary truncated images are accepted or active firmware necessarily corrupted.** Writes target inactive slot.

### R7 — P2 / STATIC + HOST: remote state lacks persistence

`Z:43–57,75–90 callbacks; M:814–822; CFG:26–41`.
Remote callbacks change config and dirty redraw flag, never pendingSave. Repro: save local lamps-on, remote off, reboot → saved on-state returns. A subsequent unrelated local display edit persists entire config, including remote state. HOST reproduces persistence behavior with simulated Preferences; source confirms missing remote save path.
**Known roadmap gap, not necessarily new regression.** Target: explicit restart policy + common debounced persistence independent of writer; no NVS write per incoming dimming step. Verify remote-only changes/reboot and unrelated local edits.

### R8 — P2 / STATIC: local Static leaves stale effect state in EP14

`Z:150–155 doReport; Z:75–85 onFaraHsv`.
Effect report sets Fara on+color. Local Static report changes level only, preserving on+color. A subsequent changed EP14 Level invokes library callback with stale on/hue/sat and reactivates the old effect. Derived from installed library setters/callbacks; Alice tile behavior not measured.
White reporting was intentionally avoided due to documented Alice glitch; do not blindly remove workaround. Target: define coherent EP14 representation of local Static and next Level command; test effect → local Static → remote Level, off/on, and white fan-out. An effect-endpoint off representation is an option, not an approved replacement for white-master semantics.

### R9 — P3 / STATIC: Chase Fade UI accepts ineffective values

`F:44–55; M:120–123 fmtParam`.
Step=400 ms, Fade=4000 ms is accepted/displayed/saved; renderer clamps fade to Step/2=200 ms. All Fade values 200–4000 then have identical effect. Target: constrain dependent parameter or display effective limit. Time formatting also hides 50 ms steps by printing only tenths of a second.

## Documentation corrections / avoid false findings

- DEV_PLAN: obsolete Static×master/Fara-power model; preserve v2 above. README roadmap wrongly lists implemented PD gate/report-back as future. SESSION_HANDOFF “fully tested/complete” exceeds evidence; test journal mixes stale TODO/FAIL with newer OK results.
- Stale comments: INO GPIO order/PD not implemented; bleota.h and ../tools/ble_ota.py claim simultaneous Zigbee+BLE; config.h claims global static master and unimplemented effect persistence. Calibration is shared, despite README's “per-channel gamma”.
- Drive matches ../SmartLadaC6/docs/TZ_drive_mode.md tables. REVERSE only entered from STOP is true; STOP only after braking is false (initial STOP, HAZARD/REVERSE→STOP). No self-edges does not mean nonrepeating timeline; cycles exist. REVERSE→CRUISE without STOP and steady brake during reverse are documented artistic choices, not implementation regressions.
- 7/8 V gate does not verify exactly 12 V. Old hardware-spec USB-host inhibit/overvoltage requirements are absent; clarify their current status before expanding scope. No external lamp-temperature sensor is intentional; die-temperature diagnostics are not lamp protection.
- Static compositor: 16,384 channel assertions passed across 256 master values × 16 masks × 4 channels; no double master, off-mask respected. Turn channel isolation passed. Effects ignoring static mask is intentional. GPIO map matches PCB verification; dual OTA slots confirmed. Successful build is not proof of runtime/radio/electrical safety.

## Reuse validation

Build from the directory containing `SmartLadaRevC/`:
```sh
arduino-cli compile --fqbn esp32:esp32:esp32c6:ZigbeeMode=ed,PartitionScheme=zigbee_8MB,CDCOnBoot=cdc,FlashSize=16M --build-path /tmp/smartlada-revc-review-build SmartLadaRevC
```
Passed: app=1,111,526 bytes (32%); globals=40,416 bytes (12%, not peak runtime RAM).
Optional temporary evidence, may expire: `/tmp/smartlada-review/{input-manifest.txt,harness.cpp,stubs/,harness}`. Harness includes actual CFG/F/C/K/M with peripheral stubs. Cases: `timing-drive`, `timing-shrink` (expected sanitizer failures); `boot`, `gate`, `held-ota`, `persist` (counterexample outputs); `compositor` (pass). Do not require the long report or /tmp artifacts to understand findings. Rebuild host tests after source changes; compiled harness is a snapshot.
