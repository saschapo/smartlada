# /// script
# dependencies = ["pillow"]
# ///
"""export_images.py - build site/public/img/ from the source photos, renders and screen captures.

  uv run site/tools/export_images.py

Photos: EXIF orientation applied, then all metadata dropped (webp written without exif/xmp).
Screens: re-rendered from site/screens/frames.json at x1 in the page palette; the page scales
them with image-rendering: pixelated.
"""
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "custom_pcb/smartlada_revC/product photos v0/selected"
OUT = ROOT / "site/public/img"
SCREEN_FG, SCREEN_BG = "f2f2f2", "000000"

# name -> (source, widths); the largest width is capped at the source width
PHOTOS = {
    "hero": (SRC / "R0015169_1_16x9.jpg", [1080, 1600, 2240]),
    "idea": (SRC / "R0015175_cleanup.png", [896]),
    "light": (SRC / "R0015170_1.jpg", [800, 1400]),
    "tft": (SRC / "R0015184.JPG", [800, 1400]),
    "board": (SRC / "R0015176_1.jpg", [1080, 1800]),
    "render-top": (ROOT / "custom_pcb/smartlada_revC/img/smartlada_revC_top.webp", [1080, 2000]),
    "render-bottom": (ROOT / "custom_pcb/smartlada_revC/img/smartlada_revC_bottom.webp", [1080, 2000]),
}
PORTRAIT = Path.home() / "Documents/Documents/cineink_web/img/saschapo_portrait_bw.jpg"
SCREENS = ["splash", "idle", "brightness", "lamp_setup_00", "settings_00", "display_00", "zigbee", "wifi"]
ANIMS = ["menu", "mode"]


def load(p):
    return ImageOps.exif_transpose(Image.open(p)).convert("RGB")


def photos():
    sizes = {}
    for name, (src, widths) in PHOTOS.items():
        im = load(src)
        for w in widths:
            w = min(w, im.width)
            h = round(im.height * w / im.width)
            im.resize((w, h), Image.LANCZOS).save(OUT / f"{name}-{w}.webp", quality=80, method=6)
        sizes[name] = (im.width, im.height, [min(w, im.width) for w in widths])
    return sizes


def alice(shot):
    # iPhone screenshot: drop the status bar and the sheet handle, keep the device card
    im = Image.open(shot).convert("RGB")
    im = im.crop((0, 262, im.width, im.height))
    w = 720
    im.resize((w, round(im.height * w / im.width)), Image.LANCZOS).save(OUT / "alice.webp", quality=82, method=6)
    return im.size


def og():
    im = load(PHOTOS["hero"][0])
    im = ImageOps.fit(im, (1200, 630), Image.LANCZOS, centering=(0.62, 0.5))
    im.save(OUT / "og.jpg", quality=84, optimize=True, progressive=True)


def portrait():
    im = Image.open(PORTRAIT).convert("L")
    im = ImageOps.fit(im, (280, 350), Image.LANCZOS)
    im.save(OUT / "portrait.webp", quality=80)


def screens():
    tmp = Path(tempfile.mkdtemp())
    shutil.copy(ROOT / "site/screens/frames.json", tmp)
    subprocess.run(["uv", "run", str(ROOT / "tools/screens.py"), "render", "--out", str(tmp), "--scale", "1",
                    "--fg", SCREEN_FG, "--bg", SCREEN_BG], check=True)
    dst = OUT / "screens"
    dst.mkdir(exist_ok=True)
    for n in SCREENS:
        shutil.copy(tmp / "png/1x" / f"{n}.png", dst / f"{n}.png")
    for n in ANIMS:
        shutil.copy(tmp / "anim" / f"{n}.webp", dst / f"{n}.webp")


def icons():
    # The tail light in 16 px: amber turn signal, white reverse, red brake + marker
    rects = [(1, 4, 4, 8, "#f09628"), (5, 4, 5, 4, "#f5ebd7"), (5, 8, 5, 4, "#cd1e19"), (10, 4, 5, 8, "#cd1e19")]
    (OUT.parent / "favicon.svg").write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" shape-rendering="crispEdges">'
        '<rect width="16" height="16" fill="#000"/>'
        + "".join(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{c}"/>' for x, y, w, h, c in rects)
        + "</svg>\n")
    g = Image.new("RGB", (16, 16), "#000")
    for x, y, w, h, c in rects:
        g.paste(c, (x, y, x + w, y + h))
    for s, name in ((180, "apple-touch-icon.png"), (192, "icon-192.png"), (512, "icon-512.png")):
        g.resize((s, s), Image.NEAREST).save(OUT.parent / name)
    g.resize((48, 48), Image.NEAREST).save(OUT.parent / "favicon.ico", sizes=[(16, 16), (32, 32), (48, 48)])


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sizes = photos()
    print("alice", alice(ROOT / "site/source/alice-app.jpg"))
    og()
    portrait()
    screens()
    icons()
    print(json.dumps(sizes, indent=1))


if __name__ == "__main__":
    main()
