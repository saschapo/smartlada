# SmartLada Rev A -- fabrication order preparation

How to turn the finished KiCad project into a manufacturable data package and place the
order. Companion to [KICAD_WORKFLOW.md](KICAD_WORKFLOW.md) (process/state) and
[smartlada_rev_a_bom_netlist.md](smartlada_rev_a_bom_netlist.md) (electrical spec).

**Status (2026-07-21):** board fully routed, DRC 0/0/0 after the J2 change to 2.54 mm.
Parts are being ordered. This document is the gate between "routing done" and "gerbers
sent".

Requirements below are distilled from three Russian fab houses whose limits and order
process are representative: PCBtech, PSElectro (ELEKTROconnect blank) and Rezonit. Any
of them can build this board; the data package is the same for all three.

---

## 1. Board summary (what the fab must build)

| Parameter | Value |
|---|---|
| Outline | 80 x 65 mm, rectangular |
| Layers | 2 (F.Cu, B.Cu) |
| Base material | FR4 |
| Board thickness | 1.5 mm |
| Copper weight | 35 um (1 oz), both sides |
| Solder mask | black, both sides (as ordered 2026-07-21) |
| Silkscreen | white, top only (bottom is a bare GND pour) |
| Surface finish | HASL (lead-free acceptable); ENIG optional, not required |
| Min track | 0.20 mm |
| Min clearance | 0.20 mm |
| Min copper-to-edge | 0.50 mm |
| Smallest finished hole | 0.40 mm (vias) |
| Distinct drill diameters | 8 |
| Plated (PTH) holes | 0.4, 0.5, 0.8, 1.0, 1.1, 1.3 mm |
| Non-plated (NPTH) holes | 2.7 mm (F1 fixator), 3.2 mm x4 (M3 mounting) |
| Electrical test | yes (recommended) |
| Panelization | none -- single boards (80 x 65 > 25 x 25) |
| Quantity | match parts on order (fab minimum is typically 5) |

The NPTH split matters: the four M3 holes and F1's 2.7 mm center hole must be
**non-metallized**. KiCad marks them NPTH automatically; keep PTH and NPTH drill files
separate so the fab cannot plate them by mistake.

---

## 2. Design rules vs fab capability -- the board is inside standard limits

Nothing here needs a special process or carries a surcharge. Verified against the three
vendors' published limits.

| Rule | This board | Rezonit std 2-layer | PSElectro | Verdict |
|---|---|---|---|---|
| Min track | 0.20 mm | 0.125 mm | 0.20 mm min polygon line | OK |
| Min clearance | 0.20 mm | 0.125 mm | -- | OK |
| Min finished hole | 0.40 mm | ~0.21 mm (1:7 of 1.5 mm) | 0.3/0.4 in standard list | OK |
| Min via annular ring | 0.20 mm (POWER 0.8/0.4), 0.15 mm (Default 0.6/0.3) | 0.15 mm | pad rim >= 0.25 mm for auto-metallization | OK, but see note |
| Copper-to-edge | 0.50 mm | -- | 0.30 mm (mill) / 0.50 mm (scoring) | OK for either edge method |
| Board coordinates | <= 80 mm | -- | <= 1430 mm | OK |
| Drill types | 8 | -- | <= 9 recommended, >12 surcharge | OK, no surcharge |
| Board size | 80 x 65 mm | large-format available | -- | OK |

**Class of accuracy:** min feature 0.20/0.20 mm sits at Russian class 3-4. Order as
class 4; class 3 is also sufficient. Do not pay for class 5.

**Annular-ring note:** Default-class vias are 0.6 mm pad on 0.3 mm drill -> 0.15 mm
annular, right at Rezonit's standard floor and below PSElectro's 0.25 mm rim for
*automatic* metallization decisions. All 8 vias on this board are GND and POWER-class
(0.8/0.4 -> 0.20 mm), so this is comfortable in practice; just confirm the drill/hole
convention in the order form (section 5) so plating compensation is applied to the drill,
not the finished size.

---

