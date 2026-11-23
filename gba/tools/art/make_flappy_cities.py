"""The far-away skylines of Jingle Flap's world tour, and the candy cane pickup.

  flappy_city_stl   St. Louis: the Gateway Arch, the old courthouse dome, downtown
  flappy_city_gnv   Gainesville: a brick clock tower, palm trees with lights, mossy oaks
  flappy_city_sea   Seattle: Mount Rainier, the Space Needle, towers
  flappy_city_sf    Bay Area: the Golden Gate Bridge, hills, fog, the Palace of Fine Arts
  flappy_city_mtn   Moorestown, New Jersey: colonial brick Main Street, a white church, the town tree
  flappy_candy      16x16 candy cane (bonus pickup)

Each skyline is 512x256 and wraps. The screen shows rows 48..207; the near rooftops cover rows 182..207, so
everything stands on row 182 and can go up to about row 100. The landmark appears twice (x and x+256) so it's
on screen soon after a city starts. Run:  python3 tools/art/make_flappy_cities.py
"""
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(__file__))
from canvas import Canvas, rgb, write_json, save_png  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
OUT = os.path.join(ROOT, "graphics", "flappy")
BG_JSON = '{\n    "type": "regular_bg",\n    "bpp_mode": "bpp_4_auto"\n}'
W = 512
GROUND = 182          # the near rooftops cover everything below this row

OUT_C = rgb("#141430")
WALL, WALL_S = rgb("#34346c"), rgb("#28285a")
WIN, WIN_W = rgb("#f8d870"), rgb("#e09848")
SNOW, SNOW_S = rgb("#c8d0f0"), rgb("#8890c8")


