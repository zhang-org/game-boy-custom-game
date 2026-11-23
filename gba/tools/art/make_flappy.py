"""Generates the Jingle Flap art in gba/graphics/flappy/ (all original pixel art).

    python3 tools/art/make_flappy.py            # write BMP + JSON files
    python3 tools/art/make_flappy.py --preview DIR   # also write 3x PNG previews into DIR

Sprites
  flappy_reindeer     32x32 x 4 frames: 0 glide, 1 legs down, 2 legs tucked, 3 crashed (eyes shut)
  flappy_sleigh       32x16: the sleigh full of presents pulled behind the reindeer
  flappy_present      8x8 x 3 frames: presents dropped down the chimneys
  flappy_rein         8x8: one bead of the reins
  (the reindeer, sleigh, presents and reins share one palette: REINDEER_PAL)
  flappy_chimney_top  32x16: snowy chimney crown (top of the bottom obstacle)
  flappy_chimney      32x64: brick chimney body (stacks vertically every 64 px)
  flappy_cloud_cap    32x32: bottom of a snow cloud with icicles (bottom of the top obstacle)
  flappy_cloud        32x64: snow cloud column (stacks vertically every 64 px)
  flappy_moon         32x32: full moon
Backgrounds (all wrap horizontally)
  flappy_sky          256x256: night sky gradient with stars
  (the far-away skylines are in make_flappy_cities.py)
  flappy_roofs        512x256: snowy rooftops along the bottom with Christmas lights (the ground)
"""
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(__file__))
from canvas import Canvas, rgb, write_json, save_png  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
OUT = os.path.join(ROOT, "graphics", "flappy")

SPRITE_JSON = '{{\n    "type": "sprite",\n    "height": {h}\n}}'
BG_JSON = '{\n    "type": "regular_bg",\n    "bpp_mode": "bpp_4_auto"\n}'

PREVIEW_DIR = None


def save(c, name, json_text, palette=None):
    c.save_bmp(os.path.join(OUT, name + ".bmp"), bpp=4, palette=palette)
    write_json(os.path.join(OUT, name + ".json"), json_text)
    if PREVIEW_DIR:
        save_png(os.path.join(PREVIEW_DIR, name + ".png"), c, scale=3)


# ----------------------------------------------------------------------------- drawing helpers
def outline_all(c, col, x0=0, y0=0, w=None, h=None):
    """1px outline (colour `col`) on transparent pixels touching any drawn pixel, inside a region."""
    w = c.w if w is None else w
    h = c.h if h is None else h
    marks = []
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            if c.get(x, y) is not None:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if x0 <= nx < x0 + w and y0 <= ny < y0 + h and c.get(nx, ny) not in (None, col):
                    marks.append((x, y))
                    break
    for x, y in marks:
        c.set(x, y, col)


def thick_line(c, x0, y0, x1, y1, r, col, oy=0):
    """Line with round ends, radius r (in pixels), drawn with pixel centres."""
    steps = int(max(abs(x1 - x0), abs(y1 - y0)) * 4) + 1
    for i in range(steps + 1):
        t = i / steps
        cx, cy = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
        for y in range(int(cy - r) - 1, int(cy + r) + 2):
            for x in range(int(cx - r) - 1, int(cx + r) + 2):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                    c.set(x, y + oy, col)


def fill_ellipse(c, cx, cy, rx, ry, col, oy=0, only=None):
    for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
        for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
            dx, dy = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
            if dx * dx + dy * dy <= 1.0:
                if only is None or c.get(x, y + oy) in only:
                    c.set(x, y + oy, col)


# ----------------------------------------------------------------------------- reindeer
R_OUT = rgb("#382028")
R_BODY = rgb("#b07040")
R_DARK = rgb("#805030")
R_LIGHT = rgb("#d09860")
R_CREAM = rgb("#f0e0b8")
R_ANT = rgb("#e8c890")
R_ANT_S = rgb("#a88050")
R_WHITE = rgb("#f8f8f8")
R_RED = rgb("#e03838")
R_RED_S = rgb("#a02028")
R_GOLD = rgb("#f8d048")
R_GOLD_S = rgb("#c89828")
R_GREEN = rgb("#38a050")
R_GREEN_S = rgb("#287038")
R_HOOF = rgb("#503028")

