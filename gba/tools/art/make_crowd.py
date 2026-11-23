"""Makes the "crowd" versions of the family characters: the spectators in the Ping Pong hall.

The GBA only has 16 sprite palettes, and every character normally has its own. Eight spectators would use
them all up, so this script copies three frames of every character (stand, side, cheer) and repaints them
with TWO shared 15-colour palettes (characters 1-5 share one, 6-10 the other). Colours that don't fit are
replaced by the closest one, which is hard to notice on small background characters.

Reads graphics/characters/chr_<name>.bmp, writes graphics/crowd/crowd_<name>.bmp (16x96: 3 frames of 16x32).
Run after changing the characters:  python3 tools/art/make_crowd.py
"""
import os
import struct

from canvas import Canvas, write_json

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
SRC = os.path.join(ROOT, "graphics", "characters")
OUT = os.path.join(ROOT, "graphics", "crowd")
NAMES = ["adam", "charly", "billy", "bo", "cherry", "lillian", "cheng", "ella", "aiden", "alyssa"]  # sb_characters order
FRAMES = [0, 6, 11]     # stand (facing you), side (facing right), cheer


def read_bmp(path):
    """Reads an indexed (4 or 8 bpp) BMP into a Canvas (index 0 = transparent)."""
    data = open(path, "rb").read()
    offset = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    colours = struct.unpack_from("<I", data, 46)[0] or (1 << bpp)
    pal_start = 14 + struct.unpack_from("<I", data, 14)[0]
    pal = [(data[pal_start + i * 4 + 2], data[pal_start + i * 4 + 1], data[pal_start + i * 4]) for i in range(colours)]
    stride = ((w * bpp + 7) // 8 + 3) & ~3
    c = Canvas(w, abs(h))
    for row in range(abs(h)):
        y = abs(h) - 1 - row if h > 0 else row
        base = offset + row * stride
        for x in range(w):
            if bpp == 8:
                i = data[base + x]
            else:
                b = data[base + x // 2]
                i = (b >> 4) if x % 2 == 0 else (b & 15)
            c.set(x, y, None if i == 0 else pal[i])
    return c


def distance(a, b):
    return sum((p - q) ** 2 for p, q in zip(a, b))


def shared_palette(canvases, size=15):
    counts = {}
    for c in canvases:
        for p in c.px:
            if p is not None:
                counts[p] = counts.get(p, 0) + 1
    colours = sorted(counts, key=lambda col: -counts[col])
    # merge the closest pair (dropping the rarer colour) until it fits
    while len(colours) > size:
        best = None
        for i in range(len(colours)):
            for j in range(i + 1, len(colours)):
                d = distance(colours[i], colours[j])
                if best is None or d < best[0]:
                    best = (d, i, j)
        _, i, j = best
        keep, drop = (colours[i], colours[j]) if counts[colours[i]] >= counts[colours[j]] else (colours[j], colours[i])
        counts[keep] += counts[drop]
        colours.remove(drop)
    return colours


def main():
    os.makedirs(OUT, exist_ok=True)
    sheets = []
    for name in NAMES:
        src = read_bmp(os.path.join(SRC, f"chr_{name}.bmp"))
        sheet = Canvas(16, 32 * len(FRAMES))
        for k, f in enumerate(FRAMES):
            for y in range(32):
                for x in range(16):
                    sheet.set(x, k * 32 + y, src.get(x, f * 32 + y))
        sheets.append(sheet)
    for group in (range(0, 5), range(5, 10)):
        pal = shared_palette([sheets[i] for i in group])
        for i in group:
            sheet = sheets[i]
            sheet.px = [None if p is None else min(pal, key=lambda q: distance(p, q)) for p in sheet.px]
            sheet.save_bmp(os.path.join(OUT, f"crowd_{NAMES[i]}.bmp"), bpp=4, palette=pal)
            write_json(os.path.join(OUT, f"crowd_{NAMES[i]}.json"), '{\n    "type": "sprite",\n    "height": 32\n}')
    print("crowd: wrote", len(NAMES), "spectators with 2 shared palettes to", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