class Sky:
    """A wrapping canvas with helpers."""

    def __init__(self):
        self.c = Canvas(W, 256)

    def put(self, x, y, col):
        self.c.set(int(x) % W, int(y), col)

    def get(self, x, y):
        return self.c.get(int(x) % W, int(y))

    def rect(self, x, y, w, h, col):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.put(xx, yy, col)

    def building(self, x, w, h, rnd, wall=WALL, wall_s=WALL_S, snow_top=True):
        top = GROUND - h
        self.rect(x, top, w, h + 26, wall)
        self.rect(x + w - 3, top, 3, h + 26, wall_s)
        if snow_top:
            self.rect(x, top, w, 2, SNOW)
        for wy in range(top + 5, GROUND - 2, 6):
            for wx in range(x + 3, x + w - 4, 5):
                if rnd.random() < 0.6:
                    self.rect(wx, wy, 2, 3, WIN if rnd.random() < 0.75 else WIN_W)

    def outline(self):
        marks = []
        for y in range(256):
            for x in range(W):
                if self.c.get(x, y) is None and any(self.get(x + dx, y + dy) not in (None, OUT_C)
                                                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    marks.append((x, y))
        for x, y in marks:
            self.c.set(x, y, OUT_C)

    def save(self, name, preview=None):
        cols = len(self.c.colors())
        assert cols <= 15, f"{name}: {cols} colours (max 15)"
        self.c.save_bmp(os.path.join(OUT, name + ".bmp"), bpp=4)
        write_json(os.path.join(OUT, name + ".json"), BG_JSON)
        if preview:
            save_png(os.path.join(preview, name + ".png"), self.c, scale=2)


def skyline_filler(s, rnd, skip, heights=(16, 40), widths=(14, 26)):
    """Plain buildings everywhere except the ranges in `skip` (where landmarks stand)."""
    x = 0
    while x < W:
        w = rnd.randint(*widths)
        if not any(a - 4 <= x + w and x <= b + 4 for a, b in skip):
            s.building(x, w, rnd.randint(*heights), rnd)
        x += w + rnd.choice([0, 2, 3])


# ----------------------------------------------------------------------------- St. Louis
def stl(preview):
    s = Sky()
    rnd = random.Random(1)
    steel, steel_l, steel_d = rgb("#b8c0d8"), rgb("#e8ecf8"), rgb("#7880a0")
    dome, dome_d = rgb("#4c9a78"), rgb("#2e6c54")
    skyline_filler(s, random.Random(2), [(34, 150), (290, 406)])
    for base in (40, 296):
        # Old courthouse: a building with a green dome, in front of the arch.
        cx = base + 86
        s.building(cx - 14, 28, 20, rnd, snow_top=False)
        for i in range(12):
            half = int(math.sqrt(max(0, 144 - (i - 12) ** 2)) * 0.8)
            for x in range(cx - half, cx + half + 1):
                s.put(x, GROUND - 20 - 12 + i, dome if x < cx + half // 2 else dome_d)
        s.rect(cx - 1, GROUND - 20 - 16, 2, 4, dome_d)
        s.rect(cx - 16, GROUND - 21, 32, 2, SNOW)
        # The Gateway Arch: a weighted catenary as wide as it is tall. Its legs are triangles in cross-section,
        # so we see two steel faces: a bright one and a shaded one. Thick at the ground, much thinner at the top.
        ax, half_w, height = base + 40, 38, 82
        a = 2.4
        span = math.cosh(a) - 1
        for i in range(-half_w * 8, half_w * 8 + 1):
            x = i / 8
            t = x / half_w
            y = GROUND - height * (1 - (math.cosh(a * t) - 1) / span)
            # slope of the curve, for the direction across the leg
            dy = height * a * math.sinh(a * t) / span / half_w
            n = math.hypot(1, dy)
            nx, ny = -dy / n, 1 / n                  # pointing down/outwards from the curve
            up = (GROUND - y) / height               # 0 at the ground, 1 at the top
            thick = 7.5 - 4.5 * up                   # about 7 px at the feet, 3 px at the top
            for k in range(int(thick * 4) + 1):
                d = k / 4 - thick / 2
                px = ax + x + nx * d * (1 if x >= 0 else -1)
                py = y + ny * d
                outer = d > 0
                if x < 0:
                    col = steel_l if outer else steel
                else:
                    col = steel if outer else steel_d
                if abs(d) > thick / 2 - 0.6:
                    col = steel_d if x >= 0 else steel
                s.put(round(px), round(py), col)
    s.outline()
    s.save("flappy_city_stl", preview)


# ----------------------------------------------------------------------------- Gainesville
def gnv(preview):
    s = Sky()
    rnd = random.Random(3)
    brick, brick_d = rgb("#b05038"), rgb("#7c3428")
    trunk = rgb("#8a6a48")
    leaf, leaf_d = rgb("#3a8a4a"), rgb("#246034")
    moss = rgb("#8a9490")
    bulbs = [rgb("#f05050"), WIN, rgb("#60a0f8")]
    skyline_filler(s, rnd, [(30, 150), (286, 406)], heights=(12, 26))
    for base in (30, 286):
        # Brick clock tower with a pointed roof.
        tx = base + 60
        s.rect(tx - 6, GROUND - 64, 12, 64 + 26, brick)
        s.rect(tx + 3, GROUND - 64, 3, 64 + 26, brick_d)
        for y in range(GROUND - 60, GROUND, 8):
            s.rect(tx - 4, y, 2, 4, WIN_W)
            s.rect(tx + 1, y, 2, 4, WIN_W)
        s.rect(tx - 4, GROUND - 56, 8, 8, SNOW)                # the clock face
        s.put(tx, GROUND - 53, OUT_C); s.put(tx, GROUND - 52, OUT_C); s.put(tx + 1, GROUND - 52, OUT_C)
        for i in range(16):
            half = max(0, 7 - i // 2)
            for x in range(tx - half, tx + half + 1):
                s.put(x, GROUND - 65 - i, brick_d if x > tx else brick)
        # Palm trees with Christmas lights, and big oaks with hanging moss.
        for k, px in enumerate((base + 18, base + 100, base + 116)):
            h = 40 + (k * 7) % 12
            for i in range(h):
                s.put(px + int(3 * math.sin(i / 14)), GROUND - i, trunk)
                s.put(px + 1 + int(3 * math.sin(i / 14)), GROUND - i, trunk)
                if i % 5 == 2:
                    s.put(px + 2 + int(3 * math.sin(i / 14)), GROUND - i, bulbs[(i // 5) % 3])
            topx, topy = px + int(3 * math.sin(h / 14)), GROUND - h
            for ang in range(0, 360, 40):
                for r in range(1, 12):
                    xx = topx + r * math.cos(math.radians(ang))
                    yy = topy + r * 0.6 * math.sin(math.radians(ang)) + (r * r) / 30
                    s.put(xx, yy, leaf if r < 8 else leaf_d)
        for ox in (base - 10, base + 140):
            for y in range(GROUND - 30, GROUND):
                for x in range(ox - 16, ox + 17):
                    dx, dy = (x - ox) / 16, (y - (GROUND - 22)) / 10
                    if dx * dx + dy * dy <= 1 and rnd.random() < 0.9:
                        s.put(x, y, leaf_d if dy > 0.2 else leaf)
            for x in range(ox - 12, ox + 13, 4):
                for y in range(GROUND - 16, GROUND - 16 + rnd.randint(4, 9)):
                    s.put(x, y, moss)
            s.rect(ox - 1, GROUND - 12, 3, 12, trunk)
    s.outline()
    s.save("flappy_city_gnv", preview)


# ----------------------------------------------------------------------------- Seattle
def sea(preview):
    s = Sky()
    rnd = random.Random(4)
    mount, mount_s = rgb("#5a6aa8"), rgb("#46528c")
    needle, needle_l, needle_d = rgb("#c8cce0"), rgb("#f0f0f8"), rgb("#8a90b0")
    halo = rgb("#f8a040")
    # Mount Rainier far behind (one wide snowy mountain per 256 px).
    for base in (0, 256):
        peak_x = base + 180
        for x in range(base, base + 256):
            d = abs(x - peak_x)
            top = int(GROUND - 70 + d * 0.55 + 4 * math.sin(x / 9))
            for y in range(max(top, 0), GROUND):
                col = mount if x < peak_x else mount_s
                if y < GROUND - 70 + 18 + 4 * math.sin(x / 5) and d < 30:
                    col = SNOW if x < peak_x else SNOW_S
                s.put(x, y, col)
    skyline_filler(s, rnd, [(36, 70), (292, 326)], heights=(18, 48), widths=(10, 20))
    for base in (36, 292):
        nx = base + 17
        # Legs: narrow at the waist, wider at the ground and under the saucer.
        for y in range(GROUND - 66, GROUND):
            t = (y - (GROUND - 66)) / 66
            half = int(2 + 5 * abs(t - 0.3) ** 1.3 * 2.2)
            s.put(nx - half, y, needle)
            s.put(nx + half, y, needle_d)
            s.put(nx, y, needle if y % 6 else needle_l)
        # The saucer with its lit band, then the roof and the spire.
        for i in range(8):
            half = 12 - abs(i - 3) * (1 if i < 3 else 2)
            for x in range(nx - half, nx + half + 1):
                s.put(x, GROUND - 74 + i, needle_l if i < 3 else needle if i != 4 else halo)
        for i in range(5):
            for x in range(nx - 6 + i, nx + 7 - i):
                s.put(x, GROUND - 78 + i, needle_d)
        for y in range(GROUND - 92, GROUND - 78):
            s.put(nx, y, needle_l)
        s.put(nx, GROUND - 93, rgb("#f05050"))
    s.outline()
    s.save("flappy_city_sea", preview)


# ----------------------------------------------------------------------------- Bay Area
def sf(preview):
    s = Sky()
    rnd = random.Random(5)
    hill, hill_d = rgb("#2e4a5a"), rgb("#223a48")
    red, red_d = rgb("#e05a30"), rgb("#a03a20")
    fog, fog_s = rgb("#a8b0d0"), rgb("#8890b8")
    ochre, ochre_d = rgb("#d8a070"), rgb("#a87048")
    deck = GROUND - 20
    # Dark hills behind; they rise into a headland at both ends of each bridge, so the deck runs into land.
    for x in range(W):
        local = x % 256
        top = GROUND - 30 + 8 * math.sin(x / 40) + 4 * math.sin(x / 13)
        for end in (8, 214):
            d = abs(local - end)
            if d < 22:
                top = min(top, deck - 6 - (22 - d) * 0.9)
        for y in range(int(top), GROUND):
            s.put(x, y, hill if math.sin(x / 40) > 0 else hill_d)
    # A bank of fog all the way along, under the decks.
    for x in range(W):
        for y in range(deck + 3, deck + 3 + int(5 + 3 * math.sin(x * 2 * math.pi / 64))):
            s.put(x, y, fog if (x + y) % 5 else fog_s)
    for base in (0, 256):
        # The bridge: deck from headland to headland, two towers, the cables.
        t1, t2 = base + 62, base + 160
        for x in range(base + 8, base + 215):
            s.put(x, deck, red)
            s.put(x, deck + 1, red_d)
        for tx in (t1, t2):
            s.rect(tx - 3, GROUND - 76, 7, 76 + 26, red)
            s.rect(tx + 2, GROUND - 76, 2, 76 + 26, red_d)
            for y in (GROUND - 70, GROUND - 56, GROUND - 42):
                s.rect(tx - 3, y, 7, 2, red_d)
                s.put(tx, y + 3, OUT_C)
        cable_top = GROUND - 74
        for x in range(t1, t2 + 1):
            t = (x - t1) / (t2 - t1)
            y = cable_top + 46 * 4 * t * (1 - t)
            s.put(x, y, red)
            if (x - t1) % 6 == 0:
                for yy in range(int(y) + 1, deck):
                    s.put(x, yy, red_d)
        # side spans: from the tower tops down to the deck at each headland, with a gentle sag
        for (xa, xb) in ((base + 12, t1), (t2, base + 211)):
            for x in range(xa, xb + 1):
                t = (x - xa) / (xb - xa)
                if xa < t1:
                    y = deck - 3 + (cable_top - deck + 3) * t + 6 * t * (1 - t)
                else:
                    y = cable_top + (deck - 3 - cable_top) * t + 6 * t * (1 - t)
                s.put(x, y, red)
                if (x - xa) % 6 == 3:
                    for yy in range(int(y) + 1, deck):
                        s.put(x, yy, red_d)
        # The Palace of Fine Arts, between the bridges: a domed rotunda on columns, with colonnades.
        px = base + 236
        for cx in range(px - 14, px + 15, 4):              # colonnade wings
            s.rect(cx, GROUND - 12, 2, 12, ochre if cx < px else ochre_d)
        s.rect(px - 15, GROUND - 14, 31, 2, ochre)
        for cx in range(px - 7, px + 8, 3):                # the rotunda's columns
            s.rect(cx, GROUND - 26, 2, 14, ochre if cx < px + 2 else ochre_d)
        s.rect(px - 9, GROUND - 28, 19, 3, ochre)
        for i in range(10):                                # the dome
            half = int(math.sqrt(max(0.0, 100 - (10 - i) ** 2)) * 0.9)
            for x in range(px - half, px + half + 1):
                s.put(x, GROUND - 28 - 10 + i, ochre if x < px + half // 3 else ochre_d)
        s.rect(px, GROUND - 41, 1, 3, ochre_d)
    s.outline()
    s.save("flappy_city_sf", preview)


# ----------------------------------------------------------------------------- Moorestown, New Jersey
def mtn(preview):
    s = Sky()
    rnd = random.Random(6)
    brick, brick_d = rgb("#a84838"), rgb("#7a3028")
    white, white_d = rgb("#e8e8f0"), rgb("#a8a8c0")
    tree, tree_d = rgb("#2e7a40"), rgb("#1e5a30")
    bulbs = [rgb("#f05050"), WIN, rgb("#60a0f8")]
    x = 0
    while x < W:
        local = x % 256
        if 92 <= local < 168:                               # leave room for the church and the big tree
            x = (x // 256) * 256 + 168
            continue
        # Colonial brick houses along Main Street: white trim, shutters, a fanlight door, a wreath.
        w = rnd.choice([30, 34, 38])
        h = rnd.choice([22, 26, 30])
        top = GROUND - h
        s.rect(x, top, w, h + 26, brick)
        s.rect(x + w - 3, top, 3, h + 26, brick_d)
        for i in range(8):                                  # roof with snow
            inset = i
            s.rect(x - 1 + inset, top - 1 - i, w + 2 - 2 * inset, 1, SNOW if i > 0 else white_d)
        s.rect(x, top, w, 1, white)
        for wy in range(top + 4, GROUND - 6, 9):
            for wx in range(x + 4, x + w - 6, 9):
                s.rect(wx - 1, wy, 1, 5, OUT_C)              # shutters
                s.rect(wx + 4, wy, 1, 5, OUT_C)
                s.rect(wx, wy, 4, 5, WIN if rnd.random() < 0.8 else WIN_W)
        dx = x + w // 2 - 2
        s.rect(dx, GROUND - 7, 5, 7, white)
        s.rect(dx + 1, GROUND - 6, 3, 6, OUT_C)
        s.put(dx + 2, GROUND - 4, tree)                     # wreath on the door
        x += w + rnd.choice([2, 4])
    for base in (0, 256):
        # The white church with its tall steeple.
        cx = base + 110
        s.rect(cx - 12, GROUND - 26, 24, 26 + 26, white)
        s.rect(cx + 8, GROUND - 26, 4, 26 + 26, white_d)
        for i in range(10):
            s.rect(cx - 13 + i, GROUND - 27 - i, 26 - 2 * i, 1, SNOW if i else white_d)
        s.rect(cx - 4, GROUND - 52, 8, 16, white)
        s.rect(cx + 2, GROUND - 52, 2, 16, white_d)
        s.rect(cx - 2, GROUND - 48, 4, 5, WIN)               # the bell window
        for i in range(16):
            half = max(0, 4 - i // 4)
            s.rect(cx - half, GROUND - 53 - i, 2 * half + 1, 1, white if i % 4 else white_d)
        s.rect(cx, GROUND - 72, 1, 4, WIN)
        for wy in (GROUND - 20, GROUND - 12):
            s.rect(cx - 8, wy, 3, 5, WIN)
            s.rect(cx + 5, wy, 3, 5, WIN)
        # The big town Christmas tree, all lit up.
        tx = base + 148
        for i in range(52):
            half = 1 + i * 13 // 52
            if i % 13 > 10:
                half -= 2
            s.rect(tx - half, GROUND - 56 + i, 2 * half + 1, 1, tree if (i // 3) % 3 else tree_d)
        for i in range(6, 52, 5):
            half = 1 + i * 13 // 52
            for k, lx in enumerate(range(tx - half + 1, tx + half, 3)):
                s.put(lx, GROUND - 56 + i + (k % 2), bulbs[(k + i) % 3])
        s.rect(tx - 1, GROUND - 59, 3, 3, WIN)
        s.rect(tx - 2, GROUND - 4, 5, 4, brick_d)
    s.outline()
    s.save("flappy_city_mtn", preview)


# ----------------------------------------------------------------------------- candy cane
def candy(preview):
    red, white, out = rgb("#e83838"), rgb("#f8f8f8"), rgb("#401828")
    c = Canvas(16, 16)
    # the hook
    for ang in range(0, 181, 6):
        for r in (3, 4):
            x = 8 + r * math.cos(math.radians(180 + ang))
            y = 5 + r * math.sin(math.radians(180 + ang))
            c.set(int(round(x)), int(round(y)), red if (ang // 30) % 2 else white)
    for y in range(5, 15):
        for x in (4, 5):
            c.set(x, y, red if ((y + x) // 2) % 2 else white)
    marks = [(x, y) for y in range(16) for x in range(16) if c.get(x, y) is None and
             any(c.get(x + dx, y + dy) not in (None, out) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))]
    for x, y in marks:
        c.set(x, y, out)
    c.save_bmp(os.path.join(OUT, "flappy_candy.bmp"), bpp=4)
    write_json(os.path.join(OUT, "flappy_candy.json"), '{\n    "type": "sprite",\n    "height": 16\n}')
    if preview:
        save_png(os.path.join(preview, "flappy_candy.png"), c, scale=6)


def main():
    preview = sys.argv[1] if len(sys.argv) > 1 else None
    stl(preview)
    gnv(preview)
    sea(preview)
    sf(preview)
    mtn(preview)
    candy(preview)
    print("Wrote the Jingle Flap cities to", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
