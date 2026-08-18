#!/usr/bin/env python3
"""anim2h.py - convert an animation into a C header of 1bpp frames.

Output format matches what sketches/Ec11Tft24Test expects:

    static const unsigned char <name>_frames[F][BYTES] PROGMEM = { {..}, .. };

Each frame is a 1-bit-per-pixel mask, MSB-first, rows padded to whole bytes
(byteWidth = (w+7)//8). A set bit = "on" pixel, drawn by Arduino_GFX
drawBitmap(x, y, frame, w, h, fgColor[, bgColor]) in whatever color you pass.

Inputs (read by Pillow):
  - an animated GIF or APNG
  - a directory of numbered PNG/BMP frames (sorted by name)

The source color does not matter - only the shape is stored; the sketch picks
the color. If the source has alpha, alpha is used as the mask (opaque = on);
otherwise luminance vs --threshold is used.

Examples:
  python3 tools/anim2h.py checkmark.gif --name checkmark_ok_64_64_28f --size 64
  python3 tools/anim2h.py frames_dir/ --size 64 --frames 28 --threshold 128
"""
import argparse
import os
import sys
from PIL import Image, ImageSequence


def load_frames(path):
    """Return a list of RGBA PIL images, coalesced (partial GIF frames fixed)."""
    if os.path.isdir(path):
        files = sorted(
            f for f in os.listdir(path)
            if f.lower().endswith((".png", ".bmp", ".gif", ".jpg", ".jpeg"))
        )
        if not files:
            sys.exit(f"no image files in directory {path}")
        return [Image.open(os.path.join(path, f)).convert("RGBA") for f in files]

    im = Image.open(path)
    frames = []
    canvas = None
    for frame in ImageSequence.Iterator(im):
        rgba = frame.convert("RGBA")
        if canvas is None:
            canvas = rgba.copy()
        else:
            canvas = canvas.copy()
            canvas.alpha_composite(rgba)  # honor disposal for partial frames
        frames.append(canvas.copy())
    return frames


def subsample(frames, n):
    total = len(frames)
    if n <= 0 or n >= total:
        return frames
    return [frames[round(i * (total - 1) / (n - 1))] for i in range(n)]


def frame_to_1bpp(img, w, h, threshold, use_alpha, invert):
    img = img.resize((w, h), Image.LANCZOS)
    if use_alpha:
        chan = img.getchannel("A")
    else:
        chan = img.convert("L")
    px = chan.load()
    byte_w = (w + 7) // 8
    out = bytearray(byte_w * h)
    for y in range(h):
        for x in range(w):
            on = px[x, y] >= threshold
            if invert:
                on = not on
            if on:
                out[y * byte_w + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out)


