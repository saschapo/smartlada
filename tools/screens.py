#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial", "pillow"]
# ///
"""screens.py - capture the SmartLadaRevC menu straight from the firmware's frame buffer.

The board must run a SCREEN_DUMP build (either display variant):

  FQBN="esp32:esp32:esp32c6:ZigbeeMode=ed,PartitionScheme=zigbee_8MB,CDCOnBoot=cdc,FlashSize=16M"
  arduino-cli compile -b "$FQBN" --build-property "compiler.cpp.extra_flags=-DSCREEN_DUMP=1" SmartLadaRevC
  (add -DUI_TFT=1 for the TFT + encoder bench variant; flash a normal build afterwards)

  uv run tools/screens.py capture   # default walk through the menu -> site/screens/
  uv run tools/screens.py shell     # drive it by hand, save screens by name
  uv run tools/screens.py render --scale 6 --fg f2f2f2   # re-render saved frames, no board

The firmware prints every changed frame as "SCR <millis> <2048 hex>" (raw 128x64 page buffer)
and takes single-char keys: U/D/S/B = UP/DOWN/SEL/BACK. Opening the port resets the board, so a
capture always starts from a fresh boot: splash, then idle, every menu cursor at 0.

Output (--out, default site/screens/):
  frames.json          raw frames: {"variant", "shots": {name: hex}, "anims": {name: [[ms, hex]...]}}
  png/<name>.png       shots at --scale (nearest-neighbour), png/1x/<name>.png at native size
  anim/<name>.webp     animated transitions (carousel scrolls), same scale
  contact.png          every shot on one sheet, for picking
"""

import argparse
import glob
import json
import re
import sys
import threading
import time
from pathlib import Path

W, H = 128, 64
FRAME_BYTES = W * H // 8
ROOT = Path(__file__).resolve().parents[1]

# Semantic steps -> firmware keys. Under UI_TFT the menu swaps list navigation (so the encoder's
# CW both raises values and steps lists forward), so "next" is UP there and DOWN on the OLED build.
KEYS = {
    "oled": {"next": "D", "prev": "U", "ok": "S", "back": "B", "inc": "U", "dec": "D"},
    "tft":  {"next": "U", "prev": "D", "ok": "S", "back": "B", "inc": "U", "dec": "D"},
}


class Stop(Exception):
    pass


# ---------------------------------------------------------------- serial link

class Link:
    """Background reader: collects SCR frames with arrival times, and the hello line."""

    def __init__(self, port, verbose=False):
        import serial
        s = serial.Serial()
        s.port, s.baudrate, s.timeout = port, 115200, 0.1
        s.dtr = s.rts = False          # the open resets the board anyway; do not add a second one
        s.open()
        self.s, self.verbose = s, verbose
        self.frames = []               # [(host time, bytes, device ms)]
        self.variant = None
        self.lock = threading.Lock()
        threading.Thread(target=self._rx, daemon=True).start()

    def _rx(self):
        buf = b""
        while True:
            try:
                buf += self.s.read(4096)
            except Exception:          # port hiccup during the reset: keep trying
                time.sleep(0.2)
                continue
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                self._line(line.strip())

    # Matched anywhere in a line: a frame cut short by the USB link must not swallow what follows.
    RE_FRAME = re.compile(rb"SCR (\d+) ([0-9a-f]{%d})(?![0-9a-f])" % (2 * FRAME_BYTES))
    RE_HELLO = re.compile(rb"SCRDUMP ui=(\w+)")

    def _line(self, line):
        m = self.RE_FRAME.search(line)
        if m:
            with self.lock:
                self.frames.append((time.monotonic(), bytes.fromhex(m.group(2).decode()),
                                    int(m.group(1))))
        h = self.RE_HELLO.search(line)
        if h:
            self.variant = h.group(1).decode()
        if not m and not h and self.verbose and line:
            print("  fw:", line[:160].decode("utf-8", "replace"))

    def send(self, keys):
        self.s.write(keys.encode())

    def count(self):
        with self.lock:
            return len(self.frames)

    def last(self):
        with self.lock:
            return self.frames[-1] if self.frames else None

    def since(self, i):
        with self.lock:
            return self.frames[i:]

    def settle(self, start, quiet=0.45, timeout=4.0, max_total=8.0):
        """Wait for frames after index `start` to stop coming; return them ([] = no reaction)."""
        t0 = time.monotonic()
        while True:
            now = time.monotonic()
            n = self.count()
            if n > start:
                if now - self.last()[0] >= quiet or now - t0 >= max_total:
                    return self.since(start)
            elif now - t0 >= timeout:
                return []
            time.sleep(0.02)


def find_port(port):
    if port:
        return port
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("no /dev/cu.usbmodem* port -- pass --port")
    return ports[0]


# ---------------------------------------------------------------- capture session

