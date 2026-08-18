SmartLada RevC - Assembly notes (for PCBA quote)
=================================================

Files:
- smartlada_revC_BOM_pcba.csv  : 26 lines, 61 parts to place + 4 DNP
- smartlada_revC_CPL_pcba.csv  : 61 placements (Designator, Mid X, Mid Y, Layer, Rotation)
- Gerbers/drill: in the separate fab zip.

General:
- Single board 100 x 59 mm, 2 layers. ALL components on TOP side.
- LCSC column is blank on purpose: please match by MPN, or suggest LCSC equivalents.

Do NOT populate (DNP):
- TP1..TP4 - bare test pads, no component.

Through-hole parts (hand / wave solder):
- J1  USB-C receptacle (16P, TYPE-C-31-M-12)
- J2,J3,J5,J6,J7  Faston 6.3 mm tabs (63951-1)
- J4  pin header 1x8 right-angle 2.54 mm
- J8  pin header 1x3 right-angle 2.54 mm
Please confirm if you can hand-solder these or if we should exclude them.

Customer can supply:
- U1  ESP32-C6-WROOM-1-N16 module (we already have 5 pcs) - optional, tell us if you prefer to source it.

Polarity / orientation - verify carefully:
- C11 electrolytic (+ mark)
- D1..D6 LED (cathode)
- U1..U4 and Q1..Q4 (pin 1)
- Rotations in the CPL are KiCad convention (degrees, CCW positive), same origin as the gerbers.
  Please re-check rotation of SOT-23-6 (U2,U4), SSOP-10 (U3), DPAK (Q1-4), LED, and the
  electrolytic against your placement library.

Notes:
- U3 CH224K is SSOP-10 with an exposed thermal pad (EP) under the body - needs paste on the EP.
- Board has plated slots (USB-C) and non-plated holes (mounting + connector pegs) - see drill files.
