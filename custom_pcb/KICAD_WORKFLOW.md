# SmartLada Rev A — KiCad workflow, rules and lessons

Working notes for the carrier board. Companion to
[smartlada_rev_a_bom_netlist.md](smartlada_rev_a_bom_netlist.md), which holds the
electrical spec. This file holds **process**: the order of work, the rules that decided
the design, and the mistakes that were caught along the way.

**Status (2026-07-20): board is fully routed and clean — ERC 0/0, DRC 0 violations,
0 unconnected, 0 footprint errors. Next stage is fab output generation.**

**Start here in a new session:** read section 5 for the current numbers, section 6 for
what is still open, section 7 for the next actions. The electrical spec lives in
[smartlada_rev_a_bom_netlist.md](smartlada_rev_a_bom_netlist.md); this file is process
and state.

---

## Contents

1. [Stage checklist](#1-stage-checklist)
2. [Rules that decided this design](#2-rules-that-decided-this-design)
3. [Errors caught in review, and the lesson from each](#3-errors-caught-in-review-and-the-lesson-from-each)
4. [KiCad-specific traps](#4-kicad-specific-traps)
5. [Current numbers](#5-current-numbers)
6. [Open items](#6-open-items)
7. [What is next](#7-what-is-next)

---

## 1. Stage checklist

| # | Stage | State |
|---|-------|-------|
| 1 | Scope: what the board replaces, what is deliberately excluded | done |
| 2 | BOM and net-level spec agreed before drawing anything | done |
| 3 | KiCad project created inside the repo | done |
| 4 | Schematic, block by block: input -> buck -> switches -> connectors | done |
| 5 | ERC clean (0 errors, 0 warnings) | done |
| 6 | Footprints assigned to every symbol | done |
| 7 | Board Setup: stackup, constraints, net classes | done |
| 8 | Update PCB from Schematic | done |
| 9 | Board outline, mounting holes | done |
| 10 | Placement | done |
| 11 | Routing: critical loops first, power last | done |
| 12 | Copper zone (GND on B.Cu) | done |
| 13 | DRC clean | done — 0/0/0 |
| 14 | Silkscreen: functional labels matching firmware | done |
| 15 | **3D check, fab outputs, order** | **next** |

The order is not decorative. Each stage locks decisions the next stage depends on;
going back costs more than doing it in order.

---

## 2. Rules that decided this design

### Electrical

- **A series part splits a net.** Fuse, FET, inductor, resistor — each one starts a new
  net after it. The 3.33 A input path is therefore three nets (`+12V_IN`,
  `+12V_FUSED`, `+12V`), and each needs its own width. A `+12V` pour covers only the
  last one and must not bridge the others, or it shorts out the fuse and the FET.
- **P-channel reverse-polarity FET: drain to the incoming supply, source to the board.**
  The body diode runs anode-on-drain, so it must point *along* normal current flow;
  then reverse polarity finds it blocking. Wired the other way the board still works on
  the bench and fails on the one event the part exists for.
- **Gate pull-down lives at the gate**, after the series resistor. It holds the lamps off
  between reset and `forceAllLow()`, and it also turns a channel off if a flying lead
  falls off.
- **Feedback must sense after the inductor.** `SW` is the input of the LC filter, not the
  output. A divider on `SW` closes the loop around an unregulated node.
- **Slow-blow means slow-blow.** IEC 60127: `T` = time-lag, `F` = fast. A fast 4 A fuse
  dies on the cold-filament inrush; the letter is the specification.
- **Fuse rating must be below the supply's own limit** or it can never blow. The Ecola
  PSU limits at 6.6 A, so an 8 A fuse is decorative.

### Layout

- **The switching regulator's input loop is the one thing that must be tight.** Nearest
  input cap within ~2 mm of VIN/GND. Loop area, not trace length, is what sets the
  voltage spike on VIN and the radiated field.
- **Give the short path to the big current.** `Q5.S -> J2.1` (3.33 A) is 6 mm;
  `Q5.S -> U1.VIN` (0.4 A) is 32 mm. Length only costs where current flows.
- **A bottom-layer trace cuts a slot in the ground pour**, and return current must go
  around it. On two layers this is the usual reason a board with "a ground plane" still
  behaves badly. Keep the return corridor clean; route signals on the other layer.
- **DRC does not check current.** 0.2 mm at 3.33 A calculates to roughly a 300 C rise and
  no tool will complain. Trace widths are the designer's job: IPC-2221, 1 oz, 10 C rise
  gives ~1.6 mm for 3.33 A.
- **Star ground was dropped once the layout made it unnecessary.** With the buck 40 mm
  from the lamp path the separation is already geometric; a single pour plus a dedicated
  FB ground trace achieves the same thing with less risk.

---

## 3. Errors caught in review, and the lesson from each

Kept because the lesson is worth more than the fix.

| What was wrong | Lesson |
|---|---|
| Q5 documented source-to-input | Derive body-diode direction from device physics, not memory. Verify in the symbol graphics. |
| Fuse and bulk caps tapped *before* the protection | Draw the series chain in physical order; the electrolytic must sit behind reverse protection or it vents. |
| `F4A` typed instead of `T4A` | A single letter can be the whole spec. |
| Feedback divider tapped on `SW` instead of after `L1` | "Output" means the node with the output caps, not the pin next to the inductor. |
| `+12V_IN` / `+12V_FUSED` left unnamed, so they inherited the 0.2 mm Default class | Name every net that carries real current; netclass patterns work on names. |
| Netclass patterns `+12V`, `LAMP*` might miss `/+12V_IN`, `/LAMP0` | Local labels on the root sheet get a `/` prefix; power symbols do not. Use leading wildcards. |
| `Update Symbols from Library` silently reset Value fields | Bulk library operations overwrite *your* data. Uncheck the reset options. |
| Designator gap J1, J2, J3, J5 | Renumber while it is free; a gap reads as a missing BOM line later. |
| Silkscreen numbered channels 1..4 while nets and firmware are 0..3 | An off-by-one between the board and the code is a wiring bug waiting to happen. Fixed by labelling functionally: `GPIO_0_stop`, `LAMP 0: STOP`. |
| A stray copper ring shorting GND to Q1's gate | See below — routing with arcs can silently produce a full circle. Found only because DRC named the exact coordinates. |
| **Review script mirrored every footprint in Y** | **A wrong tool produces confident, specific, wrong numbers.** It reported gates facing left when they faced right. Cross-check computed geometry against a screenshot before acting on it. |

That last one is the most important entry in this table, and it was caught by the user
pushing back rather than accepting the analysis.

---

## 4. KiCad-specific traps

- **Footprints are copied into the board file.** Editing a library footprint does not
  change the board until `Tools > Update Footprints from Library` — and that command
  reverts any per-instance 3D-model override made in the board.
- **Not every stock footprint ships with a 3D model.** `TerminalBlock_MaiXu_MX126-*`
  references a `TerminalBlock.3dshapes` directory that does not exist in the install.
  Substituted Phoenix MKDS models of the same pitch.
- **Vendor footprints from UltraLibrarian/SnapEDA often arrive with no `(model ...)`
  entry at all** and an empty 3D folder. The library path and env var can be perfectly
  configured and still show nothing.
- **Vendor symbols may carry a second body style** (`_1_2`), with a different pin
  arrangement that is not the one in use.
- **`PWR_FLAG` is not a component.** It marks a net as externally driven so ERC stops
  asking who drives it. Needed only on nets that have a power symbol but no power-output
  pin — here `+12V` and `GND`.
- **A footprint's pad coordinates are Y-down, same as the board.** There is no Y flip
  (unlike schematic symbols, where library Y points up). Getting this wrong mirrors every
  part.
- **Rotating a symbol 180 degrees moves its pins to the other side**; to swap pin order
  in place, mirror it instead.
- **Mounting holes drawn as `Edge.Cuts` circles** become real cutouts and enforce
  copper-to-edge clearance, but carry no courtyard, so DRC will not keep components off
  them. A `MountingHole` footprint is better; give each a unique reference (`H1..H4`)
  or DRC reports `duplicate_footprints`, and tick "Not in schematic" to silence
  `extra_footprint`.
- **Routing with arcs can produce a full circle.** If an arc's start and end land within
  a few hundredths of a millimetre of each other while the mid point is far away, the
  three points describe a near-complete circle. One such arc here was 0.04 mm of "chord"
  but 4.29 mm of radius — a 1 mm wide copper ring 8.6 mm across, shorting GND onto a
  MOSFET gate pad. It is invisible as an error until DRC names the coordinates. **After
  drawing an arc, look at it; if it closed into a ring, undo immediately.**
- **A vendor footprint can carry zero-length courtyard segments** (`start == end`).
  KiCad then reports the courtyard as both "self-intersecting" and "not a closed shape".
  L1's footprint had four of them; deleting the degenerate segments left a valid closed
  square.

---

## 5. Current numbers

Board **80 x 65 mm**, 2 layers, 1 oz, 1.6 mm. 30 components + 4 M3 mounting holes.

| Net class | Nets | Track | Via |
|---|---|---|---|
| POWER | `*+12V*`, `GND`, `*LAMP*` | 1.5 mm | 0.8 / 0.4 mm |
| Default | everything else | 0.2 mm | 0.6 / 0.3 mm |

| Metric | Value |
|---|---|
| ERC / DRC | 0 / 0 violations, 0 unconnected, 0 footprint errors |
| Copper, F.Cu | 323.5 mm of track |
| Copper, B.Cu | 5.3 mm (a deliberate FB-ground link only) |
| Vias | 8, all GND |
| GND pour | one island, 4841 mm^2 = 93% of board area |
| Buck input loop (nearest cap) | 3.39 mm centre-to-centre, ~10 mm^2 loop |
| C5 -> BOOT / SW | 3.89 / 4.86 mm |
| SW -> L1 | 5.59 mm |
| R2 -> FB | 4.87 mm |
| Rpd -> gate | 6.5 mm (all four) |
| Q5.S -> J2.1 (3.33 A) | 1.5 mm wide |
| `+12V_IN`, `+12V_FUSED`, `LAMP0..3` | 1.5 mm throughout |
| `+12V` feed to the buck (0.2 A) | 0.5 mm, 37 mm long — fine at that current |
| Courtyard overlaps | none |

Silkscreen is functional and matches the firmware's channel names:
`LAMP 0: STOP / 1: REV / 2: TURN / 3: MARKER`, `GPIO_0_stop .. GPIO_3_marker`,
`+12V OUTPUT`, `+5V OUTPUT`, `Empty` on the reserved J3.8.

---

## 6. Open items

**Resolved:** mounting holes (4x M3 3.2 mm at 5 mm inset), silkscreen numbering,
L1 courtyard, the stray GND ring, J1/J2 3D models, netclass patterns.

**Devboard reserve: dropped.** A 26 x 52 mm rectangle does not fit on 80 x 65 alongside
the components. Rev A uses flying leads and the devboard sits beside the board, so the
reserve buys nothing today. If Rev B sockets the devboard, the outline changes anyway.

**Before ordering:**

- [ ] AOD4184A Rds(on) and Qg **at Vgs = 3.3 V**, and SOA at the cold-filament inrush.
- [ ] AMS1117 dropout at ~4.6 V in, which sets the lower edge of the 5 V window.
- [ ] Confirm the real devboard's pin order against its silkscreen before wiring J3 —
      the board on order is not the one the available drawings describe.
- [ ] Review Gerbers in an external viewer before uploading.

**Optional, cheap:**

- [ ] A second via on each MOSFET source. One via carries 0.83 A comfortably, but a
      single via is a single point of failure for that channel's return.
- [ ] Three sub-0.1 mm arcs remain on `+5V_SW`, `U1-FB`, `U1-BOOT`. Harmless (radius
      under 0.1 mm), left in place deliberately — removing them risks breaking a link.

**At bring-up:**

- [ ] Verify "duty 0 = lamp off" on first power-up, before anything else.
- [ ] Measure the buck output and trim R3 so the post-diode voltage lands near 4.6 V.
- [ ] Check reverse polarity protection deliberately, on the bench, before trusting it.

---

## 7. What is next

Routing is finished. Remaining work, in order:

1. **3D check** (`Alt+3`) for mechanical collisions: the fuse holder and the electrolytic
   are the tall parts; confirm nothing fouls the terminal blocks or the M3 holes.
2. **Fab outputs**: `File > Fabrication Outputs` — Gerbers (all copper, mask, silk,
   Edge.Cuts), drill files, and a position file if anything will be machine-placed.
3. **Verify the Gerbers in an external viewer** before uploading. Look at: board
   outline present and closed, the four M3 holes in the drill file, silkscreen legible
   and not clipped by pads, GND pour continuous, no missing layer.
4. **Order.** 80 x 65 mm is inside the cheapest tier at the usual fabs; confirm the
   current threshold at order time.
5. **Assembly and bring-up** — see the bring-up checklist in section 6.

Firmware needs no changes: pins stay `GPIO0..3` (PWM), `GPIO8` (WS2812) and `GPIO9`
(button), all on the devboard.
