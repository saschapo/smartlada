# SmartLada Rev A — carrier board for nanoESP32-C6

Minimal DIY carrier that replaces the **4x D4184 module + loose wiring**. The
**MuseLab nanoESP32-C6 devboard is kept as-is** and connects through an 8-pin header
(J3) on flying leads.

Two decisions, taken deliberately and for different reasons:

- **Not integrating the ESP32-C6-WROOM-1 module.** The devboard is already proven (3 h
  continuous 30 kHz run, 2026-07-11), and keeping it removes the RF keepout, USB-C,
  EN/RC, BOOT/RESET and module decoupling from this board entirely.
- **Not socketing the devboard either, yet.** Its physical pin order is unconfirmed and
  the board on order differs from the one the available drawings describe. J3 lets the
  power stage be laid out and built now, with the pin mapping deferred to wiring time.

Both are Rev B topics, to revisit once the power stage has been driven and the real
devboard is in hand.

Supply: **Ecola B2L080ESB**, 12 V DC regulated, 80 W / 6.6 A (indoor, not a vehicle
bus). Load: 4x R10W 12 V BA15S, 0.83 A/channel, 3.33 A / 40 W total.

Firmware: **no changes required.** Pins stay `GPIO0..3` (PWM), `GPIO8` (WS2812, on the
devboard), `GPIO9` (button, on the devboard).

---

## 1. What the carrier does and does not contain

| Block | In Rev A | Why |
|-------|:--------:|-----|
| 4x low-side MOSFET + gate network | yes | the actual point of the board |
| 12 V input, fuse, reverse polarity | yes | one wiring mistake must not kill the devboard |
| 12 V -> 5 V buck | yes | feeds the devboard's onboard AMS1117 |
| J3 header for the devboard | yes | flying leads; sockets deferred to Rev B |
| Lamp + supply screw terminals | yes | — |
| I2C header (J4) | **no** | dropped from Rev A — see 3.4. J3.8 reserved for its 3V3 |
| USB, ESD, EN/RC, BOOT/RESET, WS2812 | **no** | all present on the devboard |
| TVS on 12 V | **no** | regulated indoor PSU, resistive load, no transient source |
| Drain-source snubbers | **no** | incandescent lamps are resistive; add in Rev B only if EMI shows up |
| ADS1115, NTC, 12 V sense | **no** | firmware does not use them, and the 3 h thermal run showed no heating |

---

## 2. Power architecture — the one non-obvious decision

From the devboard schematic (`hardware/nanoESP32C6.pdf`, sheet `nanoESP32-C6 v1.0`):

```text
J4 VBUS (CH343 USB-C) --+
                        +-- F1 --+-- 5V (header pin) --+-- U3 AMS1117-3.3 --> 3V3
J5 VBUS (native USB-C) -+                              |
                                                       +-- (nothing else)
```

Note: `J4`, `J5`, `F1` and `U3` above are **the devboard's own designators**, from its
schematic. They are unrelated to this carrier's `J1..J3` and `F1`.

**The header's `5V` pin is tied straight to both USB VBUS rails through F1, with no
blocking diode.** So driving that pin from our buck would put our output in direct
contention with a laptop's USB VBUS whenever someone plugs USB in to flash or watch
logs — which is exactly what you will do, constantly.

Fix: **buck set to 5.0 V, feeding the `5V` pin through a series Schottky D1 (SS14).**

```text
12V --> U2 buck (5.0 V) --> D1 SS14 --> ~4.65 V --> header 5V pin --> AMS1117 --> 3V3
```

Why this works in all three cases:

| Situation | Result |
|-----------|--------|
| 12 V only | 4.65 V into AMS1117. Dropout ~1.1 V at our current -> 3.3 V holds |
| USB only | D1 reverse-biased, buck isolated. Devboard runs from VBUS as it always did |
| Both | 5.0 V VBUS beats 4.65 V, so USB silently wins. No back-feed into the laptop, no contention |

Do **not** feed the `3V3` pin instead — that back-drives the AMS1117 output, which LDOs
tolerate poorly, and creates the same contention one rail lower.

`3V3` on the header is an **output** here; use it only for the I2C pull-ups.

---

## 3. BOM