## 3. Generate the fab outputs in KiCad

`File > Fabrication Outputs`. Two data sets: Gerbers and drill files.

### 3.1 Gerbers (`... > Gerbers (.gbr)`)

Plot exactly these layers:

- **F.Cu, B.Cu** -- copper
- **F.Mask, B.Mask** -- solder mask
- **F.Silkscreen** (B.Silkscreen is empty but plot it too; an empty gerber is fine)
- **Edge.Cuts** -- board outline

Do **not** send: F.Fab/B.Fab, User.* (documentation only), Courtyard, F.Paste/B.Paste
(paste is stencil-only; this board is hand-soldered, no stencil needed).

Plot options for Russian fab CAM:

- Format: **Gerber**, Units: **mm**
- Coordinate format: **4.6**
- **Uncheck** "Use extended X2 format" and "Include netlist attributes" -- older CAM
  reads plain RS-274X more reliably (PSElectro asks for RS-274X specifically)
- **Uncheck** "Plot border and title block"
- **Check** "Subtract soldermask from silkscreen" (keeps silk off pads -- PSElectro:
  silk on bare copper will not print)
- Mirror: **off**
- Drill marks: **none** (drills come from the Excellon file, not the copper gerber)
- "Use drill/place file origin" (aux origin) so gerbers and drill share an origin

### 3.2 Drill files (`... > Drill Files (.drl)`)

- Format: **Excellon**
- **Uncheck** "Merge PTH and NPTH into one file"  -- keep them separate so the 2.7 mm
  and 3.2 mm holes are unambiguously non-plated
- Units: **mm**, decimal format
- Drill origin: **same aux origin** as the gerbers
- Also generate the **drill map** (PDF or gerber) for the fab to sanity-check the pattern
- Optionally "Generate map file" + the drill report -- the report lists the 8 diameters
  and PTH/NPTH counts; keep it in the package

### 3.3 Package

Zip the whole set (gerbers + PTH.drl + NPTH.drl + drill map + report) under one name,
e.g. `smartlada_revA_gerbers_v1.zip`. Include this file's section 5 as the board
description / order form. KiCad's default file names carry the layer in the extension,
which every fab CAM recognizes; no renaming needed.

---

## 4. Verify the gerbers before uploading

Open the generated set in an **external** viewer (KiCad's own GerbView counts, but a
fresh open catches export mistakes the PCB editor hides). Check, in order:

- [ ] **Edge.Cuts** present and forms one closed rectangle -- no gap, no stray segment
- [ ] **Four M3 holes** and **F1's 2.7 mm hole** appear in the **NPTH** drill, not PTH
- [ ] All **8 GND vias** present on both copper layers
- [ ] **GND pour on B.Cu** is one continuous island, not fragmented by the export
- [ ] **Silkscreen** legible, not clipped by pads, sits over mask (not on bare copper)
- [ ] **Mask openings** on every pad; no pad left mask-covered
- [ ] No layer missing and no extra layer (paste must be absent)
- [ ] Drill count/types match the report: 8 diameters, PTH 0.4-1.3 mm, NPTH 2.7/3.2 mm

Only after this passes: upload.

---

## 5. Order form values

These fill any of the three vendors' forms. Field order follows the ELEKTROconnect
"Zayavka" blank v21.01 (`BOM_work/revA/electroconnect_blank_zakaza.xls`); PCBtech accepts
the same set as a free-form description; PSElectro reads it off the gerbers plus this
sheet.