class Session:
    def __init__(self, link):
        self.link = link
        self.shots = {}                # name -> bytes
        self.anims = {}                # name -> [(ms, bytes)]
        self.rec = None

    def boot(self):
        """After the port-open reset: splash is the first frame, idle follows the boot ramp."""
        print("waiting for the board to boot ...")
        fr = self.link.settle(0, quiet=1.5, timeout=15.0, max_total=12.0)
        self.link.send("+")            # hello: learn the variant (re-sends the current frame)
        t0 = time.monotonic()
        while self.link.variant is None and time.monotonic() - t0 < 5:
            time.sleep(0.05)
        if self.link.variant not in KEYS:
            raise Stop("no hello from the firmware -- is a SCREEN_DUMP build flashed?")
        if not fr:
            raise Stop("no boot frames: the board did not reset on port open, so the menu "
                       "cursors are unknown. Press RESET and run again.")
        print(f"variant: {self.link.variant}")
        lit = [f[1] for f in fr if any(f[1])]         # blank frames come before the splash is drawn
        if lit:
            self.shots["splash"] = lit[0]
        self.link.settle(self.link.count() - 1, quiet=0.5, timeout=1.0)
        self.shot("idle")

    def shot(self, name):
        self.shots[name] = self.link.last()[1]
        print(f"  shot {name}")

    def press(self, step, name=None):
        key = KEYS[self.link.variant][step] if step in KEYS["oled"] else step
        start = self.link.count()
        self.link.send(key)
        fr = self.link.settle(start)
        if not fr:
            # A press the menu swallowed (e.g. waking a blanked panel) would shift every later
            # key onto the wrong screen, where 'ok' can toggle the radio or confirm a reset.
            raise Stop(f"no screen change after '{step}' -- stopping before keys go astray")
        if self.rec is not None:
            self.rec.extend(fr)
        if name:
            self.shot(name)

    def record(self):
        last = self.link.last()
        self.rec = [last] if last else []

    def stop_record(self, name):
        rec, self.rec = self.rec, None
        if len(rec) > 1:
            t0 = rec[0][2]                                  # device clock: real frame timing
            self.anims[name] = [(ms - t0, d) for _, d, ms in rec]
            print(f"  anim {name} ({len(rec)} frames)")

    def cycle(self, prefix, limit=24):
        """Step 'next' through a wrapping list until it is back where it started."""
        first = self.link.last()[1]
        self.shot(f"{prefix}_00")
        self.record()
        for i in range(1, limit):
            self.press("next")
            if self.link.last()[1] == first:
                break
            self.shot(f"{prefix}_{i:02d}")
        else:
            print(f"  warning: {prefix} did not wrap within {limit} steps")
        self.stop_record(prefix)

    def to_json(self):
        return {
            "variant": self.link.variant,
            "shots": {k: v.hex() for k, v in self.shots.items()},
            "anims": {k: [[ms, d.hex()] for ms, d in v] for k, v in self.anims.items()},
        }


def force_out(s):
    """Settings -> Force Out -> Yes, so idle shows the real main screen on USB power (without it,
    no PD 12 V = "NO 12V"). Force is RAM-only, so it has to happen after the port-open reset.
    It drives the lamp FETs from the USB 5 V: lamps must be disconnected. Leaves every cursor
    back at 0 so the default walk runs unchanged."""
    s.shot("idle_no12v")
    s.press("ok")
    for _ in range(4):
        s.press("next")                                     # menu -> Settings
    s.press("ok")
    for _ in range(3):
        s.press("next")                                     # settings -> Force Out
    s.press("ok", "force_confirm")                          # confirm screen, still on No
    s.press("next"); s.press("ok")                          # Yes -> back in Settings, "on"
    s.press("next"); s.press("next")                        # settings cursor 3 -> 0
    s.press("back"); s.press("next")                        # menu cursor 4 -> 0
    s.press("back", "idle")


def default_walk(s, forced=False):
    """Visit every screen that is safe to open. It only enters and leaves: 'ok' is never pressed
    inside Zigbee / WiFi (toggles the radio and reboots), Force Out is skipped, and confirm
    screens (BLE update, factory reset) are left with 'back' while they still say No."""
    s.boot()
    if forced:
        force_out(s)
    s.press("ok"); s.cycle("menu")                           # carousel Mode .. Settings, animated
    s.press("ok"); s.cycle("mode"); s.press("back")          # effect picker; back = no change
    s.press("next"); s.press("ok", "brightness"); s.press("back")
    s.press("next"); s.press("ok", "timings"); s.press("back")
    s.press("next"); s.press("ok"); s.cycle("lamp_setup"); s.press("back")
    s.press("next"); s.press("ok"); s.cycle("settings")      # ends back on Display
    s.press("ok"); s.cycle("display"); s.press("back")
    s.press("next"); s.press("ok", "zigbee"); s.press("back")
    s.press("next"); s.press("ok", "wifi"); s.press("back")
    s.press("next"); s.press("next")                         # skip Force Out
    s.press("ok"); s.cycle("system")                         # ends back on Statistics
    s.press("ok", "statistics"); s.press("back")
    s.press("next"); s.press("ok", "ble_confirm"); s.press("back")
    s.press("next"); s.press("ok", "factory_confirm"); s.press("back")
    s.press("back"); s.press("back"); s.press("back", "idle_end")      # -> Settings -> Menu -> idle