Passives 0805 unless noted, X7R or better, 1% on the feedback divider.

### 3.1 12 V input

| Ref | Qty | Value / PN | Package | Notes |
|-----|-----|------------|---------|-------|
| J1 | 1 | screw terminal 2-pos, 5.08 mm | THT | 12 V IN |
| F1 | 1 | T4A slow-blow 5x20 mm + clips | THT | slow-blow is mandatory: cold-filament inrush ~8-10 A per lamp |
| Q5 | 1 | **AOD4185** | TO-252 (DPAK) | P-channel reverse-polarity FET in the +12 V line |
| R1 | 1 | 100 k | 0805 | Q5 gate pull-down to GND |
| C1 | 1 | 100 uF / 25 V | THT electrolytic | 12 V bulk; buffers the 30 kHz switching of 3.33 A |
| C2 | 1 | 100 nF / 50 V | 0805 | HF bypass |

No TVS and no gate zener: with no clamping device on the bus, Q5 sees Vgs = 12 V,
comfortably inside its +/-20 V rating.

### 3.2 12 V -> 5 V buck

| Ref | Qty | Value / PN | Package | Notes |
|-----|-----|------------|---------|-------|
| U2 | 1 | **TPS54202DDCR** | SOT-23-6 | 4.5-28 V in, 2 A, synchronous, 500 kHz. Pins: 1 GND, 2 SW, 3 VIN, 4 FB, 5 EN, 6 BOOT |
| L1 | 1 | 10 uH, >=2 A sat, shielded | 6x6 mm | CDRH6D28NP-100NC. See note below |
| C3, C4 | 2 | 10 uF / 50 V X5R | 1210 | Vin, immediately adjacent to U2. Datasheet asks for >10 uF ceramic |
| C5 | 1 | 100 nF | 0603 | bootstrap, SW to BOOT — mandatory |
| C6, C7 | 2 | 22 uF / 16 V | 0805 | Vout, before D1 |
| R2 | 1 | 100 k 1% | 0805 | FB upper |
| R3 | 1 | 13.7 k 1% | 0805 | FB lower. Vref 0.596 V -> Vout = 4.95 V. **Trim on first board** |
| D1 | 1 | **SS14** | SMA | series Schottky into the devboard 5 V pin. See section 2 |

**EN is left floating** — SLVSD26C, table 4-1: "Float the EN pin to enable." An internal
pull-up current source enables the device. Place a no-connect flag so ERC stays quiet.

**On the inductor value.** TI's own design example lands on 15 uH, but that is for
VIN(max) = 28 V and IOUT = 2 A. At our 12 V in / 5 V out / ~0.4 A, 10 uH gives ripple
`dIL = (12-5) x 0.4167 / (10u x 500k) = 0.58 A` p-p, peak coil current ~0.7 A against a
2.5 A (min) internal limit, and output ripple `~0.58/(8 x 500k x 25u) = 6 mV`. 10 uH is
fine here; the converter will pulse-skip at light load, which is a documented mode.

Load: C6 RF peak ~350 mA + WS2812 ~60 mA. 2 A is generous; the AMS1117 dissipates
~0.55 W at 4.65 V in / 3.3 V out / 400 mA, which its SOT-223 handles.

### 3.3 Power switches — 4 identical channels (n = 0..3)

| Ref | Qty | Value / PN | Package | Notes |
|-----|-----|------------|---------|-------|
| Q1..Q4 | 4 | **AOD4184A** | TO-252 (DPAK) | N-ch, 40 V, logic-level. Same die family as the proven D4184 module |
| Rg1..Rg4 | 4 | 100 R | 0805 | gate series, same value as the module. 3.3 V / 100 R = 33 mA, under the 40 mA GPIO limit |
| Rpd1..Rpd4 | 4 | 100 k | 0805 | **gate pull-down — critical, do not omit** |
| TP_G0..3 | 4 | 1 mm test pad | copper only | gate probe, not a part |

One FET per channel is ample: the module used two in parallel to reach a 15 A rating,
not for a 0.83 A load. A single die also halves Qg, so switching loss at 30 kHz is
*lower* than the present setup.

### 3.4 Connectors

