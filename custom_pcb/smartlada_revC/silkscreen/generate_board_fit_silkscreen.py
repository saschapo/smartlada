"""Make a board-fit silkscreen layout from the 2106 lamp illustration.

The board is 100 x 59 mm.  Black pixels are intended for KiCad's Image
Converter.  Import the generated footprint onto F.SilkS and flip it onto
B.SilkS; the image is prepared to read correctly when viewed from the back.
The script checks the drawing against the back solder-mask openings after that
KiCad flip.
"""

from __future__ import annotations

import math
import re
from pathlib import Path

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont, ImageOps


ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / "custom_pcb/smartlada_revC/smartlada_revC.kicad_pcb"
SOURCE = Path("/Users/saschapo/Desktop/2106_lamp_schematic.png")
OUT = ROOT / "silkscreen/2106_lamp_schematic_back_silkscreen_import_then_flip_100x59mm.png"
PREVIEW = ROOT / "silkscreen/2106_lamp_schematic_back_silkscreen_preview.png"

PX_PER_MM = 20
W, H = 100 * PX_PER_MM, 59 * PX_PER_MM
X0, Y0 = 130.0, 71.0
CLEARANCE_MM = 0.30
FONT = "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"


def footprint_blocks(board: str):
    pos = 0
    while (pos := board.find("(footprint ", pos)) >= 0:
        depth = 0
        for end in range(pos, len(board)):
            depth += (board[end] == "(") - (board[end] == ")")
            if depth == 0:
                yield board[pos : end + 1]
                pos = end + 1
                break


def pad_clearance_mask() -> Image.Image:
    """Rasterize B.Mask openings, enlarged by the production clearance."""
    mask = Image.new("L", (W, H), 0)
    draw = ImageDraw.Draw(mask)
    board = BOARD.read_text()
    pad_rx = re.compile(
        r'\(pad "([^"]*)"\s+([^\s]+)\s+([^\s]+).*?'
        r'\(at ([\d.-]+) ([\d.-]+)(?: ([\d.-]+))?\).*?'
        r'\(size ([\d.-]+) ([\d.-]+)\).*?\(layers ([^\)]*)\)',
        re.S,
    )
    for block in footprint_blocks(board):
        origin = re.search(r"\(at ([\d.-]+) ([\d.-]+)(?: ([\d.-]+))?\)", block)
        if not origin:
            continue
        ox, oy, rotation = map(float, (origin.group(1), origin.group(2), origin.group(3) or 0))
        angle = math.radians(rotation)
        for match in pad_rx.finditer(block):
            _num, _kind, shape, dx, dy, _pad_rotation, sx, sy, layers = match.groups()
            # Front-only SMD pads do not constrain the back silkscreen.
            if "B.Mask" not in layers and "*.Mask" not in layers:
                continue
            dx, dy, sx, sy = map(float, (dx, dy, sx, sy))
            cx = ox + dx * math.cos(angle) - dy * math.sin(angle)
            cy = oy + dx * math.sin(angle) + dy * math.cos(angle)
            cx = (cx - X0) * PX_PER_MM
            cy = (cy - Y0) * PX_PER_MM
            rx = (sx / 2 + CLEARANCE_MM) * PX_PER_MM
            ry = (sy / 2 + CLEARANCE_MM) * PX_PER_MM
            box = (cx - rx, cy - ry, cx + rx, cy + ry)
            if shape in {"circle", "oval"}:
                draw.ellipse(box, fill=255)
            else:
                draw.rounded_rectangle(box, radius=min(rx, ry) * 0.22, fill=255)
    return mask


def mm(v: float) -> int:
    return round(v * PX_PER_MM)


def font(size_mm: float) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(FONT, mm(size_mm))