def shell(s):
    print("keys: n/p = next/prev, o = ok, b = back, +/- = inc/dec (values!), "
          "'s NAME' = save shot, 'r' = record, 'r NAME' = stop recording, 'q' = save and quit")
    steps = {"n": "next", "p": "prev", "o": "ok", "b": "back", "+": "inc", "-": "dec"}
    while True:
        try:
            cmd = input("> ").strip()
        except EOFError:
            break
        if cmd == "q":
            break
        try:
            if cmd in steps:
                s.press(steps[cmd])
            elif cmd.startswith("s "):
                s.shot(cmd[2:].strip())
            elif cmd == "r":
                s.record(); print("  recording")
            elif cmd.startswith("r "):
                s.stop_record(cmd[2:].strip())
            elif cmd:
                print("  ?")
        except Stop as e:
            print("  " + str(e))


# ---------------------------------------------------------------- rendering

def hexcolor(s):
    s = s.lstrip("#")
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def to_image(data, fg, bg, scale=1):
    from PIL import Image
    img = Image.new("RGB", (W, H), bg)
    px = img.load()
    for y in range(H):
        row, bit = (y >> 3) * W, 1 << (y & 7)
        for x in range(W):
            if data[row + x] & bit:
                px[x, y] = fg
    return img.resize((W * scale, H * scale), Image.NEAREST) if scale > 1 else img


def render(out, scale, fg, bg):
    from PIL import Image, ImageDraw
    doc = json.loads((out / "frames.json").read_text())
    shots = {k: bytes.fromhex(v) for k, v in doc["shots"].items()}
    anims = {k: [(ms, bytes.fromhex(d)) for ms, d in v] for k, v in doc.get("anims", {}).items()}
    for d in ("png", "png/1x", "anim"):
        (out / d).mkdir(parents=True, exist_ok=True)
    for name, data in shots.items():
        to_image(data, fg, bg, scale).save(out / "png" / f"{name}.png")
        to_image(data, fg, bg).save(out / "png" / "1x" / f"{name}.png")
    for name, frames in anims.items():
        # Browsers stretch very short frame delays, and the carousel redraws every ~12 ms: keep a
        # frame only once >= 20 ms have passed since the last kept one, so real speed survives.
        kept = [frames[0]]
        for f in frames[1:-1]:
            if f[0] - kept[-1][0] >= 20:
                kept.append(f)
        if len(frames) > 1:
            kept.append(frames[-1])
        imgs = [to_image(d, fg, bg, scale) for _, d in kept]
        durs = [max(20, kept[i + 1][0] - kept[i][0]) for i in range(len(kept) - 1)] + [1200]
        imgs[0].save(out / "anim" / f"{name}.webp", save_all=True, append_images=imgs[1:],
                     duration=durs, loop=0, lossless=True)
    # contact sheet at x2 with names
    cols, cw, ch, pad = 4, W * 2, H * 2, 18
    rows = (len(shots) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (cw + 8) + 8, rows * (ch + pad + 8) + 8), (40, 40, 44))
    draw = ImageDraw.Draw(sheet)
    for i, (name, data) in enumerate(shots.items()):
        x, y = 8 + (i % cols) * (cw + 8), 8 + (i // cols) * (ch + pad + 8)
        sheet.paste(to_image(data, fg, bg, 2), (x, y))
        draw.text((x, y + ch + 3), name, fill=(200, 200, 205))
    sheet.save(out / "contact.png")
    print(f"rendered {len(shots)} shots, {len(anims)} animations -> {out}")


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("cmd", choices=["capture", "shell", "render"])
    ap.add_argument("--port")
    ap.add_argument("--out", default=str(ROOT / "site" / "screens"))
    ap.add_argument("--scale", type=int, default=4)
    ap.add_argument("--fg", default="ffffff")
    ap.add_argument("--bg", default="000000")
    ap.add_argument("--force-out", action="store_true",
                    help="capture: enable Force Out first, so idle shows the main screen on USB "
                         "power. Drives the lamp FETs from USB 5 V: disconnect the lamps")
    ap.add_argument("-v", "--verbose", action="store_true", help="echo the firmware's own log lines")
    a = ap.parse_args()
    out = Path(a.out)
    fg, bg = hexcolor(a.fg), hexcolor(a.bg)

    if a.cmd == "render":
        render(out, a.scale, fg, bg)
        return

    s = Session(Link(find_port(a.port), a.verbose))
    try:
        if a.cmd == "capture":
            default_walk(s, a.force_out)
        else:
            s.boot()
            shell(s)
    except Stop as e:
        print("stopped:", e)
    except KeyboardInterrupt:
        print("interrupted")
    if s.shots:
        out.mkdir(parents=True, exist_ok=True)
        (out / "frames.json").write_text(json.dumps(s.to_json(), indent=1))
        render(out, a.scale, fg, bg)


if __name__ == "__main__":
    main()