| Ref | Qty | Value | Notes |
|-----|-----|-------|-------|
| J2 | 1 | screw terminal 5-pos, 5.08 mm | COM +12 V, CH0-, CH1-, CH2-, CH3- |
| J3 | 1 | male header 2.54 mm, 8-pin single row | **MCU interface — flying leads to the devboard.** See section 4 |
| H1..H4 | 4 | M3 mounting hole, 3.2 mm | corners, 5 mm inset from the edges |
**J4 (I2C service header) is not fitted in Rev A.** It was the only consumer of the
devboard's 3.3 V, so `+3V3_DEV` is dropped with it and `J3.8` is left as a reserved,
unconnected pin (section 4.1). Nothing else on the board wanted that rail.

If I2C comes back in Rev B, note that **there are no pull-ups anywhere in this system.**
Verified against the devboard schematic: its only resistors are 10 k on `CHIP_PU` and
`GPIO9` (EN and BOOT), 22 R in series with the USB lines, and 5.1 k CC resistors on both
USB-C receptacles. `GPIO18` and `GPIO19` are plain unloaded pins. Most breakout modules
carry their own pull-ups; a bare sensor chip will not communicate without two 4.7 k to
3.3 V.
| TP1..TP4 | 4 | 1 mm test pads | 12V, 5V, 3V3, GND |

**Distinct part numbers: 13.** Total placements: ~28.

---

## 4. MCU interface — J3

Rev A does **not** socket the devboard. The devboard sits loose and is wired to J3 with
8 dupont leads.

Reason: the devboard's physical pin order is still unconfirmed (section 7), and the
board on order is not the one the available drawings describe. Committing socket
footprints now would gate the whole PCB on a fact we do not have. An 8-pin header does
not care about pin order — the mapping is decided at assembly time, with wires.
Socketing is a Rev B decision, once the actual board is in hand and measured.

### 4.1 J3 pinout

| Pin | Net | Dir | Wire to devboard |
|----:|-----|-----|------------------|
| 1 | +5V_DEV | **out** | `5V` pin |
| 2 | GND | out | any `G` pin |
| 3 | CH0_IN | in | `GPIO0` — stop |
| 4 | CH1_IN | in | `GPIO1` — reverse |
| 5 | CH2_IN | in | `GPIO2` — turn |
| 6 | CH3_IN | in | `GPIO3` — marker |
| 7 | GND | out | any `G` pin |
| 8 | *(reserved)* | — | **nothing — leave unwired** |

Seven flying leads, not eight. Pin 8 is a reserved pad: it carried the devboard's 3.3 V
solely to feed the J4 pull-ups, and J4 is not fitted in Rev A. The pad stays so that pin
numbering does not shift if I2C returns in Rev B. Give it a no-connect flag in the
schematic and mark it `NC` on the silkscreen so nobody lands a wire on it.

Silkscreen must mark pin 1 and its direction: pin 1 **sources** ~400 mA into the
devboard.

Two grounds, at pins 2 and 7, are deliberate: they flank the four signal lines so each
gate drive has a return path close by. Use a ribbon or twisted bundle, not four wires
strung separately across the bench.

### 4.2 Why flying leads are electrically acceptable here

The wires carry **logic signals, not the gate nodes**. `Rg` and `Rpd` stay on the
carrier next to their own MOSFET, so the gate loop is short regardless of lead length,
and `Rg` damps ringing between the lead inductance and Cgs.

The 3.3 V / 100 R gate drive is 33 mA peak at 30 kHz — slow edges, modest current,
tolerant of 10-15 cm of wire. Do not extend this to half a metre.

The lamp current never touches these wires: 3.33 A returns through the carrier's PGND
polygon to J1. Only gate drive and the devboard's own ~400 mA supply flow through J3.

One useful property falls out of `Rpd` living at the gate: **if a dupont lead falls off,
that channel turns OFF, not on.** The same pull-down that covers the boot window covers
a disconnected wire.

### 4.3 Devboard pin order — for wiring, not for the PCB

The table below is the best available reading, from the **vendor mechanical drawing**
(dimensioned, `Unit: mm`), whose signal set matches the `HEADER` block of
`nanoESP32C6.pdf`. Two single-row 2.54 mm headers, **16 positions each**, order
top-to-bottom with the two USB-C connectors at the bottom edge.

