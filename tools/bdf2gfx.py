#!/usr/bin/env python3
"""Convert every BDF in fonts/ into a single Adafruit_GFX header (fonts_all.h).

Only the printable ASCII range 0x20..0x7E is emitted (keeps flash small and is
all the SmartLada UI needs -- identifiers/labels are ASCII). Missing glyphs in
that range become blank entries so first..last stays contiguous.
"""
import glob
import os
import re
import sys

FIRST = 0x20
LAST = 0x7E


def sanitize(name):
    s = re.sub(r"[^0-9A-Za-z]", "_", name)
    if s and s[0].isdigit():
        s = "f_" + s
    return s


def parse_bdf(path):
    """Return (glyphs dict enc->glyph, yAdvance).

    glyph = dict(width,height,xadv,xoff,yoff,rows[list of int per row, MSB-left]).
    """
    glyphs = {}
    y_advance = None
    ascent = descent = None
    fbb_h = None
    with open(path, "r", encoding="latin-1") as fh:
        lines = fh.read().splitlines()

    i = 0
    cur = None
    reading_bitmap = False
    while i < len(lines):
        ln = lines[i]
        parts = ln.split()
        key = parts[0] if parts else ""

        if key == "FONTBOUNDINGBOX":
            fbb_h = int(parts[2])
        elif key == "FONT_ASCENT":
            ascent = int(parts[1])
        elif key == "FONT_DESCENT":
            descent = int(parts[1])
        elif key == "STARTCHAR":
            cur = {"enc": None, "bbx": None, "dwidth": None, "rows": []}
            reading_bitmap = False
        elif key == "ENCODING" and cur is not None:
            cur["enc"] = int(parts[1])
        elif key == "DWIDTH" and cur is not None:
            cur["dwidth"] = int(parts[1])
        elif key == "BBX" and cur is not None:
            cur["bbx"] = [int(x) for x in parts[1:5]]  # w h xoff yoff
        elif key == "BITMAP" and cur is not None:
            reading_bitmap = True
        elif key == "ENDCHAR" and cur is not None:
            enc = cur["enc"]
            if enc is not None and cur["bbx"] is not None:
                w, h, xoff, yoff = cur["bbx"]
                glyphs[enc] = {
                    "width": w,
                    "height": h,
                    "xadv": cur["dwidth"] if cur["dwidth"] is not None else w,
                    "xoff": xoff,
                    "yoff": yoff,
                    "rows": cur["rows"],
                }
            cur = None
            reading_bitmap = False
        elif reading_bitmap and cur is not None and re.fullmatch(r"[0-9A-Fa-f]+", ln.strip() or ""):
            cur["rows"].append(int(ln.strip(), 16))
        i += 1

    if ascent is not None and descent is not None:
        y_advance = ascent + descent
    elif fbb_h is not None:
        y_advance = fbb_h
    else:
        y_advance = 8
    return glyphs, y_advance


