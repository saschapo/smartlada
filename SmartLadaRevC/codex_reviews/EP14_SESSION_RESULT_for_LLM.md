# EP14 / Yandex color feedback — session result

Date: 2026-09-06. Project: `SmartLadaRevC`. Firmware: `1.0.9-revC`.
This document supersedes `EP14_DEBUG_HANDOFF.md` for this incident. Older project notes
claiming EP14 reports H/S or calling report-back fully complete are outdated.

## Outcome

Implemented and flashed a small Yandex compatibility workaround: suppress automatic
Hue/Saturation reports on EP14 before incoming Color Control commands update attributes.
Keep received H/S unchanged; retain attribute reads, brightness/on-off reporting and EP10–13.
No color scaling, preset lookup table, fabricated values, core-library edits or re-pairing.
No commits; the working tree contains extensive pre-existing user changes. Preserve them.

This removes the demonstrated trigger. It does **not** establish the precise defect inside
the station, its adapter/backend or the phone app.

## Context and decisive evidence

EP14 uses color to select an effect, not physical RGB output. Saturation <40 means static;
otherwise hue selects an effect. Level controls brightness. Original symptom: after white,
the app jumped to red; white became an unnamed white. Effect selection itself worked.

Received raw H/S examples: white `162/25`, blue `158/243`, green `95/243`, red `0/243`.
Attribute values remained stable after settling and 2.3 seconds later.

| Experiment | Observed result |
|---|---|
| Inspect modes after commands | ColorMode and EnhancedColorMode both became 0; persistent mode mismatch was not the cause. |
| Explicit hue reports | Helped replace stale cached hue; did not preserve presets reliably. |
| Stop SDK H/S reports; send one combined H/S packet after settling | Stop API succeeded; no early report response remained. All six transmitted pairs exactly matched incoming bytes, with APS and ZCL success responses. Preset replacements persisted. |
| Disable both SDK H/S reports and explicit color reports | User confirmed no jumps through white → blue → white → green. |
| Send one unchanged green report | No jump. Station had already received the same pair earlier; duplicate filtering is plausible, not proven. |
| Select white without reports, then manually report unchanged `162/25` | White stayed selected until the report, then became unnamed. Packet: `08c10a000020a201002019`; APS success and ZCL success response. |
| Select white again without reports; reopen card; change brightness | User confirmed preset remained selected after both actions. |

The separate-packet ordering hypothesis failed: combined reports still caused the issue.
An extreme-red slider test received S=243, not 254, so it did not rule out conversion or
rounding upstream. Screenshots alone do not reveal numeric H/S. The user's other lamp is
Matter, not Zigbee; no independent Zigbee-lamp comparison was completed.

## Final implementation

- `src/net/zigbee.cpp`: `onApsIndication()` stops only configured SEND reports for EP14
  Color Control attributes 0/1. It always chains Arduino's existing
  `zb_apsde_data_indication_handler()` to preserve binding-list handling and normal processing.
  Installed after `Zigbee.begin()` under the SDK lock; callback itself already holds the lock.
- Same file: protect pending HSV snapshot with `portMUX_TYPE`; compare settling timestamps
  using signed subtraction. A callback timestamp newer than the loop's sampled time previously
  underflowed unsigned subtraction and bypassed the 150 ms wait. This was separately fixed.
- `.ino`: USB TX timeout remains zero, avoiding stalls when debug output is unread.
- `README.md`: compatibility behavior, evidence and limitations documented.
- Removed temporary `color_report.{h,cpp}` and `fara_diagnostics.{h,cpp}`. No manual P/R/H/Q/C
  protocol or experimental APS sender remains in the final firmware.

## Validation and stopping state

- Arduino-ESP32 **3.3.10**, ESP32-C6, USB `/dev/cu.usbmodem101`.
- Build and upload succeeded; flash hashes verified. Final build: 1,112,890 program bytes,
  40,432 global-data bytes. Boot log confirms workaround installed and firmware ready.
- C++ tests with ASan/UBSan passed against extracted final handler and HSV snapshot:
  reporting scope, malformed/unrelated traffic, original-handler chaining, error path,
  settling deadline and timer wrap. These do not simulate the entire Zigbee stack.
- Final cleaned firmware received blue → white and selected modes 4 → 0 correctly in logs.
  **User did not explicitly confirm the final cleaned build's UI smoke test before ending.**
  The no-jump/reopen/brightness confirmations above were on diagnostic v6 with the same
  report-suppression behavior.
- USB capture stopped. Device remains on final firmware. No further tests scheduled.

## Limits / avoid scope expansion

- Local button-selected effects do not update the app's color selector; this already matched
  the pre-investigation policy. Suppression trades color feedback for stable Yandex presets.
- Re-pairing, other controllers and future Arduino versions are untested. The chained Arduino
  hook is version-sensitive. This is a compatibility workaround, not full reporting compliance.
- Immediate `applyLevel()` still writes shared configuration from the Zigbee callback;
  broader state races, static-channel overwrite semantics and persistence were not fixed here.

## Evidence / reproducibility

Logs are alongside this file: `EP14_USB_paired_no_auto_v5_2026-09-06.log` (exact paired
reports still fail), `EP14_USB_quiet_v6_2026-09-06.log` (no-report and manual-white A/B),
`EP14_USB_final_2026-09-06.log` (final boot/commands). Earlier logs retain exploratory tests.

Temporary builds/tests/source backups: `/tmp/smartlada-ep14-debug/` (not durable).
Final build directory: `build-final`; debug-v6 source snapshot: `v6-sources`.
FQBN: `esp32:esp32:esp32c6:ZigbeeMode=ed,PartitionScheme=zigbee_8MB,CDCOnBoot=cdc,FlashSize=16M`.
