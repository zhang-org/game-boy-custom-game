"""Generates the Pony Gallop art in gba/graphics/horse/.

    python3 tools/art/make_horse.py [PREVIEW_DIR]   # PREVIEW_DIR: optional, writes scaled PNG previews

Sprites (4bpp, 15 colours + transparent):
  horse_horse.bmp      64x32 frames: 0-3 gallop, 4 jump (legs tucked), 5 stumble, 6 stand
  horse_rival_pal.bmp  palette swap for the rival's horse (grey horse, blue blanket); same colour order
  horse_obstacles.bmp  16x32 frames: 0 low fence, 1 tall fence, 2 low fence knocked, 3 tall fence knocked,
                       4 hay bale, 5 hay bale knocked, 6 distance flag
  horse_drift.bmp      32x16 frames: 0 snow drift, 1 snow drift kicked flat
  horse_puff.bmp       16x16 frames: 0-2 snow puff (landing dust)
Backgrounds (bpp_4_auto, each image uses <= 15 colours so it needs one palette bank):
  horse_far.bmp   256x256  sky, clouds, distant mountains (scrolls slowest)
  horse_mid.bmp   512x256  snowy hills, pine trees, a red barn and a farmhouse
  horse_near.bmp  256x256  paddock fence, the rival's lane, the main track, the front snow bank

All art is original and drawn with code; tweak the shapes and colours below and re-run.
"""
import math
import os
import sys

from canvas import Canvas, rgb, write_json, save_png

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
OUT = os.path.join(ROOT, "graphics", "horse")

SPRITE_JSON = '{\n    "type": "sprite",\n    "height": %d\n}'
BG_JSON = '{\n    "type": "regular_bg",\n    "bpp_mode": "bpp_4_auto"\n}'
PAL_JSON = '{\n    "type": "sprite_palette",\n    "bpp_mode": "bpp_4"\n}'


