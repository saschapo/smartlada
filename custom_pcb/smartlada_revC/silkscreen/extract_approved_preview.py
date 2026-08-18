"""Turn the approved visual composition into a KiCad-importable bitmap.

The source is the latest approved back-side preview.  It contains white silk
over a green PCB; yellow pads and black holes are deliberately excluded.
"""

from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(
    "/Users/saschapo/.codex/generated_images/01a00761-ac5b-7932-9514-0f7518e1b9e4/"
    "exec-78e6a8fb-370e-4aed-a056-d97b69ea22de.png"
)
OUTPUT = ROOT / "silkscreen/2106_lamp_schematic_back_silkscreen_approved_v2_100x59mm.png"

# Inside edge of the rendered 100 x 59 mm board, in source-preview pixels.
# Include a thin outside margin: the far-right callout deliberately reaches
# close to the board edge, and must not be clipped during extraction.
BOARD_BOX = (7, 10, 1620, 954)
TARGET_SIZE = (2000, 1180)  # 20 px/mm; 508 DPI makes this 100 x 59 mm.


def is_white_silkscreen(r: int, g: int, b: int) -> bool:
    """Select white drawing/text, rejecting green substrate and yellow pads."""
    return r > 185 and g > 185 and b > 175 and max(r, g, b) - min(r, g, b) < 58


def main() -> None:
    preview = Image.open(SOURCE).convert("RGB").crop(BOARD_BOX)
    preview = preview.resize(TARGET_SIZE, Image.Resampling.LANCZOS)
    pixels = preview.load()
    output = Image.new("L", TARGET_SIZE, 255)  # white background
    out = output.load()
    for y in range(TARGET_SIZE[1]):
        for x in range(TARGET_SIZE[0]):
            out[x, y] = 0 if is_white_silkscreen(*pixels[x, y]) else 255
    output.save(OUTPUT, optimize=True, dpi=(508, 508))
    print(f"Wrote {OUTPUT}")


if __name__ == "__main__":
    main()