**Treat it as provisional.** It describes a board that is not the one on order, and it
conflicts with a community colour pinout (section 7). Verify against the silkscreen of
the actual board before landing a single wire. With J3 this is a wiring mistake to
correct, not a PCB respin — which is the point.

| # | LEFT row | Wire to | # | RIGHT row | Wire to |
|--:|----------|---------|--:|-----------|---------|
| 1 | 3V3 | - *(Rev B: J3.8)* | 1 | G | **J3.2 or J3.7** |
| 2 | RST (CHIP_PU) | - | 2 | TX (GPIO16 / U0TXD) | - |
| 3 | GPIO4 | - strapping | 3 | RX (GPIO17 / U0RXD) | - |
| 4 | GPIO5 | - strapping | 4 | GPIO15 | - strapping |
| 5 | GPIO6 | - | 5 | GPIO23 | - |
| 6 | GPIO7 | - | 6 | GPIO22 | - |
| 7 | **GPIO0** | **J3.3 — CH0 stop** | 7 | GPIO21 | - |
| 8 | **GPIO1** | **J3.4 — CH1 reverse** | 8 | GPIO20 | - |
| 9 | GPIO8 | - RGB LED + strapping | 9 | GPIO19 | - *(Rev B: I2C SCL)* |
| 10 | GPIO10 | - | 10 | GPIO18 | - *(Rev B: I2C SDA)* |
| 11 | GPIO11 | - | 11 | GPIO9 | - BOOT button + strapping |
| 12 | **GPIO2** | **J3.5 — CH2 turn** | 12 | G | **J3.2 or J3.7** |
| 13 | **GPIO3** | **J3.6 — CH3 marker** | 13 | GPIO13 | - **USB D+** |
| 14 | **5V** | **J3.1** | 14 | GPIO12 | - **USB D-** |
| 15 | G | **J3.2 or J3.7** | 15 | G | **J3.2 or J3.7** |
| 16 | NC | - | 16 | NC | - |

**Never land a wire on these:**

- `GPIO12` / `GPIO13` — native USB D-/D+. A wire here breaks flashing and logging.
- `GPIO4`, `GPIO5`, `GPIO8`, `GPIO9`, `GPIO15` — strapping pins. `GPIO8` also drives the
  onboard WS2812 and `GPIO9` is the BOOT button; the firmware uses both as-is.
- `3V3` — output of the onboard AMS1117. It feeds J3.8; never drive it from the carrier.

### 4.4 Devboard mechanical envelope — informational, for Rev B