def frame_to_rgb565(img, w, h):
    img = img.convert("RGB").resize((w, h), Image.LANCZOS)
    px = img.load()
    out = []
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            out.append(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
    return out


def emit_header_rgb565(name, w, h, frames_px, out_path):
    n = len(frames_px)
    npx = w * h
    guard = name.upper() + "_H"
    lines = [
        f"#ifndef {guard}", f"#define {guard}", "",
        f"// Animation: {name} (RGB565)",
        f"// Size: {w}x{h}, Frames: {n}, {npx} px/frame -> draw16bitRGBBitmap",
        "", "#include <Arduino.h>", "",
        f"static const uint16_t {name}_frames[{n}][{npx}] PROGMEM = {{",
    ]
    for fr in frames_px:
        lines.append("  {" + ",".join(f"0x{p:04x}" for p in fr) + "},")
    lines += [
        "};", "",
        f"#define {name.upper()}_W {w}",
        f"#define {name.upper()}_H {h}",
        f"#define {name.upper()}_FRAMES {n}",
        "", f"#endif // {guard}",
    ]
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")


def has_alpha(frames, threshold):
    """True if the alpha channel actually carries the shape (not fully opaque)."""
    a = frames[len(frames) // 2].getchannel("A")
    lo, hi = a.getextrema()
    return lo < threshold <= hi  # some transparent and some opaque pixels


def emit_header(name, w, h, frames_bytes, out_path):
    byte_w = (w + 7) // 8
    n = len(frames_bytes)
    nbytes = byte_w * h
    guard = name.upper() + "_H"
    lines = []
    lines.append(f"#ifndef {guard}")
    lines.append(f"#define {guard}")
    lines.append("")
    lines.append(f"// Animation: {name}")
    lines.append(f"// Size: {w}x{h}, Frames: {n}, {nbytes} bytes/frame (1bpp, MSB-first)")
    lines.append("")
    lines.append("#include <Arduino.h>")
    lines.append("")
    lines.append(f"static const unsigned char {name}_frames[{n}][{nbytes}] PROGMEM = {{")
    for fr in frames_bytes:
        body = ",".join(f"0x{b:02x}" for b in fr)
        lines.append("  {" + body + "},")
    lines.append("};")
    lines.append("")
    lines.append(f"#define {name.upper()}_W {w}")
    lines.append(f"#define {name.upper()}_H {h}")
    lines.append(f"#define {name.upper()}_FRAMES {n}")
    lines.append("")
    lines.append(f"#endif // {guard}")
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description="Animation -> 1bpp C header")
    ap.add_argument("input", help="animated GIF/APNG, or a directory of frames")
    ap.add_argument("--name", help="C identifier base (default: from --out or input)")
    ap.add_argument("--size", default="64", help="WxH or single N (default 64)")
    ap.add_argument("--frames", type=int, default=0, help="subsample to N frames (0=all)")
    ap.add_argument("--threshold", type=int, default=128, help="on/off cutoff 0-255")
    ap.add_argument("--alpha", choices=["auto", "on", "off"], default="auto",
                    help="use alpha as mask (default auto-detect)")
    ap.add_argument("--invert", action="store_true", help="invert on/off")
    ap.add_argument("--format", choices=["1bpp", "rgb565"], default="1bpp",
                    help="1bpp mask (drawBitmap) or rgb565 color (draw16bitRGBBitmap)")
    ap.add_argument("--out", help="output .h path (default: <name>.h next to input)")
    args = ap.parse_args()

    if "x" in args.size.lower():
        w, h = (int(v) for v in args.size.lower().split("x"))
    else:
        w = h = int(args.size)

    name = args.name
    if not name and args.out:
        name = os.path.splitext(os.path.basename(args.out))[0]
    if not name:
        base = os.path.basename(args.input.rstrip("/"))
        name = os.path.splitext(base)[0]
    name = "".join(c if c.isalnum() else "_" for c in name)

    out_path = args.out or os.path.join(
        os.path.dirname(os.path.abspath(args.input)), name + ".h")

    frames = load_frames(args.input)
    frames = subsample(frames, args.frames)

    if args.format == "rgb565":
        packed = [frame_to_rgb565(f, w, h) for f in frames]
        nonzero = sum(1 for fr in packed for p in fr if p)
        if nonzero == 0:
            sys.exit("ERROR: all frames are black (0 nonzero pixels).")
        emit_header_rgb565(name, w, h, packed, out_path)
        print(f"wrote {out_path}")
        print(f"  {name}_frames[{len(packed)}][{w * h}] uint16  {w}x{h}  "
              f"rgb565  {w * h * 2 * len(packed) // 1024}KB flash")
        return

    if args.alpha == "on":
        use_alpha = True
    elif args.alpha == "off":
        use_alpha = False
    else:
        use_alpha = has_alpha(frames, args.threshold)

    packed = [frame_to_1bpp(f, w, h, args.threshold, use_alpha, args.invert)
              for f in frames]

    set_bits = sum(bin(b).count("1") for fr in packed for b in fr)
    if set_bits == 0:
        sys.exit("ERROR: all frames are empty (0 set bits). Check --threshold / "
                 "--alpha / --invert, or the source has no visible shape.")

    emit_header(name, w, h, packed, out_path)
    print(f"wrote {out_path}")
    print(f"  {name}_frames[{len(packed)}][{(w + 7) // 8 * h}]  {w}x{h}  "
          f"alpha={'yes' if use_alpha else 'no'}  set_bits={set_bits}")


if __name__ == "__main__":
    main()
