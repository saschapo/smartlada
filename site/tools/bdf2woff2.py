# /// script
# dependencies = ["fonttools", "brotli"]
# ///
"""bdf2woff2.py - turn a bitmap BDF font into a pixel-exact woff2 for the web.

Every lit pixel becomes a square (horizontal runs are merged per row), so at a font-size that is
an integer multiple of the BDF pixel size the glyphs land on whole CSS pixels.

  uv run site/tools/bdf2woff2.py fonts/tecate/orp-bold.bdf site/public/fonts/orp-bold.woff2 --name "orp Bold"

Only ASCII, Cyrillic and a few punctuation marks are kept (the page needs nothing else).
"""
import argparse
import sys

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

KEEP = set(range(0x20, 0x7F)) | set(range(0x410, 0x450)) | {
    0x401, 0x451, 0xA0, 0xAB, 0xBB, 0xB7, 0xB0, 0xD7, 0x2014, 0x2013, 0x2116, 0x2192, 0x2026}
UNIT = 100  # font units per BDF pixel


def parse_bdf(path):
    glyphs, props, cur, rows = {}, {}, None, None
    for line in open(path, encoding="latin-1"):
        p = line.split()
        if not p:
            continue
        k = p[0]
        if cur is None and k in ("FONT_ASCENT", "FONT_DESCENT", "PIXEL_SIZE"):
            props[k] = int(p[1])
        elif k == "STARTCHAR":
            cur = {"enc": -1}
        elif k == "ENCODING":
            cur["enc"] = int(p[1])
        elif k == "DWIDTH":
            cur["adv"] = int(p[1])
        elif k == "BBX":
            cur["bbx"] = tuple(int(v) for v in p[1:5])
        elif k == "BITMAP":
            rows = []
        elif k == "ENDCHAR":
            cur["rows"] = rows
            glyphs[cur["enc"]] = cur
            cur, rows = None, None
        elif rows is not None:
            rows.append(int(k, 16) if k else 0)
    return props, glyphs


def draw(g):
    pen = TTGlyphPen(None)
    w, h, xo, yo = g["bbx"]
    bits = ((w + 7) // 8) * 8
    for r, v in enumerate(g["rows"]):
        y = (yo + h - 1 - r) * UNIT  # bottom of this row
        x = 0
        while x < w:
            if v >> (bits - 1 - x) & 1:
                x0 = x
                while x < w and v >> (bits - 1 - x) & 1:
                    x += 1
                a, b = (xo + x0) * UNIT, (xo + x) * UNIT
                pen.moveTo((a, y)); pen.lineTo((a, y + UNIT)); pen.lineTo((b, y + UNIT)); pen.lineTo((b, y))
                pen.closePath()
            else:
                x += 1
    return pen.glyph()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bdf")
    ap.add_argument("out")
    ap.add_argument("--name", required=True)
    a = ap.parse_args()

    props, glyphs = parse_bdf(a.bdf)
    asc, desc = props["FONT_ASCENT"], props["FONT_DESCENT"]
    px = props.get("PIXEL_SIZE", asc + desc)
    upm = px * UNIT
    keep = sorted(c for c in glyphs if c in KEEP)

    order = [".notdef"] + [f"uni{c:04X}" for c in keep]
    cmap = {c: f"uni{c:04X}" for c in keep}
    empty = TTGlyphPen(None).glyph()
    glyf = {".notdef": empty, **{f"uni{c:04X}": draw(glyphs[c]) for c in keep}}
    adv = glyphs[keep[0]]["adv"] * UNIT
    metrics = {n: (adv, 0) for n in order}
    for c in keep:
        metrics[f"uni{c:04X}"] = (glyphs[c]["adv"] * UNIT, glyphs[c]["bbx"][2] * UNIT)

    # Line box = the font's pixel size; the rest of the ascent/descent split follows the BDF.
    top, bot = (px - desc) * UNIT, -desc * UNIT
    fb = FontBuilder(upm, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyf)
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=top, descent=bot)
    fb.setupOS2(sTypoAscender=top, sTypoDescender=bot, sTypoLineGap=0,
                usWinAscent=top, usWinDescent=-bot, fsType=0)
    fb.setupNameTable({"familyName": a.name, "styleName": "Regular"})
    fb.setupPost(isFixedPitch=1)
    fb.font.flavor = "woff2"
    fb.save(a.out)
    print(f"{a.out}: {len(keep)} glyphs, {px}px grid, upm {upm}", file=sys.stderr)


if __name__ == "__main__":
    main()
