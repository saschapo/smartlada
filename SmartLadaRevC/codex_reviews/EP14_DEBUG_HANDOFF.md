# EP14 / Yandex color display investigation

Status: baseline hardware sequence completed 2026-09-06; owner confirms both UI failures. Persistent EnhancedColorMode mismatch hypothesis disproved in this run. Diagnostic v2 compiled; next measurement is command/report tracing.
Target: SmartLadaRevC, current working tree v1.0.9-revC, Arduino-ESP32 3.3.10.

## Evidence vs hypotheses

- Reported symptom: white -> color snaps the app wheel to red; white changes to an unnamed white about 2 s later. Physical effect selection reportedly correct; not yet independently reproduced.
- Confirmed in installed Arduino core: constructor defaults ColorMode and EnhancedColorMode to XY (1). `setLightColorCapabilities(1)` does not change either mode. HSV callback synchronization explicitly writes only ColorMode. `setLightColorMode` is private.
- DISPROVED by baseline: EnhancedColorMode stays 1 after color commands. All 20 post-command snapshots show ColorMode=EnhancedColorMode=0 without H. The underlying stack updates it despite no explicit Arduino write.
- NOT established: Yandex reads/uses EnhancedColorMode. An attribute-table dump cannot identify incoming read requests or outgoing network traffic.
- Early returns from state/level setters do not prove radio silence: automatic reporting and read responses remain possible.
- ZCL requires copying a changed ColorMode to EnhancedColorMode for ordinary color modes. Source: [ZCL revision 6, section 5.2.2.2.1.2](https://csa-iot.org/wp-content/uploads/2019/12/07-5123-06-zigbee-cluster-library-specification.pdf).

## Changes in this pass

- Added `src/net/fara_diagnostics.{h,cpp}`; added diagnostic hooks to `src/net/zigbee.cpp` only.
- USB snapshots on connection, after settled HSV processing, and 2300 ms after the last settled callback.
- USB `?`: immediate read. Logs decimal mode, enhanced mode, capabilities, hue, saturation, X/Y, temperature, on/off and level. `-1` means missing attribute or unexpected type.
- USB uppercase `H`: one-shot experiment. If caps=1, ColorMode=0 and EnhancedColorMode>0, write only EnhancedColorMode=0. Print before/after and SDK status. Otherwise do nothing.
- No automatic correction, no forced report, no hue/brightness write from diagnostic code. Original application reporting remains active. All SDK access is locked; USB printing occurs after releasing the lock.
- H deliberately does not force ColorMode: that would bypass the library's private mode cache. First select a color normally.
- No Arduino core edits, commits or factory reset. Diagnostic uploads authorized by owner. Existing uncommitted work preserved.

## Live session (2026-09-06)

- Upload to `/dev/cu.usbmodem101` succeeded; ESP32-C6 detected, all written hashes verified. Firmware diagnostic marker confirmed over USB.
- Baseline capture: `codex_reviews/EP14_USB_2026-09-06.log`. Reader session 82905 stopped after the sequence to release the port for v2 upload.
- Initial manual snapshot: mode=1, enhanced=1, caps=1, h=0, s=0, x=24939, y=24701, ct=250, on=1, level=107. Confirms inconsistent initial capabilities/mode; does NOT yet establish post-color state or cause of UI issue.
- Owner's baseline sequence: white -> magenta -> white -> blue -> white -> cyan -> white -> green -> white -> red. Requested ~4 s between selections.
- No H correction sent; no mismatch exists after a color command, so H would be a no-op.

### Baseline result

All ten selections arrived, each with the same values immediately after settling and 2300 ms later. mode=0, enhanced=0, caps=1, level=107 throughout these snapshots. X/Y stay at 24939/24701; this alone does not establish that Yandex uses them.

| Selection | H | S | Firmware effect mode |
|---|---:|---:|---:|
| White (all 5 selections) | 162 | 25 | 0 |
| Magenta | 215 | 243 | 5 |
| Blue | 158 | 243 | 4 |
| Cyan | 124 | 243 | 3 |
| Green | 95 | 243 | 2 |
| Red | 0 | 243 | 1 |

Each action produced two callbacks carrying the previous HSV pair, followed ~100 ms later by the hue/saturation update pair. Existing coalescing applied the final pair in these ten trials. This does not prove correctness under other timing or concurrency.

### Diagnostic v2

- Chain the Arduino 3.3.10 APS receive hook (preserving its binding handling) to log EP14 Color Control, OnOff and Level frames as bounded hex. This shows commands/read requests; it is not an over-the-air sniffer.
- Add ZCL send-status hook; it exposes status/TSN/endpoints, NOT transmitted attribute values. Do not claim payload evidence from it.
- `?` also reads reporting configuration/last reported storage for H/S/X/Y/modes/enhanced hue. Configuration is not proof of actual delivery.
- `R` sends the current H, S, ColorMode once each to coordinator endpoint learned from an incoming Color Control frame. Does not change any attributes or synthesize an effect-center hue. No automatic color reports.
- Next: owner selects white, waits >=4 s, selects blue, waits >=4 s and reports UI state. Capture RX/config, then use R to test whether current-value reports correct the displayed color. Do not send R before that baseline snapshot.
- v2 full build passed (1,116,464 bytes flash, 40,448 bytes globals). Host sanitizer tests passed, including bounded trace, chained handler and R destination/attribute IDs/no attribute writes. Build: `/tmp/smartlada-ep14-debug/build-v2`.
- v2 uploaded successfully; reader session **80322**, log `codex_reviews/EP14_USB_v2_2026-09-06.log`. Initial capture found a blue state (h=158/s=243), apparently selected before capture started, so its RX trace was missed. Request a fresh white -> blue sequence now that capture is active.
- Initial reporting lookup: H direction=1, S direction=0 (min=1, max=28800, delta low byte=1, last low byte=243). This is suspicious but not yet proof of the fault. v2 prints `send_info` even for direction=1: IGNORE min/max/delta/last for that record (it is the receive union). Also ignore upper bytes of delta/last for U8 attributes. Inspect actual configure-report requests if available; do not assume this lookup proves all report behavior.
- R has not been sent.

## Next hardware test

1. With owner approval, upload the compiled diagnostic build to the confirmed lamp port; keep Zigbee pairing/storage intact.
2. Capture USB output. Find `EP14 diagnostic build` marker (firmware version string itself is unchanged).
3. Select white, wait >=3 s, select a clearly non-red color, wait >=3 s. Record when the app changes and retain both settled/+2300ms snapshots. Repeat once.
4. If snapshots show caps=1, mode=0, enhanced=1, send H once. Confirm status=0 and enhanced=0. Repeat the identical white/color sequence, preferably multiple times. No forced report means the UI need not change immediately on H alone.
5. If the app still fails while both mode attributes remain 0, the mismatch alone is insufficient to explain the symptom. If enhanced was already 0 before H, the main premise in problem.txt is disproved for that run.
6. If H consistently fixes it, implement a small permanent initialization/synchronization fix and retest boot/rejoin/on-off/brightness. Do not simply write ColorMode behind the library's internal cache.
7. If inconclusive, inspect actual Zigbee traffic or compare another coordinator. A different coordinator behaving well is useful evidence, not proof that only Yandex is at fault; endpoint acceptance/quirks/report configuration may differ.

## Validation

- Full Arduino build passed: 1,113,896 bytes flash (32%); 40,440 bytes globals (12%).
- FQBN: `esp32:esp32:esp32c6:ZigbeeMode=ed,PartitionScheme=zigbee_8MB,CDCOnBoot=cdc,FlashSize=16M`.
- Build directory: `/tmp/smartlada-ep14-debug/build` (temporary).
- Host test compiled actual diagnostic source with mocked SDK, AddressSanitizer and UndefinedBehaviorSanitizer: read-only default; single-attribute correction; no write for already-HS, wrong capabilities, XY mode, missing/wrong-type attribute; lock failure and setter failure; delayed snapshot and millis wrap. Passed. This does not validate the actual stack or Yandex.

## Separate existing issue; not changed to keep the experiment isolated

`onFaraHsv()` unconditionally calls `applyLevel(value)`, including on color callbacks with an unchanged level. In static mode this still flattens every channel's stored brightness before the hue/saturation pair settles. Therefore the claim that coalescing fully prevents that overwrite is false for the present working tree. A later fix should distinguish level events from color events instead of treating every HSV callback as a brightness command. The pending HSV fields also lack an atomic snapshot across tasks.