| Dimension | Value |
|-----------|-------|
| Board outline | **25.40 x 51.80 mm** |
| Header row spacing | **22.86 mm (0.9")** |
| Header pitch | 2.54 mm, 16 positions per row |
| Module (MINI-ANT-TYPEB) width | 18.00 mm, 6.35 mm from the top edge |
| First header pin from top edge | 6.35 mm |
| Edge to header centre | 1.27 mm |

Not used in Rev A. **The reserve was considered and dropped:** a 26 x 52 mm clear
rectangle does not fit on the final 80 x 65 mm outline alongside the components, and
Rev A wires the devboard with flying leads anyway, so it sits beside the board. If Rev B
sockets the devboard the outline has to change regardless.

The numbers are kept here so they are not lost. If that path is taken, remember that
standard 2.54 mm sockets give about 8.5 mm of clearance — the 100 uF electrolytic C1 and
the fuse holder F1 are the tall parts and must stay outside the footprint, and the
devboard's bottom edge (both USB-C) has to face outward.

---

## 5. Draft schematic — net by net

### 5.1 Nets

```text
+12V_IN     J1.1 -> F1.1                      3.33 A
+12V_FUSED  F1.2 -> Q5.D                      3.33 A
+12V        Q5.S -> C1, C2, C3, C4, U2.VIN, J2.1 (LAMP_COM)   3.33 A
+5V_SW    U2 output -> D1 anode
+5V_DEV   D1 cathode -> J3.1
GND       logic ground; J3.2, J3.7
PGND      lamp return; joined to GND at ONE star point near J1
CH0..3_IN J3.3..J3.6 -> Rg -> GATE0..3
GATE0..3  Rg output, Rpd to PGND, Q gate, TP_G0..3
LAMP0..3  J2.2..J2.5 -> Q1..Q4 drain
```

### 5.2 Input stage

```text
 J1.1 --[F1 T4A]--+-- Q5.D                 Q5 = AOD4185, P-channel
                  |     |
                  |   Q5.G --[R1 100k]-- GND
                  |     |
                  |   Q5.S --+---- +12V bus --> U2.Vin
                  |          |               --> J2.1 LAMP_COM
                  |     [C1 100u] [C2 100n]
                  |          |        |
 J1.2 ------------+---------GND------GND
```

**Q5 orientation — get this right, it is the classic mistake.** The body diode of a
P-channel runs anode on drain, cathode on source, i.e. it conducts drain -> source. For
the protection to work the body diode must point **along** the normal current direction,
so that reverse polarity finds it blocking. Normal current goes input -> load, therefore:

**drain to the incoming +12 V, source to the board bus.**

| | Normal | Reverse polarity |
|---|--------|------------------|
| Body diode | forward, pulls the bus up to ~11.4 V | drain at -12 V, source ~0 V -> **blocked** |
| Vgs | source ~12 V, gate 0 V -> -12 V -> **on**, shorting out the diode | 0 V -> **off** |

Wired the other way round (source to input) the board still works on the bench, and
still fails on reverse: Vgs goes to +12 V and the channel closes correctly, but the body
diode is then forward-biased and passes the reverse current straight through the load.
The failure only shows up on the one event the part exists for.

### 5.3 Per-channel switch (x4)

```text
      +12V ------------------> J2.1  LAMP_COM
                                 |
                            [ R10W lamp ]   (external)
                                 |
                            J2.(n+2)  LAMPn
                                 |
  J3.(n+3) CHn_IN --[Rg 100R]--+-- GATE n   |
                            |               |
                        [Rpd 100k]      Q(n) drain
                            |          +----+----+
                            |          | AOD4184A|
                            |          +----+----+
                            |            source
                           PGND ------------+
```

`Rpd` sits physically **at the gate**, not on the MCU side of `Rg`. This is what holds
the lamps off in the window between reset and `platform_pwm::forceAllLow()`, while
`GPIO0..3` are still floating inputs.

No flyback diode: incandescent lamps are resistive.

---

## 6. Layout rules that matter here

1. **Single-point ground join.** PGND carries up to 3.33 A with 30 kHz edges. Route it
   to the J1 return without passing under U2 or the devboard. Join PGND and GND at one
   point at J1.
2. **SW node small.** U2's SW copper is the only real radiator on this board. Follow the
   TPS54202 datasheet layout figure literally.
2a. **FB ground is a separate return.** SLVSD26C, pin 1 description: "Connect sensitive
   VFB to this GND at a single point." Route R3's ground to U2's GND pin directly rather
   than dropping it into the nearest pour.
3. **Gate loops short.** Rg and Rpd next to their own FET; gate return into local PGND.
   Put the four MOSFETs in a column with J3 on one side of it and the lamp terminal J2
   immediately outboard on the other, so `CHn_IN` runs J3 -> Rg -> gate with no detour.
   The 12 V input and the buck take the far side of the board.
7. **J3's ground is the reference for gate drive.** Tie J3.2 / J3.7 to the quiet logic
   ground near the buck output, **not** into the middle of the lamp return path. If the
   devboard's ground reference sits somewhere along the 3.33 A PGND run, the IR drop
   across that copper appears directly as gate-drive noise.
4. **Copper for 3.33 A — and that means all three input segments.** The fuse and Q5 each
   split the input rail, so the 3.33 A path is three separate nets: `+12V_IN` (J1 to F1),
   `+12V_FUSED` (F1 to Q5 drain) and `+12V` (Q5 source onward). A `+12V` copper pour
   covers only the last one — it cannot and must not bridge the other two, or it would
   short out the fuse and the reverse-polarity FET.

   All three need POWER-class width. At 1 oz and 10 C rise, IPC-2221 gives ~1.6 mm for
   3.33 A; the `POWER` netclass is set to 1.5 mm, which is ~11 C. For reference, the
   0.2 mm `Default` width at 3.33 A works out to roughly a 300 C rise — that trace is a
   fuse, not a conductor. **DRC does not check current**, so this is on you, not the tool.

   Netclass patterns must use leading wildcards (`*12V*`, `*LAMP*`): local labels on the
   root sheet become `/LAMP0`, `/+12V_IN` etc., while power symbols stay bare (`+12V`,
   `GND`). A pattern of `LAMP*` may miss `/LAMP0`.
5. **Thermal vias** under the Q1..Q5 DPAK tabs. Calculated dissipation is low, but they
   cost nothing and hedge the cold-filament inrush case.
6. Keep the buck away from the devboard's antenna end.

---

## 6a. As built

Board **80 x 65 mm**, 2 layers, 1 oz, 1.6 mm. ERC 0/0, DRC 0 violations / 0 unconnected /
0 footprint errors. All signals on F.Cu (323.5 mm); B.Cu carries only a 5.3 mm feedback
ground link, leaving the GND pour as one uninterrupted island covering 93% of the board.
8 vias, all GND. Silkscreen is functional and 0-based, matching the firmware:
`LAMP 0: STOP / 1: REV / 2: TURN / 3: MARKER` and `GPIO_0_stop .. GPIO_3_marker`.

Process notes, KiCad traps and the full list of errors caught in review are in
[KICAD_WORKFLOW.md](KICAD_WORKFLOW.md).

---

## 7. Open items

- [ ] **Devboard pin order — no longer blocks the PCB, still blocks wiring.** Three
      readings disagree: the vendor mechanical drawing in section 4.3 (16 pins/row,
      `GPIO2`/`GPIO3` on the left), a community colour pinout (15 pins/row,
      `GPIO2`/`GPIO3` on the right), and the board actually on order, which is neither.
      All carry the same signal *set*, so the schematic cannot arbitrate — only the
      board can. Read the silkscreen when it arrives. Getting it wrong swaps channels
      and can put a gate drive on the USB D+ line, so **verify before landing wires** —
      but with J3 that is a five-minute rewire, not a respin.
- [ ] **AOD4184A Rds(on) and Qg at Vgs = 3.3 V**, not the datasheet's 4.5 V / 10 V
      figures. Recompute the 30 kHz switching loss from
      [mosfet_switch_D4184.md](../SmartLadaC6/docs/mosfet_switch_D4184.md) for a single die.
- [ ] **AOD4184A SOA at inrush** (~8-10 A for tens of ms). Firmware soft-start limits
      this; confirm the FET survives `soft_start_ms = 0` being set by mistake.
- [ ] **The 5 V window is tight — measure and trim R3 on the first board.** Verified
      against SLVSD26C: Vref is 0.581 / 0.596 / 0.611 V (min/typ/max), so R2 = 100 k +
      R3 = 13.7 k gives 4.95 V typical but roughly **4.68-5.22 V** across the Vref spread
      plus 1% resistors. Minus the SS14 drop (~0.4 V) that is **4.3-4.8 V** at the
      devboard `5V` pin, against a required window of:
      - **>= ~4.4 V**, or the AMS1117 cannot hold 3.3 V at peak current;
      - **< USB VBUS**, or the board back-feeds the laptop instead of yielding to it.

      Worst-case corners fall outside on both ends. Not a redesign — R3 is one resistor
      — but do not assume it lands correctly. Measure the buck output on the first
      board and pick R3 to put the post-diode voltage near 4.6 V.
- [ ] **AMS1117 dropout** at ~4.6 V in and the devboard's real peak current, from the
      specific vendor's datasheet. This sets the lower edge of the window above.

---

## 8. Explicitly deferred

Per-channel current sense, NTC / thermal cutout, 12 V monitoring, WROOM-1 integration,
CAN/LIN, automotive front end. See sections 4 and 8 of
[smartlada_custom_pcb_kicad.md](smartlada_custom_pcb_kicad.md).

---

**Sources:** [wuxx/nanoESP32-C6](https://github.com/wuxx/nanoESP32-C6/) and its
[hardware/nanoESP32C6.pdf](https://github.com/wuxx/nanoESP32-C6/blob/master/hardware/nanoESP32C6.pdf)
schematic; project docs `board_nanoESP32-C6.md`, `mosfet_switch_D4184.md`.