# Legs per pose: list of (points, colour) polylines. Far legs are darker and drawn first.
LEG_POSES = {
    # 0 glide: front legs folded forward, back legs stretched out behind.
    "glide": [([(19, 20), (22, 23), (25, 22)], R_DARK), ([(8, 20), (5, 22), (2, 23)], R_DARK),
              ([(21, 20), (24, 24), (27, 23)], R_BODY), ([(10, 20), (6, 23), (3, 25)], R_BODY)],
    # 1 legs down: the stroke, legs reach down.
    "down": [([(19, 20), (20, 24), (19, 27)], R_DARK), ([(8, 20), (7, 24), (5, 27)], R_DARK),
             ([(21, 20), (23, 24), (23, 28)], R_BODY), ([(10, 20), (10, 24), (8, 28)], R_BODY)],
    # 2 tucked: legs pulled up under the body (the recovery).
    "tuck": [([(19, 20), (22, 22), (20, 24)], R_DARK), ([(8, 20), (11, 23), (9, 25)], R_DARK),
             ([(21, 20), (25, 22), (23, 25)], R_BODY), ([(10, 20), (13, 23), (12, 26)], R_BODY)],
}


def reindeer_frame(c, oy, pose, crashed=False):
    f = Canvas(32, 32)
    # Legs first (behind the body).
    for pts, col in LEG_POSES[pose]:
        for (ax, ay), (bx, by) in zip(pts, pts[1:]):
            thick_line(f, ax, ay, bx, by, 0.9, col)
        hx, hy = pts[-1]
        f.set(hx, hy, R_HOOF)
        # Hoof: the last pixel plus the one leading to it.
        px, py = pts[-2]
        dx = (hx > px) - (hx < px)
        dy = (hy > py) - (hy < py)
        f.set(hx - dx, hy - dy, R_HOOF)
    # Tail (white puff, behind the body).
    fill_ellipse(f, 5.5, 14.5, 2.2, 1.8, R_WHITE)
    # Body.
    fill_ellipse(f, 14.5, 17.0, 9.5, 5.0, R_BODY)
    # Neck and head.
    thick_line(f, 21.5, 15.5, 24.5, 9.5, 2.6, R_BODY)
    fill_ellipse(f, 25.5, 8.0, 3.6, 3.2, R_BODY)
    fill_ellipse(f, 28.5, 9.5, 2.8, 2.0, R_CREAM)
    f.set(31, 9, R_OUT)
    f.set(30, 8, R_OUT)  # nose
    f.set(31, 8, R_OUT)
    # Ear (pointing back).
    for x, y in ((21, 6), (22, 6), (20, 5), (21, 5)):
        f.set(x, y, R_LIGHT)
    f.set(22, 7, R_DARK)
    # Shading on the body: light along the back, dark underneath, cream belly.
    for y in range(32):
        for x in range(32):
            col = f.get(x, y)
            if col != R_BODY:
                continue
            above = f.get(x, y - 2)
            if above is None and y < 16:
                f.set(x, y, R_LIGHT)
            elif y >= 21 and 6 <= x <= 22:
                f.set(x, y, R_DARK)
            elif y >= 19 and 11 <= x <= 20:
                f.set(x, y, R_CREAM)
    # Front of the neck (chest) a bit lighter.
    for x, y in ((25, 12), (25, 13), (24, 14), (24, 15), (23, 16), (24, 13)):
        if f.get(x, y) is not None:
            f.set(x, y, R_CREAM)
    # Antlers: the far one darker, the near one with two tines.
    for pts, col in (([(23, 4), (22, 2), (20, 1)], R_ANT_S), ([(22, 2), (22, 0)], R_ANT_S),
                     ([(25, 4), (26, 2), (25, 0)], R_ANT), ([(26, 2), (28, 1)], R_ANT), ([(25, 1), (24, 0)], R_ANT)):
        for (ax, ay), (bx, by) in zip(pts, pts[1:]):
            steps = max(abs(bx - ax), abs(by - ay))
            for i in range(steps + 1):
                f.set(round(ax + (bx - ax) * i / steps), round(ay + (by - ay) * i / steps), col)
    # Blanket (where the rider sits) with gold trim.
    for y in range(11, 16):
        for x in range(9, 19):
            if f.get(x, y) is not None or y >= 12:
                if f.get(x, y) is not None:
                    f.set(x, y, R_GREEN if y < 15 else R_GOLD)
    for x in range(9, 19):
        if f.get(x, 14) is not None:
            f.set(x, 14, R_GREEN_S)
    for y in range(11, 16):
        if f.get(9, y) is not None:
            f.set(9, y, R_GOLD_S)
        if f.get(18, y) is not None:
            f.set(18, y, R_GOLD_S)
    # Red collar with a golden bell.
    for x, y in ((20, 12), (21, 12), (22, 13), (23, 13), (24, 13), (21, 13), (22, 14)):
        if f.get(x, y) is not None:
            f.set(x, y, R_RED)
    for x, y in ((20, 13), (23, 14)):
        if f.get(x, y) is not None:
            f.set(x, y, R_RED_S)
    f.set(23, 15, R_GOLD)
    f.set(22, 15, R_GOLD)
    f.set(23, 16, R_GOLD_S)
    # Tail tuft shading.
    f.set(4, 15, R_CREAM)
    outline_all(f, R_OUT)
    # Eye (after the outline so it sits inside the head).
    if crashed:
        f.set(25, 7, R_OUT)
        f.set(26, 8, R_OUT)
        f.set(27, 7, R_OUT)
    else:
        f.set(26, 7, R_OUT)
        f.set(26, 8, R_OUT)
        f.set(27, 7, R_WHITE)
    # Cheek blush.
    f.set(25, 10, R_LIGHT)
    c.blit(f, 0, oy)