def main_art_mask() -> Image.Image:
    """Keep the lamp drawing intact; remove only the original surrounding text."""
    src = Image.open(SOURCE).convert("RGBA")
    white = Image.new("RGBA", src.size, "white")
    white.alpha_composite(src)
    gray = ImageEnhance.Contrast(white.convert("L")).enhance(2.2)
    art = gray.point(lambda p: 255 if p < 205 else 0, "1").convert("L")

    # Drawing region: exploded lamp and its leader lines.  The removed rectangles
    # contain only captions; the lamp geometry itself is not redrawn or altered.
    crop = art.crop((35, 110, 770, 450))
    erase = ImageDraw.Draw(crop)
    erase.rectangle((0, 50, 255, 100), fill=0)       # left upper part number
    erase.rectangle((535, 0, 700, 22), fill=0)       # top part number
    erase.rectangle((0, 280, 275, 340), fill=0)      # left lower part numbers
    erase.rectangle((390, 300, 735, 340), fill=0)    # "ФОНАРИ ЗАДНИЕ"

    # Position only: the artwork is scaled uniformly to 44 mm wide.
    target_w = mm(44.0)
    target_h = round(crop.height * target_w / crop.width)
    crop = crop.resize((target_w, target_h), Image.Resampling.LANCZOS)
    # After reduction, increase the thinnest scan lines to a printable ~0.15 mm.
    crop = crop.filter(ImageFilter.MaxFilter(3))
    return crop


def add_text(layer: Image.Image, xy_mm: tuple[float, float], text: str, size_mm: float = 0.82):
    """Place technical callouts in the available zones around the illustration."""
    ImageDraw.Draw(layer).text((mm(xy_mm[0]), mm(xy_mm[1])), text, font=font(size_mm), fill=255)


def build_artwork() -> Image.Image:
    art = Image.new("L", (W, H), 0)  # white mask means silkscreen ink
    lamp = main_art_mask()
    art.paste(lamp, (mm(25.0), mm(25.3)), lamp)

    # Captions are deliberately kept in open board regions.  They replace the
    # labels removed from the source while leaving the lamp drawing untouched.
    add_text(art, (25.0, 46.5), "2106-3716065")
    add_text(art, (25.0, 47.7), "2106-3716070 / -71")
    add_text(art, (25.0, 48.9), "I0857790")
    add_text(art, (25.0, 50.6), "ФОНАРИ ЗАДНИЕ", 0.92)
    add_text(art, (25.0, 52.1), "I4148190     K240", 0.76)
    add_text(art, (70.2, 25.0), "2106-3716175")
    add_text(art, (70.2, 26.2), "2106-3716175-01", 0.72)
    add_text(art, (70.2, 27.4), "2106-3716018")
    add_text(art, (70.2, 29.2), "2106  21062  21064", 0.70)
    add_text(art, (70.2, 30.3), "21061 21063 21065-01", 0.64)
    add_text(art, (70.2, 31.4), "21066", 0.70)
    # Raise one-pixel scan/text strokes to at least ~0.15 mm before checking.
    return art.filter(ImageFilter.MaxFilter(3))


def main():
    art = build_artwork()
    pads = pad_clearance_mask()
    # KiCad mirrors the footprint when it is flipped from F.SilkS to B.SilkS.
    # Compare in the back-side viewing coordinate system: the artwork remains
    # readable while the physical B.Mask openings are mirrored around board X.
    back_view_pads = ImageOps.mirror(pads)
    collision = ImageChops.multiply(art, back_view_pads)
    collision_pixels = sum(1 for value in collision.getdata() if value)
    if collision_pixels:
        raise RuntimeError(f"Layout intersects enlarged B.Mask openings: {collision_pixels} pixels")

    # KiCad image converter uses black as the filled PCB geometry.
    ImageOps.invert(art).convert("1").convert("L").save(
        OUT, optimize=True, dpi=(508, 508)
    )

    # Human-readable QA preview: white silk over green solder mask, yellow mask openings.
    preview = Image.new("RGB", (W, H), "#2e6529")
    preview.paste("#f5dc00", mask=back_view_pads)
    preview.paste("white", mask=art)
    preview.save(PREVIEW, optimize=True)
    print(f"Wrote {OUT}")
    print(f"Wrote {PREVIEW}")
    print("Collision check: passed (0 pixels inside 0.30 mm expanded B.Mask openings).")


if __name__ == "__main__":
    from PIL import ImageChops

    main()