| Field | Value |
|---|---|
| Board file name | smartlada_revA |
| Order type | new |
| Quantity | (set to match the parts run; fab min ~5) |
| Delivery format | single boards (no array) |
| Exact board size (mm) | 80 x 65 |
| Layer count | 2 |
| Base material | FR4 |
| Board thickness (mm) | 1.5 |
| Copper foil (um) | 35 (1 oz) |
| Solder mask | yes, both sides |
| Solder mask color | black |
| Marking (silkscreen) | white, top side only |
| Surface finish | HASL lead-free (or leaded HASL; ENIG not required) |
| Edge-connector plating | none |
| Electrical test | yes |
| Contour method | milling (routing); scoring also fine, both edge clearances met |
| Drill file diameter is | finished (after plating) -- confirm with vendor; apply plating allowance to drill, not to the finished hole |
| Input format (CAD) | Gerber RS-274X + Excellon |
| Outline layer name | Edge.Cuts |
| Internal-layer order (MLB) | n/a (2 layers) |
| Cutouts / windows | none (only round holes) |
| Vias | tented/covered by mask is fine; all vias are GND/POWER |
| Assembly | no -- bare board only |
| Delivery | (fill per vendor) |
| Other requirements | 4x M3 (3.2 mm) and F1 2.7 mm holes are NON-PLATED |

---

## 6. Vendor notes and how to submit

**PCBtech** ([order procedure](https://www.pcbtech.ru/poryadok-oformleniya-zakaza)).
Accepts Gerber RS-274X (and legacy formats with a surcharge). Order = order form (or
free-form description) + PCB files + any supporting docs. Must state thickness, finish,
mask color, layer count, material. Submit to **pcb@pcbtec.ru**, via the online form, or
in person; include purpose, contact, and requestor. Processing starts after payment.

**PSElectro / ELEKTROconnect**
([project requirements](https://pselectro.ru/zakaz-pechatnyh-plat/trebovania-k-proektu-pecatnoj-platy-40626/)).
Gerber **RS-274X**, coordinates <= 1430 mm; drill in **Excellon**. Outline in a dedicated
layer as continuous, non-self-intersecting lines/arcs (edge = line centerline). Vector
polygons only, min line 0.2 mm -- no raster fills. Mask: green negative, 0.1 mm
pad-to-mask gap, 100 um min mask bridge. Silk: min line 0.15 mm, text >= 1.3 mm, stroke
font (GOST type B), >= 0.3 mm from edges/slots, nothing on bare pads. Copper-to-edge
0.3 mm (milled) / 0.5 mm (scored). Keep drill types <= 9 (>12 surcharges). Fill the
`electroconnect_blank_zakaza.xls` blank and attach it.

**Rezonit** ([single/double-sided](https://www.rezonit.ru/pcb/odnostoronnie-i-dvustoronnie/),
[tech capabilities](https://www.rezonit.ru/directory/tekhnologicheskie-osobennosti-proizvodstva/)).
Standard 2-layer process reaches 0.125/0.125 mm track/gap, 0.15 mm via annular ring, and
hole:thickness down to 1:7 -- this board (0.20/0.20, 0.40 mm holes) is well inside
standard, no special-process fee. Multiple mask/silk colors available if wanted; green
is cheapest. Upload gerbers + drill through their site quote.

---

## 7. Decide before ordering

Choices the fab form forces that the design does not fix:

- **Surface finish.** HASL is cheapest and fine here -- no fine-pitch/BGA parts; the
  DPAK and SOT-23-6 solder fine on HASL. Pick ENIG only if flatness or shelf life matters.
- **Copper weight.** Design basis is 1 oz: POWER traces are 1.5 mm, ~11 C rise at 3.33 A
  (KICAD_WORKFLOW section 5). 2 oz would lower that but is not needed and slightly
  stresses the 0.2 mm etch; **stay at 1 oz.**
- **Mask/silk color.** Green/white unless there is a reason otherwise.
- **Quantity.** Match the parts run. Note the fab minimum (~5).

Then run the section 4 checklist and submit.

---

## 8. Still open (from KICAD_WORKFLOW section 6, carried here)

- [ ] Confirm the drill-diameter convention with the chosen fab (finished vs tooling).
- [ ] Silkscreen text height: KiCad default 1.0 mm meets most fabs; PSElectro prefers
      >= 1.3 mm for readability. Spot-check the smallest reference designators; enlarge
      if any fall below 1.0 mm.
- [ ] Devboard pin order still unconfirmed -- a wiring issue at bring-up, not a board
      issue; does not block fabrication (J3 is flying-lead).