# The sleigh, presents and reins use the reindeer's colours, so all of them share one sprite palette.
REINDEER_PAL = [R_OUT, R_BODY, R_DARK, R_LIGHT, R_CREAM, R_ANT, R_ANT_S, R_WHITE, R_RED, R_RED_S, R_GOLD, R_GOLD_S,
                R_GREEN, R_GREEN_S, R_HOOF]


def make_reindeer():
    c = Canvas(32, 128)
    reindeer_frame(c, 0, "glide")
    reindeer_frame(c, 32, "down")
    reindeer_frame(c, 64, "tuck")
    reindeer_frame(c, 96, "glide", crashed=True)
    save(c, "flappy_reindeer", SPRITE_JSON.format(h=32), palette=REINDEER_PAL)


def present_box(c, x0, y0, w, h, box, box_s, ribbon):
    """A wrapped present (top-left x0, y0) with a ribbon cross and a little bow on top."""
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            c.set(x, y, box_s if x >= x0 + w - 2 else box)
    mid = x0 + w // 2
    for y in range(y0, y0 + h):
        c.set(mid, y, ribbon)
    for x in range(x0, x0 + w):
        c.set(x, y0 + h // 2, ribbon)
    c.set(mid - 1, y0 - 1, ribbon)
    c.set(mid + 1, y0 - 1, ribbon)


def make_sleigh():
    """flappy_sleigh 32x16: Santa's red sleigh, piled with presents, on golden runners (faces right)."""
    c = Canvas(32, 16)
    # Presents piled in the back (drawn first, the sleigh's side covers their bottoms).
    present_box(c, 4, 2, 7, 7, R_GREEN, R_GREEN_S, R_GOLD)
    present_box(c, 11, 4, 6, 5, R_WHITE, R_CREAM, R_RED)
    present_box(c, 16, 1, 5, 7, R_GOLD, R_GOLD_S, R_RED)
    # The body: a red tub, its back curling up a little and its front rising into a scroll.
    for y in range(7, 13):
        for x in range(3, 25):
            c.set(x, y, R_RED_S if y >= 11 else R_RED)
    for y in range(4, 13):                       # the front scroll
        for x in range(23, 28):
            if (x - 23) + (12 - y) * 0.6 <= 6.5:
                c.set(x, y, R_RED_S if x >= 26 else R_RED)
    for x in range(3, 26):                       # gold trim along the side
        c.set(x, 9, R_GOLD)
    c.set(26, 4, R_GOLD)
    c.set(27, 5, R_GOLD)
    # Runners: two posts and a long golden blade curling up at the front.
    for x in (6, 20):
        c.set(x, 13, R_GOLD_S)
    for x in range(2, 28):
        c.set(x, 14, R_GOLD)
    c.set(28, 13, R_GOLD)
    c.set(29, 12, R_GOLD)
    c.set(29, 11, R_GOLD_S)
    outline_all(c, R_OUT)
    save(c, "flappy_sleigh", SPRITE_JSON.format(h=16), palette=REINDEER_PAL)


def make_present():
    """flappy_present 8x8 x 3 frames: the presents dropped down the chimneys."""
    c = Canvas(8, 24)
    for i, (box, box_s, ribbon) in enumerate(((R_GREEN, R_GREEN_S, R_GOLD), (R_RED, R_RED_S, R_GOLD),
                                              (R_WHITE, R_CREAM, R_RED))):
        f = Canvas(8, 8)
        present_box(f, 1, 2, 6, 5, box, box_s, ribbon)
        outline_all(f, R_OUT)
        c.blit(f, 0, i * 8)
    save(c, "flappy_present", SPRITE_JSON.format(h=8), palette=REINDEER_PAL)


def make_rein():
    """flappy_rein 8x8: one bead of the reins between the sleigh and the reindeer."""
    c = Canvas(8, 8)
    for x, y in ((3, 3), (4, 3), (3, 4), (4, 4)):
        c.set(x, y, R_GOLD)
    c.set(4, 4, R_GOLD_S)
    save(c, "flappy_rein", SPRITE_JSON.format(h=8), palette=REINDEER_PAL)


# ----------------------------------------------------------------------------- chimney
C_OUT = rgb("#301820")
C_BRICK = rgb("#c85038")
C_BRICK_S = rgb("#983028")
C_BRICK_L = rgb("#e87850")
C_MORTAR = rgb("#e0c8b0")
C_MORTAR_S = rgb("#a08878")
C_SNOW = rgb("#f8f8f8")
C_SNOW_S = rgb("#c8d8f0")
C_SNOW_D = rgb("#90a8d8")
C_SOOT = rgb("#482830")
CHIMNEY_PAL = [C_OUT, C_BRICK, C_BRICK_S, C_BRICK_L, C_MORTAR, C_MORTAR_S, C_SNOW, C_SNOW_S, C_SNOW_D, C_SOOT]


def brick_px(x, y, x0, x1, period_offset=0):
    """Colour of a brick wall pixel at (x, y) for a wall spanning x0..x1 (inclusive)."""
    row = (y + period_offset) // 4
    in_row = (y + period_offset) % 4
    shift = 4 if row % 2 else 0
    shaded = x >= x1 - 5
    if in_row == 3 or (x - x0 + shift) % 8 == 0:
        return C_MORTAR_S if shaded else C_MORTAR
    if shaded:
        return C_BRICK_S
    if in_row == 0 and (x - x0 + shift) % 8 in (1, 2, 3):
        return C_BRICK_L
    if x <= x0 + 1:
        return C_BRICK_L
    return C_BRICK


def make_chimney():
    # Body: 24 px wide (x 4..27), tiles every 8 px vertically.
    body = Canvas(32, 64)
    for y in range(64):
        body.set(4, y, C_OUT)
        body.set(27, y, C_OUT)
        for x in range(5, 27):
            body.set(x, y, brick_px(x, y, 5, 26))
    save(body, "flappy_chimney", SPRITE_JSON.format(h=64), palette=CHIMNEY_PAL)

    # Top (32x16): snow heap (rows 0-6), wide brick crown (rows 6-11), then the body (rows 12-15).
    top = Canvas(32, 16)
    for y in range(12, 16):
        top.set(4, y, C_OUT)
        top.set(27, y, C_OUT)
        for x in range(5, 27):
            top.set(x, y, brick_px(x, y - 12 + 4, 5, 26))
    for x in range(5, 27):
        top.set(x, 12, C_SOOT)  # shadow under the crown
    for y in range(7, 11):
        for x in range(2, 30):
            top.set(x, y, brick_px(x, y - 7 + 1, 2, 29))
    for x in range(1, 31):
        top.set(x, 11, C_OUT)
    for y in range(6, 12):
        top.set(1, y, C_OUT)
        top.set(30, y, C_OUT)
    # Snow heap, a soft mound that droops a little over the crown's edges.
    heap = Canvas(32, 16)
    for x in range(2, 30):
        h = 3.0 + 2.4 * math.sin((x - 2) / 27 * math.pi) + (0.7 if x in (9, 10, 11, 19, 20) else 0)
        for y in range(int(round(7 - h)), 7):
            heap.set(x, y, C_SNOW)
    for x, y in ((2, 7), (2, 8), (3, 7), (29, 7), (29, 8), (28, 7), (13, 7), (14, 7), (23, 7), (22, 7)):
        heap.set(x, y, C_SNOW)
    for y in range(16):
        for x in range(32):
            if heap.get(x, y) == C_SNOW:
                if heap.get(x, y + 1) is None:
                    heap.set(x, y, C_SNOW_D if x >= 24 else C_SNOW_S)
                elif x >= 23 and heap.get(x, y - 1) is not None:
                    heap.set(x, y, C_SNOW_S)
    top.blit(heap, 0, 0)
    marks = []
    for y in range(16):
        for x in range(32):
            if top.get(x, y) is None and any(top.get(x + dx, y + dy) in (C_SNOW, C_SNOW_S, C_SNOW_D)
                                             for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                marks.append((x, y))
    for x, y in marks:
        top.set(x, y, C_OUT)
    for x, y in ((8, 3), (17, 2), (12, 4)):
        if top.get(x, y) == C_SNOW:
            top.set(x, y, C_SNOW_S)
    save(top, "flappy_chimney_top", SPRITE_JSON.format(h=16), palette=CHIMNEY_PAL)


# ----------------------------------------------------------------------------- snow cloud with icicles
K_OUT = rgb("#303868")
K_WHITE = rgb("#f8f8f8")
K_SHADE = rgb("#d0e0f8")
K_DEEP = rgb("#a0b8e0")
K_DEEPEST = rgb("#7890c8")
K_ICE_L = rgb("#e8f8ff")
K_ICE = rgb("#a8e0f8")
K_ICE_D = rgb("#60a8e0")
CLOUD_PAL = [K_OUT, K_WHITE, K_SHADE, K_DEEP, K_DEEPEST, K_ICE_L, K_ICE, K_ICE_D]

# The cloud column is a stack of round puffs, repeating every 16 rows (so 64-row sprites stack).
CLOUD_PUFFS = [(10.5, 8.0, 9.0), (21.5, 0.0, 9.0)]  # (x, y, radius) inside one 16-row period


def puff_canvas(rows, extra=(), last_y=999):
    """Draws puffs for rows 0..rows-1 (pattern period 16), plus extra (x, y, r) puffs.

    Returns a canvas where every cloud pixel is shaded by the puff in front of it."""
    c = Canvas(32, rows)
    puffs = []
    for k in range(-1, rows // 16 + 2):
        for px, py, r in CLOUD_PUFFS:
            if py + k * 16 <= last_y:
                puffs.append((px, py + k * 16, r))
    puffs += list(extra)
    for px, py, r in puffs:
        for y in range(int(py - r) - 1, int(py + r) + 2):
            for x in range(int(px - r) - 1, int(px + r) + 2):
                dx, dy = x + 0.5 - px, y + 0.5 - py
                d2 = dx * dx + dy * dy
                if d2 <= r * r:
                    # Light from the upper left: shade the lower right rim of every puff.
                    t = (dx * 0.6 + dy * 0.8) / r
                    if t > 0.78 and d2 > (r - 2.2) ** 2:
                        col = K_DEEP
                    elif t > 0.45 and d2 > (r - 3.5) ** 2:
                        col = K_SHADE
                    else:
                        col = K_WHITE
                    c.set(x, y, col)
    return c


def make_cloud():
    body = puff_canvas(64)
    # Outline only on the left/right sides (top and bottom continue into the next piece).
    marks = []
    for y in range(64):
        for x in range(32):
            if body.get(x, y) is None and (body.get(x + 1, y) is not None or body.get(x - 1, y) is not None):
                marks.append((x, y))
    for x, y in marks:
        body.set(x, y, K_OUT)
    save(body, "flappy_cloud", SPRITE_JSON.format(h=64), palette=CLOUD_PAL)

    # Cap: the column continues at the top, then a wide round bottom with icicles hanging below.
    cap = puff_canvas(32, extra=[(8.0, 13.0, 7.0), (23.5, 12.0, 7.0), (16.0, 15.0, 7.5)], last_y=8)
    ice = Canvas(32, 32)
    icicles = [(4, 7), (8, 4), (12, 10), (17, 6), (21, 11), (25, 5)]
    for ix, length in icicles:
        top_y = 12
        while cap.get(ix + 1, top_y + 1) is not None:
            top_y += 1
        for i in range(length):
            y = top_y + i
            width = 3 if i < length * 0.45 else (2 if i < length * 0.8 else 1)
            for k in range(width):
                col = K_ICE_L if k == 0 and width > 1 else (K_ICE_D if k == width - 1 and width > 1 else K_ICE)
                ice.set(ix + k + (1 if width == 1 else 0), y, col)
    cap.blit(ice, 0, 0)
    marks = []
    for y in range(32):
        for x in range(32):
            if cap.get(x, y) is not None:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                ny = y + dy
                if ny < 0:
                    continue
                if cap.get(x + dx, ny) is not None and cap.get(x + dx, ny) != K_OUT:
                    marks.append((x, y))
                    break
    for x, y in marks:
        cap.set(x, y, K_OUT)
    save(cap, "flappy_cloud_cap", SPRITE_JSON.format(h=32), palette=CLOUD_PAL)


# ----------------------------------------------------------------------------- moon
def make_moon():
    out, base, shade, crater, crater_d, shine = (rgb("#706898"), rgb("#f8f0c8"), rgb("#e8d898"), rgb("#d8c888"),
                                                 rgb("#b8a870"), rgb("#fffff0"))
    c = Canvas(32, 32)
    fill_ellipse(c, 16, 16, 12.5, 12.5, base)
    # Shade on the lower right.
    for y in range(32):
        for x in range(32):
            if c.get(x, y) == base and (x - 11) ** 2 + (y - 11) ** 2 > 15 ** 2:
                c.set(x, y, shade)
    for cx, cy, r in ((12, 11, 3.0), (20, 18, 3.6), (11, 21, 2.0), (21, 9, 1.6), (16, 25, 1.4)):
        fill_ellipse(c, cx, cy, r, r, crater, only=(base, shade))
        # Rim shadow on the upper left of every crater.
        for y in range(32):
            for x in range(32):
                if c.get(x, y) == crater and c.get(x - 1, y - 1) in (base, shade):
                    c.set(x, y, crater_d)
    for x, y in ((9, 8), (10, 7), (8, 9)):
        c.set(x, y, shine)
    outline_all(c, out)
    save(c, "flappy_moon", SPRITE_JSON.format(h=32))


# ----------------------------------------------------------------------------- night sky
def make_sky():
    bands = ["#101840", "#182050", "#202860", "#283068", "#303878", "#384080", "#404890", "#48509a", "#5058a0",
             "#5868a8"]
    bands = [rgb(b) for b in bands]
    star, star_dim = rgb("#f8f8d0"), rgb("#a8b0e8")
    c = Canvas(256, 256)
    # Visible rows are 48..207 (the background is centred). One colour per 2 tile rows.
    for ty in range(32):
        band = max(0, min(len(bands) - 1, (ty - 6) // 2))
        c.rect(0, ty * 8, 256, 8, bands[band])
    # Star stamps: each sits inside one tile.
    stamps = {
        "dot": [(3, 3, star)],
        "dim": [(4, 2, star_dim)],
        "plus": [(3, 3, star), (2, 3, star_dim), (4, 3, star_dim), (3, 2, star_dim), (3, 4, star_dim)],
        "big": [(3, 3, star), (2, 3, star), (4, 3, star), (3, 2, star), (3, 4, star), (1, 3, star_dim),
                (5, 3, star_dim), (3, 1, star_dim), (3, 5, star_dim)],
    }
    rnd = random.Random(7)
    for ty in range(6, 26):
        density = 0.28 if ty < 16 else 0.14
        for tx in range(32):
            if rnd.random() < density:
                kind = rnd.choices(["dim", "dot", "plus", "big"], [5, 4, 2, 1])[0]
                for x, y, col in stamps[kind]:
                    c.set(tx * 8 + x, ty * 8 + y, col)
    save(c, "flappy_sky", BG_JSON)


# ----------------------------------------------------------------------------- far town
def make_town():
    W = 512
    out = rgb("#181838")
    wall = rgb("#363670")
    wall_s = rgb("#2a2a5a")
    roof = rgb("#583058")
    snow = rgb("#c0c8f0")
    snow_s = rgb("#8890c8")
    win = rgb("#f8d870")
    win_w = rgb("#e09848")
    tree = rgb("#1c4048")
    tree_l = rgb("#2c6060")
    c = Canvas(W, 256)

    def put(x, y, col):
        c.set(x % W, y, col)

    def get(x, y):
        return c.get(x % W, y)

    base_y = 207  # bottom visible row
    rnd = random.Random(12)
    x = 0
    items = []
    while x < W - 20:
        kind = rnd.choices(["house", "tree", "church"], [6, 2, 1])[0]
        if kind == "church" and any(k == "church" for k, *_ in items):
            kind = "house"
        if kind == "house":
            w = rnd.choice([24, 28, 32, 36, 40])
            h = rnd.choice([18, 22, 26, 30])
            items.append(("house", x, w, h))
            x += w + rnd.choice([2, 4, 6])
        elif kind == "tree":
            items.append(("tree", x, 14, rnd.choice([20, 24, 28])))
            x += 14 + 2
        else:
            items.append(("church", x, 28, 34))
            x += 30
    for kind, x0, w, h in items:
        top = base_y - 26 - h  # houses stand behind the near rooftops (which cover the bottom 26 rows)
        if kind in ("house", "church"):
            roof_h = w // 2 - 2 if kind == "house" else 12
            # Walls.
            for y in range(top, base_y + 1):
                for x in range(x0, x0 + w):
                    put(x, y, wall_s if x >= x0 + w - 4 else wall)
            # Roof (triangle) above the walls, snow covered.
            for i in range(roof_h):
                y = top - i
                inset = int(i * (w / 2 + 1) / roof_h)
                for x in range(x0 - 2 + inset, x0 + w + 2 - inset):
                    put(x, y, roof)
            for i in range(roof_h):
                y = top - i
                inset = int(i * (w / 2 + 1) / roof_h)
                lx, rx = x0 - 2 + inset, x0 + w + 1 - inset
                for x in range(lx, rx + 1):
                    # Snow covers the roof except a thin eave line.
                    if i > 0:
                        put(x, y, snow if x < (lx + rx) // 2 + 1 else snow_s)
            for x in range(x0 - 2, x0 + w + 2):
                put(x, top, roof)
            # Windows (lit), 4x5 with a cross.
            if kind == "house":
                cols = max(1, (w - 8) // 10)
                rows = max(1, (h - 6) // 10)
                for r in range(rows):
                    for k in range(cols):
                        if rnd.random() < 0.25:
                            continue
                        wx = x0 + 5 + k * ((w - 10) // max(1, cols - 1) if cols > 1 else 0) + (
                            (w - 14) // 2 if cols == 1 else 0)
                        wy = top + 4 + r * 10
                        lit = win if rnd.random() < 0.7 else win_w
                        for yy in range(wy, wy + 5):
                            for xx in range(wx, wx + 4):
                                put(xx, yy, lit)
                        for xx in range(wx, wx + 4):
                            put(xx, wy + 2, out)
            else:
                # Church: tall steeple with a round window.
                sx = x0 + w // 2
                for i in range(18):
                    y = top - roof_h - i + 4
                    half = 3 if i < 12 else max(0, (17 - i) // 2)
                    for x in range(sx - half, sx + half + 1):
                        put(x, y, wall if x < sx + 2 else wall_s)
                for x, y in ((sx, top - roof_h - 14), (sx, top - roof_h - 15)):
                    put(x, y, snow)
                for yy in range(top + 6, top + 14):
                    for xx in range(sx - 3, sx + 3):
                        put(xx, yy, win_w if yy > top + 9 else win)
            # Chimney on some houses.
            if kind == "house" and w >= 32:
                cx = x0 + w - 9
                for y in range(top - roof_h + 3, top - roof_h // 2 + 2):
                    for x in range(cx, cx + 4):
                        if get(x, y) is None or get(x, y) in (snow, snow_s):
                            put(x, y, roof)
                for x in range(cx, cx + 4):
                    put(x, top - roof_h + 2, snow)
        else:
            # Pine tree with snow.
            cx = x0 + 7
            bottom = base_y
            tip = base_y - 26 - h
            for y in range(tip, bottom + 1):
                d = y - tip
                half = min(6, d // 3 + (1 if d % 8 > 4 else 0))
                for x in range(cx - half, cx + half + 1):
                    put(x, y, tree_l if x < cx else tree)
                if d % 8 == 5:
                    for x in range(cx - half, cx + half + 1, 2):
                        put(x, y, snow)
    # Outline.
    marks = []
    for y in range(256):
        for x in range(W):
            if c.get(x, y) is None and any(get(x + dx, y + dy) is not None
                                           for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                marks.append((x, y))
    for x, y in marks:
        c.set(x, y, out)
    save(c, "flappy_town", BG_JSON)


# ----------------------------------------------------------------------------- near rooftops (the ground)
ROOF_SNOW_TOP = 184  # first row of snow (background coordinates; screen y = row - 128)


def make_roofs():
    W = 512
    out = rgb("#282038")
    snow = rgb("#f8f8f8")
    snow_s = rgb("#d0e0f8")
    snow_d = rgb("#98b0e0")
    red, red_s = rgb("#c84040"), rgb("#902838")
    teal, teal_s = rgb("#3890a0"), rgb("#286070")
    yellow = rgb("#f8d040")
    green = rgb("#48c060")
    blue = rgb("#58a0f8")
    wall, wall_s = rgb("#e8d0a0"), rgb("#b89870")
    c = Canvas(W, 256)

    def put(x, y, col):
        c.set(x % W, y, col)

    # Houses: (start x, width, roof colours).
    houses = []
    x = 0
    i = 0
    widths = [96, 80, 112, 64, 88, 72]
    for w in widths:
        houses.append((x, w, (red, red_s) if i % 2 == 0 else (teal, teal_s)))
        x += w
        i += 1
    assert x == W, x
    top = ROOF_SNOW_TOP
    for hx, hw, (rc, rcs) in houses:
        for x in range(hx, hx + hw):
            # Snow lumps on top (period 16 px, varies per house).
            lump = round(1.5 * math.sin((x - hx) / 16 * math.pi * 2 + hx)) + 1
            sy = top + 1 - lump + (0 if lump >= 0 else 0)
            for y in range(sy, 256):
                put(x, y, None)
            for y in range(sy, top + 6):
                put(x, y, snow)
            # Scalloped snow edge hanging over the shingles.
            drip = 6 + (1 if (x - hx) % 8 in (2, 3, 4, 5) else 0) + (1 if (x - hx) % 8 in (3, 4) else 0)
            for y in range(top + 6, top + drip):
                put(x, y, snow_s)
            put(x, top + drip, snow_d)
            # Shingles (rows of 3 px, staggered).
            for y in range(top + drip + 1, top + 17):
                r = (y - top) // 3
                shingle_edge = (y - top) % 3 == 2 or (x - hx + (4 if r % 2 else 0)) % 8 == 0
                put(x, y, rcs if shingle_edge else rc)
            # Gutter wire with lights.
            put(x, top + 17, out)
            # Wall with windows.
            for y in range(top + 18, 208):
                put(x, y, wall_s if (x - hx) % 24 in (0, 23) else wall)
        # Lights every 6 px along the eave.
        colours = [red if rc != red else blue, yellow, green, blue if rc != red else yellow]
        for k, lx in enumerate(range(hx + 3, hx + hw - 2, 6)):
            col = colours[k % len(colours)]
            put(lx, top + 18, col)
            put(lx + 1, top + 18, col)
            put(lx, top + 19, col)
            put(lx + 1, top + 19, col)
            put(lx, top + 17, out)
        # Windows (tops only are visible).
        for wx in range(hx + 10, hx + hw - 12, 24):
            for y in range(top + 21, 208):
                for x in range(wx, wx + 8):
                    put(x, y, yellow if (x - wx) not in (0, 7, 4) else out)
            for x in range(wx - 1, wx + 9):
                put(x, top + 21, out)
        # Divider between houses.
        for y in range(top + 6, 208):
            put(hx, y, out)
    # Outline above the snow.
    marks = []
    for y in range(150, 208):
        for x in range(W):
            if c.get(x, y) is None and c.get(x, y + 1) is not None:
                marks.append((x, y))
            elif c.get(x, y) is None and (c.get((x + 1) % W, y) is not None or c.get((x - 1) % W, y) is not None):
                marks.append((x, y))
    for x, y in marks:
        c.set(x, y, out)
    # Sparkles in the snow.
    for x in range(5, W, 37):
        put(x, top + 3, snow_s)
    save(c, "flappy_roofs", BG_JSON)


def main():
    global PREVIEW_DIR
    if "--preview" in sys.argv:
        PREVIEW_DIR = sys.argv[sys.argv.index("--preview") + 1]
        os.makedirs(PREVIEW_DIR, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)
    make_reindeer()
    make_sleigh()
    make_present()
    make_rein()
    make_chimney()
    make_cloud()
    make_moon()
    make_sky()
    make_roofs()
    print("Wrote Jingle Flap art to", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
