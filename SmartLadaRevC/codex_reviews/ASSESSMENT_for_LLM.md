# SmartLadaRevC — assessment and development priorities

Date: 2026-09-06. Basis: review of working-tree firmware **1.0.6-revC**, project documents, successful build, and targeted host tests. Detailed defects/evidence: `REPORT_for_LLM.md` in this directory. This document summarizes engineering judgment and recommendations; it does not authorize implementation or replace accepted requirements. Follow the user's current task.

**Scope:** indoor decorative light using a VAZ tail-light housing. Assessment covers firmware, control semantics, and development process. Hardware, radio, electrical and thermal safety were not validated on a device during this review.

## Verdict

Useful working prototype with a sensible structure; not yet sufficiently predictable for an unfamiliar user to operate as a finished product. Normal operation can be stable while uncommon transitions remain unsafe or inconsistent. No evidence supports a full rewrite.

Strengths:
- Modules separate PWM channels, effects, input, menu, power and networking without excessive abstraction.
- Static/effect composition is understandable. Targeted tests confirmed static on-mask behavior and absence of double master scaling.
- Development includes real-device observations, rejected alternatives and recorded integration constraints.
- Dedicated BLE OTA mode is a practical response to observed radio coexistence problems.

Main weakness: reliability currently depends on individual fixes more than system-wide rules. Examples: boot ramp retains an old clock; Timings editor survives a remote effect change with invalid state; OTA writer owns writes but callbacks still abort its resource. These are primarily interaction/transition problems, not evidence that every module is poorly designed.

## Implement differently — recommended order

### 1. One owner of application state

Menu and network callbacks submit typed commands; one owner applies them sequentially to `config::s`. Examples: select effect, set channel level, set channel on/off. The same path validates input, invalidates dependent UI state, schedules persistence and schedules network synchronization.

Purpose: make last-writer-wins an explicit command order and prevent inconsistent multi-field updates. Atomic byte accesses or `volatile` do not provide this guarantee. Keep the implementation small; a framework is unnecessary. Define callback/lock ordering before changing synchronization.

### 2. Separate desired light from permission to energize outputs

Effects compute desired brightness. A final output layer enforces power permission. Inhibit must immediately zero physical PWM and smoothing state, independent of user Soft Start. Power restoration gets an explicit startup ramp with a fresh clock.

Purpose: visual smoothing must not determine protective shutdown behavior. Review boot, loss of supply, return of supply and Force Out transitions as separate scenarios.

### 3. Give OTA an explicit lifecycle and resource owner

States: idle → preparing → receiving → validating → completion, or cancellation/error. One owner executes begin/write/end/abort. Callbacks request actions; they do not independently destroy writer resources. Define repeated START, disconnect during write/finalization, cancellation acknowledgement and retry behavior.

Purpose: replace implicit combinations of shared flags with reviewable transitions. More explicit code is justified here by failure cost.

### 4. Separate application state from Alice's representation

EP14 hue is a transport/UI encoding for effect selection. Stale endpoint attributes must not silently become a second authority over the selected mode. Specify what the next Level command means after locally choosing Static, and how that state is represented remotely.

Preserve accepted v2 unless the user explicitly changes it: Static has no master multiplier; effects intentionally ignore static channel masks; Fara off returns to Static rather than turning every lamp off. Account for the documented Alice white-reporting glitch when choosing a synchronization fix.

### 5. Maintain one compact current-behavior specification

Use a table: command → state mutation → output behavior → persistence. Include local/remote input, off, reboot, power loss/return and overlapping commands. Keep historical decisions separate and label superseded rules.

Purpose: prevent future edits from restoring obsolete behavior. Assertions such as “fully tested” or “resolved” must identify the firmware version, tested scenario and evidence limits. A successful device run validates that setup/scenario, not every transition or power source.

## Add next — proposals, not approved requirements

1. **Power-on policy: Off / Restore.** Make restart behavior explicit. Persist eligible local and remote changes consistently; do not make remote-state persistence depend on an unrelated display edit.
2. **Unambiguous local “all light off”.** Define an operation that extinguishes outputs in every mode. Distinguish it from disabling one static channel or exiting an effect. Decide persistence/resume semantics before implementation.
3. **Small recent-event log.** RAM ring buffer: command source, mode changes, output-inhibit reason, OTA result and reset reason. Choose an accessible diagnostic view/output. RAM history does not survive reboot unless explicitly preserved; do not imply otherwise.
4. **Targeted transition tests.** Cover remote mode changes during editing, startup after delays, power loss/restoration, remote off followed by reboot, OTA held-key entry/cancellation/retry. Test meaningful invariants and failure paths; coverage percentage is not the objective.
5. **Saved scenes.** Named channel/brightness combinations may be more useful than additional animations. This is a product hypothesis to validate through actual use, not an established user need.

## Defer

Defer Wi-Fi/web UI and additional effects until the existing control paths are predictable. They add interaction cases to an area already producing defects. Revisit them after state ownership, output permission, OTA lifecycle and restart semantics are verified.

## Suggested next iteration

Fix the demonstrated memory-access defect and boot-clock defect first. Then address power inhibit, OTA entry/lifecycle and concurrent state handling; define persistence and EP14 transition semantics explicitly. Add regression checks for those behaviors and update the current-behavior document. Verify electrical/radio consequences on hardware rather than inferring them from a successful build or host test.

Success criterion: the existing feature set behaves predictably across transitions and failures, with evidence bounded to what was actually tested. Preserve useful modules and accepted behavior; no wholesale rewrite or automatic scope expansion.