# ----------------------------------------------------------------------------- drawing helpers
def disc(c, cx, cy, r, col):
    for y in range(int(cy - r) - 1, int(cy + r) + 2):
        for x in range(int(cx - r) - 1, int(cx + r) + 2):
            if (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= r * r:
                c.set(x, y, col)


def thick_line(c, x0, y0, x1, y1, r, col):
    n = max(1, int(max(abs(x1 - x0), abs(y1 - y0)) * 2))
    for i in range(n + 1):
        t = i / n
        disc(c, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, r, col)


def outline_all(c, col, keep=()):
    """1px outline around every opaque pixel (4-neighbourhood)."""
    marks = []
    for y in range(c.h):
        for x in range(c.w):
            if c.get(x, y) is not None:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if c.get(x + dx, y + dy) is not None:
                    marks.append((x, y))
                    break
    for x, y in marks:
        c.set(x, y, col)


def layer(dst, src):
    dst.blit(src, 0, 0)


def recolor(c, mapping):
    c.px = [mapping.get(p, p) for p in c.px]


# ----------------------------------------------------------------------------- the horse
OUTLINE = rgb("#301820")
COAT = rgb("#b8602c")
COAT_S = rgb("#86401c")
COAT_L = rgb("#e08c4c")
MANE = rgb("#f8e0a8")
MANE_S = rgb("#c8a060")
HOOF = rgb("#403840")
BLANKET = rgb("#d83838")
BLANKET_S = rgb("#982030")
TRIM = rgb("#f8c840")
SADDLE = rgb("#5c3420")
EYE = rgb("#181018")
WHITE = rgb("#f8f8f8")
MUZZLE = rgb("#6c3424")
FAR_LEG = rgb("#6c3018")

HORSE_PALETTE = [OUTLINE, COAT, COAT_S, COAT_L, MANE, MANE_S, HOOF, BLANKET, BLANKET_S, TRIM, SADDLE, EYE,
                 WHITE, MUZZLE, FAR_LEG]

# The rival rides a dapple-grey horse with a dark mane and a blue blanket (same colour order).
RIVAL_PALETTE = [rgb("#282838"), rgb("#c0c0d0"), rgb("#8888a0"), rgb("#e8e8f0"), rgb("#484058"),
                 rgb("#302838"), rgb("#383040"), rgb("#3070d8"), rgb("#204098"), rgb("#f8f8f8"),
                 rgb("#383038"), rgb("#181018"), rgb("#f8f8f8"), rgb("#707088"), rgb("#686880")]

# Leg poses: (hip angle, knee angle) in degrees from straight down; positive = pointing forward.
# Order: front near, front far, hind near, hind far. bob moves the body up (-) / down (+).
POSES = {
    "gallop0": dict(legs=[(45, 70), (30, 45), (-40, -60), (-25, -40)], bob=-1),
    "gallop1": dict(legs=[(25, 15), (40, 60), (-10, 5), (-30, -45)], bob=0),
    "gallop2": dict(legs=[(-10, -60), (5, 0), (25, 0), (10, -5)], bob=1),
    "gallop3": dict(legs=[(5, -30), (-20, -50), (10, 20), (30, 15)], bob=0),
    "jump":    dict(legs=[(60, -40), (50, -50), (-55, -80), (-45, -70)], bob=-1, head=-1),
    "stumble": dict(legs=[(35, 30), (-15, -10), (-10, -20), (5, 0)], bob=2, head=4),
    "stand":   dict(legs=[(2, 0), (-4, 0), (0, 2), (6, 0)], bob=0),
}
FRAMES = ["gallop0", "gallop1", "gallop2", "gallop3", "jump", "stumble", "stand"]

UPPER, LOWER = 6.5, 7.0
FRONT_HIP = (41, 17)
HIND_HIP = (19, 17)


def leg(c, hip, angles, col, bob):
    hx, hy = hip[0], hip[1] + bob
    a1, a2 = math.radians(angles[0]), math.radians(angles[1])
    kx, ky = hx + math.sin(a1) * UPPER, hy + math.cos(a1) * UPPER
    fx, fy = kx + math.sin(a2) * LOWER, ky + math.cos(a2) * LOWER
    # Keep standing hooves on the ground line.
    if fy > 30.5:
        shift = fy - 30.5
        ky -= shift
        fy -= shift
    thick_line(c, hx, hy - 2, kx, ky, 2.0, col)
    thick_line(c, kx, ky, fx, fy, 1.3, col)
    # Hoof: a small dark blob at the end, angled with the lower leg.
    disc(c, fx + math.sin(a2) * 0.8, fy + math.cos(a2) * 0.8, 1.4, HOOF)


def horse_frame(name):
    pose = POSES[name]
    bob = pose["bob"]
    head = pose.get("head", 0)
    legs = pose["legs"]
    c = Canvas(64, 32)

    # Far legs (darker), each outlined so they read as separate from the near legs.
    far = Canvas(64, 32)
    leg(far, FRONT_HIP, legs[1], FAR_LEG, bob)
    leg(far, HIND_HIP, legs[3], FAR_LEG, bob)
    outline_all(far, OUTLINE)
    layer(c, far)

    # Tail (behind the body): flows back in the wind.
    tail = Canvas(64, 32)
    tail_pts = [(13, 10), (9, 10), (6, 12), (4, 15), (3, 19)] if name != "stand" else \
        [(13, 10), (11, 12), (10, 16), (10, 20), (10, 23)]
    for (x0, y0), (x1, y1) in zip(tail_pts, tail_pts[1:]):
        thick_line(tail, x0, y0 + bob, x1, y1 + bob, 2.0 if x0 > 8 else 1.6, MANE)
    for x, y in [(p[0], p[1] + bob + 1) for p in tail_pts[1:]]:
        tail.set(int(x), int(y) + 1, MANE_S)
    outline_all(tail, OUTLINE)
    layer(c, tail)

    # Near legs.
    near = Canvas(64, 32)
    leg(near, FRONT_HIP, legs[0], COAT, bob)
    leg(near, HIND_HIP, legs[2], COAT, bob)
    outline_all(near, OUTLINE)
    layer(c, near)

    # Body, neck and head as one shape.
    b = Canvas(64, 32)
    b.ellipse(30, 13.5 + bob, 14, 6, COAT)          # barrel
    b.ellipse(19, 12.5 + bob, 7.5, 6.5, COAT)       # hindquarters
    b.ellipse(40, 13.5 + bob, 6.5, 6, COAT)         # chest
    hb = bob + head
    thick_line(b, 40, 12 + bob, 49, 4.5 + hb, 4.0, COAT)  # neck
    b.ellipse(52, 4.5 + hb, 5, 3.6, COAT)                 # head
    b.ellipse(57, 7.5 + hb, 3.6, 2.8, COAT)               # muzzle
    # Shading: highlight along the top edge, shadow along the belly.
    for y in range(32):
        for x in range(64):
            if b.get(x, y) != COAT:
                continue
            if b.get(x, y - 1) is None and y < 12 + bob:
                b.set(x, y, COAT_L)
            elif b.get(x, y + 2) is None or (b.get(x, y + 3) is None and x < 46):
                b.set(x, y, COAT_S)
    # Muzzle tip darker, nostril, eye with a glint.
    for y in range(32):
        for x in range(58, 64):
            if b.get(x, y) in (COAT, COAT_S, COAT_L):
                b.set(x, y, MUZZLE)
    b.set(59, 7 + hb, EYE)
    b.set(53, 3 + hb, EYE)
    b.set(53, 4 + hb, EYE)
    b.set(52, 3 + hb, WHITE)
    # A white blaze down the face.
    for x, y in ((55, 4), (56, 5), (57, 6)):
        b.set(x, y + hb, WHITE)
    # Ears.
    b.set(50, 0 + hb, COAT)
    b.set(50, 1 + hb, COAT)
    b.set(51, 1 + hb, COAT_S)
    # Red winter blanket over the back with a gold trim, and the saddle on top.
    for y in range(32):
        for x in range(22, 38):
            if b.get(x, y) is not None and y <= 16 + bob:
                b.set(x, y, BLANKET if y < 14 + bob else BLANKET_S)
        if b.get(22, y) is not None and y <= 16 + bob:
            b.set(22, y, BLANKET_S)
    for x in range(22, 38):
        b.set(x, 17 + bob, TRIM)
    for x in range(26, 34):
        b.set(x, 7 + bob, SADDLE)
        b.set(x, 8 + bob, SADDLE)
    b.set(25, 6 + bob, SADDLE)
    b.set(34, 7 + bob, SADDLE)
    # Girth strap.
    for y in range(9, 18):
        b.set(31, y + bob, SADDLE)
    # Bridle.
    for x, y in ((54, 6), (55, 7), (56, 8)):
        b.set(x, y + hb, SADDLE)
    outline_all(b, OUTLINE)
    layer(c, b)

    # Mane along the top of the neck, blowing back, plus a forelock.
    for i in range(7):
        t = i / 6
        x = 42 + t * 7
        y = 7.5 + bob - t * 6 + (head * t)
        disc(c, x, y, 1.5, MANE)
        c.set(int(x) - 2, int(y) + (1 if name != "stand" else 2), MANE_S)
    c.set(51, 2 + hb, MANE)
    c.set(52, 2 + hb, MANE)
    c.set(52, 1 + hb, MANE_S)
    return c


def horse_sheet(palette):
    s = Canvas(64, 32 * len(FRAMES))
    for i, f in enumerate(FRAMES):
        s.blit(horse_frame(f), 0, 32 * i)
    if palette is not HORSE_PALETTE:
        recolor(s, dict(zip(HORSE_PALETTE, palette)))
    return s


# ----------------------------------------------------------------------------- obstacles
WOOD = rgb("#a86c34")
WOOD_S = rgb("#704420")
WOOD_L = rgb("#d09858")
WOOD_O = rgb("#382018")
SNOW = rgb("#f8f8f8")
SNOW_S = rgb("#b8d0f0")
HAY = rgb("#e8c048")
HAY_S = rgb("#b88828")
HAY_L = rgb("#f8e888")
FLAG = rgb("#e03838")
FLAG_S = rgb("#a02028")
POLE = rgb("#e8e8f0")
TWINE = rgb("#985828")

OBST_PALETTE = [WOOD_O, WOOD, WOOD_S, WOOD_L, SNOW, SNOW_S, HAY, HAY_S, HAY_L, FLAG, FLAG_S, POLE, TWINE]


def snow_cap(c, x0, x1, y):
    for x in range(x0, x1 + 1):
        c.set(x, y, SNOW)
        c.set(x, y + 1, SNOW_S if (x % 3 == 0) else SNOW)
    c.set(x0, y + 1, SNOW_S)


def fence(height, rails):
    """Two posts with rails between them, standing on row 31, `height` px tall (snow cap included)."""
    c = Canvas(16, 32)
    top = 32 - height
    p = Canvas(16, 32)
    for px in (2, 12):
        p.rect(px, top + 2, 2, height - 2, WOOD)
        p.vline(px + 1, top + 2, 31, WOOD_S)
    for i in range(rails):
        ry = top + 4 + i * ((height - 8) // max(1, rails - 1) if rails > 1 else 0)
        p.rect(1, ry, 15, 2, WOOD)
        p.hline(1, 15, ry, WOOD_L)
    outline_all(p, WOOD_O)
    layer(c, p)
    # Snow on the post tops and the top rail.
    snow_cap(c, 1, 4, top)
    snow_cap(c, 11, 14, top)
    ry = top + 4
    for x in range(2, 15):
        c.set(x, ry - 1, SNOW)
    return c


def fence_knocked(height):
    """The same fence after the horse crashed through it: rails on the ground, posts leaning."""
    c = Canvas(16, 32)
    p = Canvas(16, 32)
    h = height - 4
    for i in range(h):
        p.set(2 + i // 5, 31 - i, WOOD)
        p.set(3 + i // 5, 31 - i, WOOD_S)
        p.set(12 + i // 6, 31 - i, WOOD)
        p.set(13 + i // 6, 31 - i, WOOD_S)
    p.rect(0, 28, 16, 2, WOOD)
    p.hline(0, 15, 28, WOOD_L)
    for x in range(4, 13):
        p.set(x, 25 + (x - 4) // 3, WOOD)
    outline_all(p, WOOD_O)
    layer(c, p)
    for x in (1, 5, 9, 14):
        c.set(x, 30, SNOW)
        c.set(x + 1, 30, SNOW)
    return c


def hay_bale(knocked=False):
    c = Canvas(16, 32)
    b = Canvas(16, 32)
    if not knocked:
        b.rect(1, 21, 14, 10, HAY)
        for x in range(1, 15):
            b.set(x, 30, HAY_S)
            b.set(x, 21, HAY_L)
        for y in range(22, 30):
            for x in range(1, 15):
                if (x + y * 2) % 7 == 0:
                    b.set(x, y, HAY_S)
        for x in (4, 11):
            b.vline(x, 21, 30, TWINE)
        outline_all(b, WOOD_O)
        layer(c, b)
        for x in range(1, 15):
            c.set(x, 20, SNOW)
        for x in (2, 3, 7, 8, 9, 12, 13):
            c.set(x, 19, SNOW)
            c.set(x, 21, SNOW_S if x % 2 else SNOW)
    else:
        for (x, y) in ((1, 27), (6, 28), (11, 26)):
            b.rect(x, y, 5, 4, HAY)
            b.hline(x, x + 4, y, HAY_L)
            b.set(x + 2, y + 2, HAY_S)
        outline_all(b, WOOD_O)
        layer(c, b)
        for x in (0, 5, 10, 15):
            c.set(x, 30, HAY_L)
    return c


def flag():
    c = Canvas(16, 32)
    p = Canvas(16, 32)
    p.rect(3, 4, 2, 28, POLE)
    p.vline(4, 4, 31, SNOW_S)
    for y in range(5, 13):
        w = 9 - abs(y - 9) * 2 // 2
        p.hline(5, 5 + max(1, w), y, FLAG)
    p.hline(5, 12, 11, FLAG_S)
    p.hline(5, 11, 12, FLAG_S)
    outline_all(p, WOOD_O)
    layer(c, p)
    disc(c, 4, 3, 1.6, HAY)
    c.set(4, 2, HAY_L)
    for x in range(1, 8):
        c.set(x, 31, SNOW)
    return c


def obstacle_sheet():
    frames = [fence(15, 2), fence(23, 3), fence_knocked(15), fence_knocked(23), hay_bale(), hay_bale(True), flag()]
    s = Canvas(16, 32 * len(frames))
    for i, f in enumerate(frames):
        s.blit(f, 0, 32 * i)
    return s


def drift_sheet():
    s = Canvas(32, 32)
    for i, h in enumerate((10, 4)):
        c = Canvas(32, 16)
        for x in range(32):
            t = (x - 15.5) / 15.5
            top = 16 - int(round(h * max(0.0, 1 - t * t) ** 0.6)) - (1 if i == 0 and 4 < x < 28 else 0)
            for y in range(max(0, top), 16):
                c.set(x, y, SNOW)
        for y in range(16):
            for x in range(32):
                if c.get(x, y) == SNOW and (x > 18 and c.get(x + 3, y) != SNOW or y == 15):
                    c.set(x, y, SNOW_S)
        outline_all(c, rgb("#7890c8"))
        if i == 1:
            for x in (5, 12, 20, 26):
                c.set(x, 10, SNOW)
        s.blit(c, 0, 16 * i)
    return s


def puff_sheet():
    s = Canvas(16, 48)
    for i, (r, n) in enumerate(((2.5, 3), (3.5, 4), (3.0, 4))):
        c = Canvas(16, 16)
        for k in range(n):
            a = math.pi * (0.15 + 0.7 * k / max(1, n - 1))
            cx = 8 - math.cos(a) * (2 + i * 2)
            cy = 13 - math.sin(a) * (1 + i * 2)
            disc(c, cx, cy, r if i < 2 else r - k * 0.4, SNOW if i < 2 else SNOW_S)
        if i < 2:
            outline_all(c, SNOW_S)
        s.blit(c, 0, 16 * i)
    return s


# ----------------------------------------------------------------------------- backgrounds
def dither_band(c, y0, y1, top_col, bottom_col, x0=0, x1=None):
    """Fills rows y0..y1-1 with top_col, with a 2-row checker transition into bottom_col at the end."""
    x1 = c.w if x1 is None else x1
    for y in range(y0, y1):
        for x in range(x0, x1):
            col = top_col
            if y == y1 - 2 and (x + y) % 4 == 0:
                col = bottom_col
            elif y == y1 - 1 and (x + y) % 2 == 0:
                col = bottom_col
            c.set(x, y, col)


def make_far():
    c = Canvas(256, 256)
    s1, s2, s3, s4 = rgb("#68a8e8"), rgb("#88c0f0"), rgb("#a8d4f8"), rgb("#c8e4f8")
    dither_band(c, 0, 22, s1, s2)
    dither_band(c, 22, 40, s2, s3)
    dither_band(c, 40, 58, s3, s4)
    c.rect(0, 58, 256, 198, s4)
    # Clouds: two soft puffy clouds per 256 px.
    cw, cs = rgb("#f8f8f8"), rgb("#d8e8f8")
    for (cx, cy, scale) in ((44, 16, 1.0), (170, 30, 0.75)):
        cl = Canvas(256, 256)
        for (dx, dy, r) in ((-14, 3, 5), (-6, -1, 7), (4, -3, 8), (13, 1, 6), (20, 4, 4), (-20, 5, 3)):
            disc(cl, cx + dx * scale, cy + dy * scale, r * scale, cw)
        for y in range(256):
            for x in range(256):
                if cl.get(x, y) == cw and cl.get(x, y + 2) is None:
                    cl.set(x, y, cs)
        c.blit(cl, 0, 0)
    # Distant mountains: two ranges, the far one paler.
    m1, m1s, msnow = rgb("#b0c4e8"), rgb("#98acd8"), rgb("#eef4fc")
    m2, m2s = rgb("#90a8d8"), rgb("#7890c0")

    def ridge(x, peaks):
        h = 0
        for (px, ph, pw) in peaks:
            for off in (-256, 0, 256):
                d = abs(x - px - off)
                h = max(h, ph - d * ph / pw)
        return h

    far_peaks = [(20, 34, 40), (80, 26, 34), (130, 38, 44), (200, 30, 40), (240, 22, 26)]
    near_peaks = [(50, 22, 36), (110, 16, 30), (170, 24, 38), (225, 14, 26)]
    for x in range(256):
        h = int(ridge(x, far_peaks))
        base = 78
        for y in range(base - h, base + 30):
            from_top = y - (base - h)
            col = m1 if (x % 64) < 40 or from_top > 8 else m1s
            if from_top < 4 and h > 22:
                col = msnow
            c.set(x, y, col)
        h2 = int(ridge(x, near_peaks))
        base2 = 86
        for y in range(base2 - h2, base2 + 30):
            from_top = y - (base2 - h2)
            col = m2 if from_top > 1 else msnow
            if from_top > 6 and (x // 2) % 3 == 0:
                col = m2s
            c.set(x, y, col)
    return c


# Mid layer colours.
HILL = rgb("#f0f8f8")
HILL_S = rgb("#c8dcf0")
HILL_D = rgb("#a0b8e0")
DARK = rgb("#283048")
PINE = rgb("#1f6848")
PINE_L = rgb("#38905c")
TRUNK = rgb("#6c4028")
BARN = rgb("#c83828")
BARN_S = rgb("#882028")
CREAM = rgb("#f0d8a0")
CREAM_S = rgb("#c09868")
WINDOW = rgb("#f8e070")
ROOF = rgb("#704838")
SMOKE = rgb("#d8e0f0")


def pine(c, x, base, h):
    t = Canvas(c.w, c.h)
    t.rect(x - 1, base - 3, 3, 3, TRUNK)
    tiers = 3 if h >= 16 else 2
    for i in range(tiers):
        ty = base - 3 - (i * h) // (tiers + 1)
        w = (tiers - i) * 3 + 2
        th = h // (tiers) + 2
        for k in range(th):
            half = int(w * (k + 1) / th)
            t.hline(x - half, x + half, ty - th + k + 1, PINE if k > th // 3 else PINE_L)
    # Snow on the tiers' upper edges.
    for y in range(t.h):
        for xx in range(x - 14, x + 15):
            if t.get(xx, y) in (PINE, PINE_L) and t.get(xx, y - 1) is None and (xx + y) % 3 != 0:
                t.set(xx, y, HILL)
    outline_all(t, DARK)
    c.blit(t, 0, 0)


def barn(c, x, base):
    b = Canvas(c.w, c.h)
    w, h = 40, 22
    b.rect(x, base - h, w, h, BARN)
    for xx in range(x, x + w):
        if (xx - x) % 4 == 3:
            b.vline(xx, base - h, base - 1, BARN_S)
    # Gambrel roof with snow.
    for k in range(12):
        b.hline(x - 2 + k // 2, x + w + 1 - k // 2, base - h - 1 - k, ROOF if k < 3 else HILL)
    for k in range(5):
        b.hline(x + 6 + k, x + w - 7 - k, base - h - 13 - k, HILL)
    # White X door and hay loft.
    dx = x + 13
    b.rect(dx, base - 13, 14, 13, BARN_S)
    for i in range(13):
        b.set(dx + i, base - 13 + i, CREAM)
        b.set(dx + 13 - i, base - 13 + i, CREAM)
    b.hline(dx, dx + 13, base - 13, CREAM)
    b.vline(dx, base - 13, base - 1, CREAM)
    b.vline(dx + 13, base - 13, base - 1, CREAM)
    b.rect(x + 16, base - h - 6, 8, 6, DARK)
    b.rect(x + 17, base - h - 5, 6, 4, WINDOW)
    outline_all(b, DARK)
    c.blit(b, 0, 0)


def farmhouse(c, x, base):
    f = Canvas(c.w, c.h)
    w, h = 36, 18
    f.rect(x, base - h, w, h, CREAM)
    f.rect(x + w - 6, base - h, 6, h, CREAM_S)
    # Roof with snow, chimney with a puff of smoke.
    f.rect(x + 26, base - h - 16, 4, 10, BARN_S)
    for k in range(10):
        f.hline(x - 3 + k, x + w + 2 - k, base - h - 1 - k, ROOF if k < 2 else HILL)
    f.rect(x + 26, base - h - 17, 4, 1, HILL)
    for (wx) in (x + 4, x + 22):
        f.rect(wx, base - h + 5, 7, 6, DARK)
        f.rect(wx + 1, base - h + 6, 5, 4, WINDOW)
        f.vline(wx + 3, base - h + 6, base - h + 9, DARK)
    f.rect(x + 14, base - 11, 6, 11, ROOF)
    f.set(x + 18, base - 6, WINDOW)
    outline_all(f, DARK)
    c.blit(f, 0, 0)
    for (dx, dy, r) in ((28, -40, 2.5), (31, -45, 3), (35, -50, 3.5)):
        disc(c, x + dx, base + dy, r, SMOKE)


def make_mid():
    c = Canvas(512, 256)

    def hill_h(x):
        return (14 + 8 * math.sin(x * 2 * math.pi / 512 * 2) + 5 * math.sin(x * 2 * math.pi / 512 * 5 + 1.3))

    base = 92
    for x in range(512):
        top = base - int(hill_h(x))
        for y in range(top, 140):
            col = HILL
            if y == top:
                col = HILL
            elif (y - top) in (2, 3) and (x // 24) % 3 == 0 and x % 2 == y % 2:
                col = HILL_S
            c.set(x, y, col)
        c.set(x, top - 1, HILL_D)
    # A second, lower rolling hill line in front with a shaded edge.
    for x in range(512):
        top = 100 - int(6 + 4 * math.sin(x * 2 * math.pi / 512 * 3 + 0.5))
        c.set(x, top - 1, HILL_D)
        for y in range(top, 140):
            c.set(x, y, HILL_S if y - top < 2 else HILL)

    def ground(x):
        return base - int(hill_h(x)) + 2

    farmhouse(c, 60, ground(78) + 2)
    barn(c, 300, ground(320) + 2)
    for (px, h) in ((24, 18), (36, 14), (130, 20), (144, 15), (158, 22), (210, 16), (250, 20),
                    (370, 18), (384, 24), (398, 15), (452, 20), (470, 14), (494, 18)):
        pine(c, px, ground(px) + 1, h)
    return c


# Near layer colours.
N_SNOW = rgb("#f8f8f8")
N_SNOW2 = rgb("#e0ecf8")
N_SNOW_S = rgb("#b8cce8")
N_SNOW_D = rgb("#8ca4d0")
TRACK = rgb("#d0d8e8")
TRACK_S = rgb("#b0bcd4")
PRINT = rgb("#98a4c4")
FENCE_W = rgb("#a86c34")
FENCE_S = rgb("#704420")
N_DARK = rgb("#383048")
BUSH = rgb("#2f7850")
BERRY = rgb("#d83030")


def make_near():
    c = Canvas(256, 256)
    # Back field (the rival's lane) from row 86.
    c.rect(0, 86, 256, 170, N_SNOW)
    for x in range(256):
        if (x // 2) % 2 == 0:
            c.set(x, 86, N_SNOW2)
    # Rival's trodden lane.
    c.rect(0, 96, 256, 9, N_SNOW2)
    for x in range(0, 256, 16):
        for k in range(3):
            c.set(x + k * 5 + 1, 99 + (k % 2) * 2, N_SNOW_S)
    # Paddock fence along the back of the field (posts every 32 px, two rails).
    f = Canvas(256, 256)
    for x in range(0, 256, 32):
        f.rect(x + 4, 74, 3, 14, FENCE_W)
        f.vline(x + 6, 74, 87, FENCE_S)
    for ry in (77, 82):
        f.rect(0, ry, 256, 2, FENCE_W)
        f.hline(0, 255, ry + 1, FENCE_S)
    outline_all(f, N_DARK)
    c.blit(f, 0, 0)
    for x in range(0, 256, 32):
        c.hline(x + 3, x + 7, 73, N_SNOW)
        c.hline(x + 4, x + 6, 72, N_SNOW)
    for x in range(256):
        if x % 32 not in range(3, 9):
            c.set(x, 76, N_SNOW)
    # Snow bank between the lanes, with little holly bushes.
    for x in range(256):
        top = 106 + int(1.5 * math.sin(x * 2 * math.pi / 64))
        for y in range(top, 114):
            c.set(x, y, N_SNOW if y < top + 3 else N_SNOW_S)
        c.set(x, top - 1, N_SNOW_S)
    for bx in (20, 92, 150, 214):
        b = Canvas(256, 256)
        b.ellipse(bx, 106, 7, 4, BUSH)
        outline_all(b, N_DARK)
        c.blit(b, 0, 0)
        for (dx, dy) in ((-3, -1), (2, 0), (0, -3), (4, -2)):
            c.set(bx + dx, 106 + dy, BERRY)
        c.hline(bx - 5, bx + 4, 102, N_SNOW)
        c.hline(bx - 3, bx + 2, 101, N_SNOW)
    # Main track: packed snow with hoof prints and runner lines.
    c.rect(0, 114, 256, 34, TRACK)
    c.hline(0, 255, 114, N_SNOW_S)
    c.hline(0, 255, 115, N_SNOW_S)
    for x in range(256):
        if (x // 4) % 2 == 0:
            c.set(x, 116, N_SNOW_S)
    for y in (124, 136):
        for x in range(256):
            if (x // 8) % 3 != 2:
                c.set(x, y, TRACK_S)
    for x in range(0, 256, 32):
        for (dx, dy) in ((3, 128), (13, 131), (21, 127), (28, 132)):
            c.set(x + dx, dy, PRINT)
            c.set(x + dx + 1, dy, PRINT)
            c.set(x + dx, dy + 1, TRACK_S)
    for x in range(256):
        if (x // 2) % 3 == 0:
            c.set(x, 141, TRACK_S)
    # Front snow bank (scalloped edge, shaded).
    for x in range(256):
        top = 147 - int(2.5 * abs(math.sin(x * math.pi / 32)))
        c.set(x, top - 1, N_SNOW_D)
        for y in range(top, 256):
            d = y - top
            c.set(x, y, N_SNOW if d < 4 else N_SNOW2 if d < 8 else N_SNOW_S)
        for y in range(top + 4, top + 8):
            if (x + y) % 6 == 0:
                c.set(x, y, N_SNOW_S)
    return c


# ----------------------------------------------------------------------------- output
def tile_count(c):
    tiles = set()
    for ty in range(0, c.h, 8):
        for tx in range(0, c.w, 8):
            t = tuple(c.get(tx + x, ty + y) for y in range(8) for x in range(8))
            tf = tuple(c.get(tx + 7 - x, ty + y) for y in range(8) for x in range(8))
            tiles.add(min(t, tf, key=repr))
    return len(tiles)


def check_sprite(name, c):
    n = len(c.colors())
    if n > 15:
        sys.exit(f"{name}: {n} colours (max 15)")


def main():
    os.makedirs(OUT, exist_ok=True)
    preview = sys.argv[1] if len(sys.argv) > 1 else None

    horse = horse_sheet(HORSE_PALETTE)
    check_sprite("horse", horse)
    horse.save_bmp(os.path.join(OUT, "horse_horse.bmp"), bpp=4, palette=HORSE_PALETTE)
    write_json(os.path.join(OUT, "horse_horse.json"), SPRITE_JSON % 32)

    pal = Canvas(16, 8)
    for i, col in enumerate(RIVAL_PALETTE):
        pal.set(i, 0, col)
    pal.save_bmp(os.path.join(OUT, "horse_rival_pal.bmp"), bpp=4, palette=RIVAL_PALETTE)
    write_json(os.path.join(OUT, "horse_rival_pal.json"), PAL_JSON)

    obst = obstacle_sheet()
    check_sprite("obstacles", obst)
    obst.save_bmp(os.path.join(OUT, "horse_obstacles.bmp"), bpp=4)
    write_json(os.path.join(OUT, "horse_obstacles.json"), SPRITE_JSON % 32)

    drift = drift_sheet()
    drift.save_bmp(os.path.join(OUT, "horse_drift.bmp"), bpp=4)
    write_json(os.path.join(OUT, "horse_drift.json"), SPRITE_JSON % 16)

    puff = puff_sheet()
    puff.save_bmp(os.path.join(OUT, "horse_puff.bmp"), bpp=4)
    write_json(os.path.join(OUT, "horse_puff.json"), SPRITE_JSON % 16)

    bgs = {"horse_far": make_far(), "horse_mid": make_mid(), "horse_near": make_near()}
    for name, bg in bgs.items():
        n = len(bg.colors())
        if n > 15:
            sys.exit(f"{name}: {n} colours (max 15 so it fits one palette bank)")
        bg.save_bmp(os.path.join(OUT, name + ".bmp"), bpp=8)
        write_json(os.path.join(OUT, name + ".json"), BG_JSON)
        print(f"{name}: {n} colours, ~{tile_count(bg)} unique tiles")

    if preview:
        os.makedirs(preview, exist_ok=True)
        rival = horse_sheet(RIVAL_PALETTE)
        sheet = Canvas(64 * 2 + 4 + 16 * 2 + 4 + 32 + 4 + 16, 32 * len(FRAMES))
        sheet.blit(horse, 0, 0)
        sheet.blit(rival, 68, 0)
        sheet.blit(obst, 136, 0)
        sheet.blit(drift, 156, 0)
        sheet.blit(puff, 192, 0)
        save_png(os.path.join(preview, "sprites.png"), sheet, scale=3)
        scene = compose_scene(bgs, horse)
        save_png(os.path.join(preview, "scene.png"), scene, scale=2)
    print(f"Wrote Pony Gallop art to {os.path.normpath(OUT)}")


def compose_scene(bgs, horse):
    """Rough mock-up of the game screen (240x160) for previews."""
    sc = Canvas(240, 160, rgb("#68a8e8"))
    for name, ox in (("horse_far", 0), ("horse_mid", 40), ("horse_near", 0)):
        bg = bgs[name]
        for y in range(160):
            for x in range(240):
                col = bg.get((x + ox) % bg.w, y)
                if col is not None:
                    sc.set(x, y, col)
    sys.path.insert(0, os.path.dirname(__file__))
    import make_characters as mc
    rider = mc._crop(mc.sheet(mc.CHARACTERS[1]), 10)
    other = mc._crop(mc.sheet(mc.CHARACTERS[3]), 10)
    rival = horse_sheet(RIVAL_PALETTE)
    frame0 = Canvas(64, 32)
    frame0.blit(rival, 0, 0)
    sc.blit(frame0, 110, 104 - 32)
    sc.blit(other, 110 + RIDER_DX, 104 - 32 + RIDER_DY)
    f0 = Canvas(64, 32)
    f0.blit(horse, 0, -32 * 0)
    sc.blit(f0, 30, 140 - 32)
    sc.blit(rider, 30 + RIDER_DX, 140 - 32 + RIDER_DY)
    obst = obstacle_sheet()
    o = Canvas(16, 32)
    o.blit(obst, 0, -32)
    sc.blit(o, 150, 140 - 32)
    o2 = Canvas(16, 32)
    o2.blit(obst, 0, 0)
    sc.blit(o2, 200, 140 - 32)
    return sc


# Rider sprite (16x32) offset from the horse sprite's top-left corner (used by the preview and the game).
RIDER_DX = 22
RIDER_DY = -21

if __name__ == "__main__":
    main()