def read_pcf(path):
    """Parse an X11 PCF font into the same shape as parse_bdf().

    Only the tables needed for glyph rendering are decoded (metrics, bitmaps,
    encodings, accelerators). Handles compressed metrics and both byte/bit
    orders; glyph padding and multi-byte scan units are honoured.
    """
    with open(path, "rb") as fh:
        buf = fh.read()

    def le32(o):
        return buf[o] | (buf[o + 1] << 8) | (buf[o + 2] << 16) | (buf[o + 3] << 24)

    assert buf[:4] == b"\x01fcp", "not a PCF file: " + path
    ntables = le32(4)
    toc = {}
    o = 8
    for _ in range(ntables):
        ttype = le32(o); tsize = le32(o + 8); toff = le32(o + 12)
        toc[ttype] = (toff, tsize)
        o += 16

    def reader(off):
        fmt = le32(off)                     # table format is always stored LSB
        byte_msb = bool(fmt & (1 << 2))
        bit_msb = bool(fmt & (1 << 3))
        glyph_pad = 1 << (fmt & 3)
        scan_unit = 1 << ((fmt >> 4) & 3)

        def u16(p):
            return (buf[p] << 8 | buf[p + 1]) if byte_msb else (buf[p] | buf[p + 1] << 8)

        def s16(p):
            v = u16(p); return v - 0x10000 if v & 0x8000 else v

        def u32(p):
            if byte_msb:
                return buf[p] << 24 | buf[p + 1] << 16 | buf[p + 2] << 8 | buf[p + 3]
            return buf[p] | buf[p + 1] << 8 | buf[p + 2] << 16 | buf[p + 3] << 24

        return fmt, byte_msb, bit_msb, glyph_pad, scan_unit, u16, s16, u32

    # ---- METRICS (type 4) ----
    moff = toc[4][0]
    fmt, byte_msb, _, _, _, u16, s16, u32 = reader(moff)
    p = moff + 4
    metrics = []  # (lsb, rsb, width, ascent, descent)
    if fmt & 0x100:                         # PCF_COMPRESSED_METRICS
        count = u16(p); p += 2
        for _ in range(count):
            lsb = buf[p] - 0x80; rsb = buf[p + 1] - 0x80; w = buf[p + 2] - 0x80
            asc = buf[p + 3] - 0x80; desc = buf[p + 4] - 0x80
            metrics.append((lsb, rsb, w, asc, desc)); p += 5
    else:
        count = u32(p); p += 4
        for _ in range(count):
            lsb = s16(p); rsb = s16(p + 2); w = s16(p + 4)
            asc = s16(p + 6); desc = s16(p + 8)
            metrics.append((lsb, rsb, w, asc, desc)); p += 12

    # ---- BITMAPS (type 8) ----
    boff = toc[8][0]
    fmt, byte_msb, bit_msb, glyph_pad, scan_unit, u16, s16, u32 = reader(boff)
    p = boff + 4
    gcount = u32(p); p += 4
    offsets = [u32(p + 4 * i) for i in range(gcount)]; p += 4 * gcount
    bmsizes = [u32(p + 4 * i) for i in range(4)]; p += 16
    data_start = p
    total = bmsizes[fmt & 3]

    def glyph_rows(idx):
        lsb, rsb, w, asc, desc = metrics[idx]
        gw = rsb - lsb; gh = asc + desc
        start = offsets[idx]
        end = offsets[idx + 1] if idx + 1 < gcount else total
        gbytes = end - start
        if gh <= 0 or gw <= 0 or gbytes <= 0:
            return gw, gh, lsb, desc, []
        bpr = gbytes // gh
        rows = []
        rowbytes_target = (gw + 7) // 8
        for r in range(gh):
            base = data_start + start + r * bpr
            raw = bytearray(buf[base:base + bpr])
            if scan_unit > 1 and not byte_msb:  # swap bytes within each scan unit
                for k in range(0, len(raw) - scan_unit + 1, scan_unit):
                    raw[k:k + scan_unit] = raw[k:k + scan_unit][::-1]
            val = 0
            for c in range(gw):
                byte = raw[c // 8] if (c // 8) < len(raw) else 0
                bit = (byte >> (7 - (c % 8))) & 1 if bit_msb else (byte >> (c % 8)) & 1
                if bit:
                    val |= 1 << (rowbytes_target * 8 - 1 - c)
            rows.append(val)
        return gw, gh, lsb, desc, rows

    # ---- BDF_ENCODINGS (type 32) ----
    eoff = toc[32][0]
    _, byte_msb, _, _, _, u16, s16, u32 = reader(eoff)
    p = eoff + 4
    min2 = u16(p); max2 = u16(p + 2); min1 = u16(p + 4); max1 = u16(p + 6)
    p += 10                                  # skip default_char too
    cols = max2 - min2 + 1

    def enc_index(code):
        if code < min2 or code > max2 or min1 != 0 or max1 != 0:
            return 0xFFFF
        return u16(p + 2 * (code - min2))

    # ---- yAdvance from (BDF_)ACCELERATORS (type 256 preferred, else 2) ----
    y_advance = None
    for atype in (256, 2):
        if atype in toc:
            aoff = toc[atype][0]
            _, byte_msb, _, _, _, u16, s16, u32 = reader(aoff)
            fa = u32(aoff + 4 + 8); fd = u32(aoff + 4 + 12)
            y_advance = fa + fd
            break

    glyphs = {}
    max_asc = max_desc = 0
    for code in range(FIRST, LAST + 1):
        gi = enc_index(code)
        if gi == 0xFFFF or gi >= len(metrics):
            continue
        gw, gh, lsb, desc, rows = glyph_rows(gi)
        _, _, cw, asc, dsc = metrics[gi]
        max_asc = max(max_asc, asc); max_desc = max(max_desc, dsc)
        glyphs[code] = {
            "width": max(gw, 0),
            "height": max(gh, 0),
            "xadv": cw,
            "xoff": lsb,
            "yoff": -desc,                   # BDF yoff = baseline to bottom of bbox
            "rows": rows,
        }
    if not y_advance:
        y_advance = max_asc + max_desc
    return glyphs, y_advance


def load_font(path):
    if path.lower().endswith(".pcf"):
        return read_pcf(path)
    return parse_bdf(path)


def glyph_bits(g):
    """Yield the glyph's pixels as a continuous MSB-first bitstream (row-major)."""
    w, h = g["width"], g["height"]
    row_bytes = (w + 7) // 8
    rows = g["rows"]
    for r in range(h):
        val = rows[r] if r < len(rows) else 0
        # BDF row is left-justified in row_bytes*8 bits, MSB = leftmost pixel.
        for c in range(w):
            byte_index = c // 8
            bit_index = 7 - (c % 8)
            shift = (row_bytes - 1 - byte_index) * 8 + bit_index
            yield (val >> shift) & 1


def convert(path):
    glyphs, y_advance = load_font(path)
    base = sanitize(os.path.splitext(os.path.basename(path))[0])

    bitmap = bytearray()
    bit_acc = 0
    bit_cnt = 0

    def flush_byte():
        nonlocal bit_acc, bit_cnt
        while bit_cnt >= 8:
            bit_cnt -= 8
            bitmap.append((bit_acc >> bit_cnt) & 0xFF)
            bit_acc &= (1 << bit_cnt) - 1

    entries = []  # (offset,width,height,xadv,xoff,yoff)
    for code in range(FIRST, LAST + 1):
        offset = len(bitmap) + (1 if bit_cnt else 0)  # offset of next full byte start
        offset = len(bitmap)
        # pad current partial byte before a new glyph so offsets are byte-aligned
        if bit_cnt:
            bitmap.append((bit_acc << (8 - bit_cnt)) & 0xFF)
            bit_acc = 0
            bit_cnt = 0
        offset = len(bitmap)

        g = glyphs.get(code)
        if g is None or g["width"] == 0 or g["height"] == 0:
            xadv = g["xadv"] if g else max(3, (y_advance // 2))
            entries.append((offset, 0, 0, xadv, 0, 0))
            continue
        for bit in glyph_bits(g):
            bit_acc = (bit_acc << 1) | bit
            bit_cnt += 1
            flush_byte()
        # yOffset: distance from baseline (cursor) up to glyph top row.
        y_off = -(g["height"] + g["yoff"])
        entries.append((offset, g["width"], g["height"], g["xadv"], g["xoff"], y_off))

    if bit_cnt:
        bitmap.append((bit_acc << (8 - bit_cnt)) & 0xFF)

    return base, bitmap, entries, y_advance


def emit(paths, out):
    with open(out, "w", encoding="ascii") as w:
        w.write("// Auto-generated by bdf2gfx.py -- do not edit.\n")
        w.write("// Adafruit_GFX fonts for the ASCII range 0x20..0x7E.\n")
        w.write("#pragma once\n#include <Adafruit_GFX.h>\n#include <pgmspace.h>\n\n")

        table = []  # (display_name, base)
        for path in paths:
            base, bitmap, entries, yadv = convert(path)
            name = os.path.splitext(os.path.basename(path))[0]

            w.write("// ---- %s ----\n" % name)
            w.write("static const uint8_t %s_Bitmaps[] PROGMEM = {\n" % base)
            for k in range(0, len(bitmap), 16):
                chunk = bitmap[k:k + 16]
                w.write("  " + ", ".join("0x%02X" % b for b in chunk) + ",\n")
            if not bitmap:
                w.write("  0x00,\n")
            w.write("};\n")

            w.write("static const GFXglyph %s_Glyphs[] PROGMEM = {\n" % base)
            for (off, gw, gh, xadv, xoff, yoff) in entries:
                w.write("  {%d, %d, %d, %d, %d, %d},\n" % (off, gw, gh, xadv, xoff, yoff))
            w.write("};\n")

            w.write(
                "static const GFXfont %s PROGMEM = {(uint8_t*)%s_Bitmaps, (GFXglyph*)%s_Glyphs, 0x%02X, 0x%02X, %d};\n\n"
                % (base, base, base, FIRST, LAST, yadv)
            )
            table.append((name, base))

        w.write("struct FontEntry { const GFXfont* font; const char* name; };\n")
        w.write("static const FontEntry FONT_TABLE[] = {\n")
        for (name, base) in table:
            w.write('  {&%s, "%s"},\n' % (base, name))
        w.write("};\n")
        w.write("static const uint8_t FONT_COUNT = sizeof(FONT_TABLE)/sizeof(FONT_TABLE[0]);\n")
        return len(table)


if __name__ == "__main__":
    fonts_dir = sys.argv[1]
    out = sys.argv[2]
    if len(sys.argv) > 3:
        # Explicit ordered list of basenames -> curated header, order preserved.
        paths = [os.path.join(fonts_dir, name) for name in sys.argv[3:]]
    else:
        paths = sorted(glob.glob(os.path.join(fonts_dir, "*.bdf")))
    n = emit(paths, out)
    print("wrote %d fonts to %s" % (n, out))
