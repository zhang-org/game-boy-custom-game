"""Generates the Sled Dash art in gba/graphics/sled/.

    python3 tools/art/make_sled.py [preview_dir]

Files (all names start with sled_ because Butano asset names are global):
  sled_hill.bmp       256x256 background: the snowy slope with forest banks on both sides.
                      It repeats vertically, so the game scrolls it forever.
  sled_sled.bmp       32x32 sprite, 3 frames: straight, lean left, lean right.
                      The rider (the character's FRAME_SIT) is drawn on top of it.
  sled_obstacles.bmp  32x32 sprite, 5 frames: pine, snowy pine, rock, two rocks, snowman.
                      Every obstacle stands on the same spot (its "foot" is at row 27).
  sled_present.bmp    16x16 sprite, 3 frames: red, green and blue presents.
  sled_fx.bmp         16x16 sprite, 3 frames: big sparkle, small sparkle, crash star.
  sled_puff.bmp       8x8 sprite, 3 frames: snow spray puffs (big to small).
  sled_track.bmp      32x8 sprite, 3 frames: runner tracks straight, drifting left, drifting right.
  sled_course.bmp     32x32 sprite, 5 frames: red gate, blue gate, log (left half), log (right half),
                      the sled's shadow (while it flies off a ramp).
  sled_ramp.bmp       64x32 sprite: the wide snow ramp (same palette as sled_course).
  sled_banner.bmp     64x32 sprite, 2 frames: the finish banner's left and right halves.

Pass a folder to also write PNG previews (scaled up) there.
"""
import os
import random
import sys

from canvas import Canvas, rgb, write_json, save_png

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
OUT = os.path.join(ROOT, "graphics", "sled")

# ---------------------------------------------------------------------------------- palette
OUTLINE = rgb("#283048")
WHITE = rgb("#f8f8f8")
SNOW = rgb("#e8f0f8")      # base snow
SNOW_S = rgb("#c8d8f0")    # snow shade
SNOW_D = rgb("#98b0d8")    # deep snow shade / soft outline
G_D = rgb("#205840")
G_M = rgb("#308850")
G_L = rgb("#60b060")
TRUNK = rgb("#8a5a32")
TRUNK_D = rgb("#583420")
ROCK_L = rgb("#b8b8c0")
ROCK_M = rgb("#8888a0")
ROCK_D = rgb("#606078")
ORANGE = rgb("#f08828")
RED = rgb("#d83838")
RED_D = rgb("#982030")
GOLD = rgb("#f8d048")
GOLD_D = rgb("#c89028")
GREEN = rgb("#40b048")
GREEN_D = rgb("#287838")
BLUE = rgb("#4880e0")
BLUE_D = rgb("#3058a8")
WOOD = rgb("#c88848")
WOOD_L = rgb("#e8b068")
WOOD_D = rgb("#905828")


def outline(c, color=OUTLINE, skip=()):
    """1px outline on transparent pixels next to any opaque pixel (except `skip` colours)."""
    marks = []
    for y in range(c.h):
        for x in range(c.w):
            if c.get(x, y) is not None:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                v = c.get(x + dx, y + dy)
                if v is not None and v != color and v not in skip:
                    marks.append((x, y))
                    break
    for x, y in marks:
        c.set(x, y, color)


def under(c, x, y, color):
    """Paint only where the canvas is still transparent (shadows go behind things)."""
    if c.get(x, y) is None:
        c.set(x, y, color)


def shadow(c, cx, cy, rx, ry, color=SNOW_S):
    for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
        for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
            dx, dy = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
            if dx * dx + dy * dy <= 1.0:
                under(c, x, y, color)


# ------------------------------------------------------------------------------------ trees
def pine(c, cx, tiers, trunk_top, trunk_bottom, snow_amount=0.42, trunk_w=4):
    """Snow-laden pine. tiers = [(top_y, bottom_y, half_width)] from the top down.
    Light comes from the top left."""
    # Trunk first, so the lowest tier hangs over it.
    for y in range(trunk_top, trunk_bottom + 1):
        for x in range(cx - trunk_w // 2, cx + trunk_w // 2):
            c.set(x, y, TRUNK_D if x >= cx else TRUNK)
    # Draw the lowest tier first: each tier above sits in front of the one below.
    for top, bottom, hw in reversed(tiers):
        height = bottom - top
        snow_rows = max(2, int(height * snow_amount))
        for y in range(top, bottom + 1):
            t = (y - top) / height
            w = 0.6 + hw * t
            for x in range(int(cx - w - 1), int(cx + w + 2)):
                dx = x + 0.5 - cx
                if abs(dx) > w:
                    continue
                # Droopy branch tips along the bottom edge.
                if y == bottom and (x - cx) % 3 == 1:
                    continue
                rel = dx / max(w, 1)
                drip = 1 if (x * 7 + top) % 5 == 0 else 0
                if y - top < snow_rows + drip:
                    col = SNOW_S if rel > 0.45 else WHITE
                    if y - top == snow_rows + drip - 1 and rel > -0.2:
                        col = SNOW_S if rel <= 0.45 else SNOW_D
                elif y >= bottom - 1:
                    col = G_D
                elif rel < -0.35:
                    col = G_L
                elif rel > 0.4:
                    col = G_D
                else:
                    col = G_M
                c.set(x, y, col)


def frame_pine():
    c = Canvas(32, 32)
    pine(c, 16, [(2, 10, 5), (6, 17, 9), (11, 25, 13)], 24, 28)
    outline(c)
    shadow(c, 17, 28, 12, 3)
    return c


def frame_snowy_pine():
    """A rounder pine almost buried in snow."""
    c = Canvas(32, 32)
    pine(c, 16, [(3, 12, 6), (8, 19, 10), (13, 25, 12)], 24, 28, snow_amount=0.62)
    outline(c)
    shadow(c, 17, 28, 11, 3)
    return c


# ------------------------------------------------------------------------------------ rocks
def rock(c, cx, cy, rx, ry, cap_rows):
    for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
        for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
            dx, dy = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
            # Slightly flat bottom, lumpy top.
            if dy > 0:
                dy *= 1.15
            d = dx * dx + dy * dy
            if d > 1.0:
                continue
            top_edge = cy - ry
            wave = 1 if (x % 4 in (0, 1)) else 0
            if y < top_edge + cap_rows + wave:
                col = WHITE if dx < 0.35 else SNOW_S
                if y == int(top_edge + cap_rows + wave) - 1 and dx > -0.5:
                    col = SNOW_S if dx < 0.35 else SNOW_D
            elif dy > 0.55 or dx > 0.55:
                col = ROCK_D
            elif dx < -0.35 and dy < 0.2:
                col = ROCK_L
            else:
                col = ROCK_M
            c.set(x, y, col)


def frame_rock():
    c = Canvas(32, 32)
    rock(c, 16, 22, 10, 6.5, 4)
    # A little crack.
    c.set(18, 24, ROCK_D)
    c.set(19, 25, ROCK_D)
    c.set(13, 23, ROCK_M)
    outline(c)
    shadow(c, 17, 28, 12, 2.5)
    return c


def frame_rocks():
    c = Canvas(32, 32)
    rock(c, 20, 21, 8, 6, 3)
    rock(c, 10, 24, 6, 4.5, 3)
    outline(c)
    shadow(c, 16, 28, 13, 2.5)
    return c


# --------------------------------------------------------------------------------- snowman
def ball(c, cx, cy, r):
    for y in range(int(cy - r) - 1, int(cy + r) + 2):
        for x in range(int(cx - r) - 1, int(cx + r) + 2):
            dx, dy = (x + 0.5 - cx) / r, (y + 0.5 - cy) / r
            d = dx * dx + dy * dy
            if d > 1.0:
                continue
            if dx + dy > 0.75:
                col = SNOW_D
            elif dx + dy > 0.2:
                col = SNOW_S
            else:
                col = WHITE
            c.set(x, y, col)


def frame_snowman():
    c = Canvas(32, 32)
    ball(c, 16, 22, 6.5)
    ball(c, 16, 12.5, 5)
    # Stick arms.
    for i in range(5):
        c.set(10 - i, 17 - i // 2, TRUNK)
        c.set(22 + i, 17 - i // 2, TRUNK)
    c.set(7, 14, TRUNK)
    c.set(25, 14, TRUNK)
    # Scarf.
    c.hline(12, 20, 17, RED)
    c.hline(13, 19, 18, RED_D)
    c.rect(18, 18, 2, 4, RED)
    c.set(19, 21, RED_D)
    # Face: coal eyes, carrot nose, smile.
    c.set(14, 11, OUTLINE)
    c.set(17, 11, OUTLINE)
    c.set(15, 13, ORANGE)
    c.set(16, 13, ORANGE)
    c.set(17, 13, ORANGE)
    c.set(14, 15, ROCK_D)
    c.set(15, 15, ROCK_D)
    c.set(16, 15, ROCK_D)
    # Buttons.
    c.set(16, 21, OUTLINE)
    c.set(16, 24, OUTLINE)
    # Little red bobble hat.
    c.rect(13, 6, 6, 2, RED)
    c.rect(14, 5, 4, 1, RED)
    c.hline(12, 19, 8, WHITE)
    c.set(15, 4, WHITE)
    c.set(16, 4, WHITE)
    c.set(17, 6, RED_D)
    c.set(18, 7, RED_D)
    outline(c)
    shadow(c, 17, 28, 9, 2.5)
    return c


# --------------------------------------------------------------------------------- presents
def frame_present(box, box_d, ribbon, ribbon_d):
    c = Canvas(16, 16)
    # Top face (rows 5..7) and front face (rows 8..13), 3/4 view.
    for y in range(5, 8):
        for x in range(2, 14):
            c.set(x, y, box)
    for y in range(8, 14):
        for x in range(2, 14):
            c.set(x, y, box_d if x >= 11 else box)
    c.hline(2, 13, 8, box_d)
    # Highlight on the top face.
    c.hline(3, 5, 5, WHITE)
    # Ribbon: down the middle of both faces and across the top face.
    for y in range(5, 14):
        c.set(7, y, ribbon)
        c.set(8, y, ribbon_d if y >= 8 else ribbon)
    c.hline(2, 13, 6, ribbon)
    # Bow: two loops and two tails.
    c.ascii(3, 1, ["..rr....rr..",
                   ".rRrr..rrRr.",
                   ".rr.rrrr.rr.",
                   "..rrrRRrrr..",
                   "....r..r...."], {"r": ribbon, "R": ribbon_d})
    outline(c)
    for x in range(3, 14):
        under(c, x, 15, SNOW_S)
    return c


# ------------------------------------------------------------------------------------- sled
def sled_base():
    """The sled from the front-top: wooden deck, red runners curling into a front bar.
    The rider covers the middle (x 9..22, rows 0..27), so mostly the sides and the front show."""
    c = Canvas(32, 32)
    for y in range(15, 28):
        x0, x1 = (7, 24) if y == 15 else (6, 25)
        for x in range(x0, x1 + 1):
            k = (y - 16) % 4
            col = WOOD_D if y == 15 or k == 3 else WOOD_L if k == 0 else WOOD
            if x in (x0, x1):
                col = WOOD_D
            c.set(x, y, col)
    # Runners along both sides, curling up into a bar across the front.
    for y in range(18, 28):
        c.set(5, y, RED)
        c.set(26, y, RED_D)
    c.hline(5, 26, 28, RED)
    c.hline(5, 26, 29, RED)
    c.set(26, 29, RED_D)
    c.hline(6, 25, 30, RED_D)
    for x, y in ((4, 26), (4, 27), (4, 28), (27, 26), (27, 27), (27, 28)):
        c.set(x, y, RED if x == 4 else RED_D)
    # Shine on the front bar.
    c.hline(8, 11, 28, rgb("#f88878"))
    return c


def frame_sled(lean=0):
    base = sled_base()
    if lean == 0:
        c = base
    else:
        # Lean: the side we turn towards dips, the other side lifts (a small shear).
        c = Canvas(32, 32)
        for y in range(32):
            for x in range(32):
                v = base.get(x, y)
                if v is None:
                    continue
                lift = int(round((x - 15.5) / 15.5 * 2 * lean))
                c.set(x + lean, y + lift, v)
    outline(c)
    shadow(c, 16, 31, 12, 1.5)
    return c


# ---------------------------------------------------------------------------------- effects
def frame_sparkle(big):
    c = Canvas(16, 16)
    if big:
        c.ascii(0, 0, ["................",
                       ".......y........",
                       ".......y........",
                       "......yWy.......",
                       "..y...yWy...y...",
                       "...y.yWWWy.y....",
                       "....yWWWWWy.....",
                       ".yyyWWWWWWWyyyy.",
                       "....yWWWWWy.....",
                       "...y.yWWWy.y....",
                       "..y...yWy...y...",
                       "......yWy.......",
                       ".......y........",
                       ".......y........",
                       "................",
                       "................"], {"y": GOLD, "W": WHITE})
    else:
        c.ascii(0, 0, ["................",
                       "................",
                       "................",
                       "................",
                       ".......y........",
                       ".......y........",
                       "......yWy.......",
                       "....yyWWWyy.....",
                       "......yWy.......",
                       ".......y........",
                       ".......y........",
                       "................",
                       "................",
                       "................",
                       "................",
                       "................"], {"y": GOLD, "W": WHITE})
    return c


def frame_crash():
    c = Canvas(16, 16)
    c.ascii(0, 0, ["................",
                   ".......o........",
                   "..o...oyo...o...",
                   "..oyo.oyo.oyo...",
                   "...oyooWooyo....",
                   "....oyWWWyo.....",
                   ".ooooWWWWWoooo..",
                   "oyyyWWWWWWWyyyo.",
                   ".ooooWWWWWoooo..",
                   "....oyWWWyo.....",
                   "...oyooWooyo....",
                   "..oyo.oyo.oyo...",
                   "..o...oyo...o...",
                   ".......o........",
                   "................",
                   "................"], {"y": GOLD, "W": WHITE, "o": ORANGE})
    return c


def frame_puff(size):
    """Soft snow spray: white clumps with a pale shade, no hard outline."""
    c = Canvas(8, 8)
    shapes = {
        2: [".wwww...", "wwwwwws.", "wwwwwws.", "wwwwwss.", ".wwwss..", "..ss....", "........", "........"],
        1: ["........", ".www....", "wwwws...", "wwwss...", ".sss....", "........", "........", "........"],
        0: ["........", "........", ".ww.....", ".wws....", "..s.....", "........", "........", "........"],
    }
    c.ascii(0, 0, shapes[size], {"w": WHITE, "s": SNOW_S})
    outline(c, SNOW_S)  # a soft pale-blue rim so the spray shows up on white snow
    return c


def frame_track(slant):
    c = Canvas(32, 8)
    for y in range(8):
        # slant: 0 straight, -1 the sled drifts left (marks above are further right), +1 right.
        off = int(round((3.5 - y) * 0.5 * -slant)) if slant else 0
        for rx in (5, 25):
            c.set(rx + off, y, SNOW_S)
            c.set(rx + off + 1, y, SNOW_D if y % 4 else SNOW_S)
    return c


# ------------------------------------------------------------------------------- background
BANK_W = 30   # forest bank width in background pixels (each side, inside the visible 240)


def hill():
    """256x256, repeats vertically. Visible columns are 8..247 (the bg is centred on screen)."""
    W, H = 256, 256
    c = Canvas(W, H, SNOW)
    rnd = random.Random(7)

    def wset(x, y, col):
        c.set(x % W, y % H, col)

    # Gentle shading along the edges: the slope curves up into the banks.
    for y in range(H):
        for x in range(W):
            d = min(x - 8, 247 - x)  # distance from the visible screen edge
            if d < BANK_W + 6 and (d >= BANK_W or True):
                if d < BANK_W + 2:
                    c.set(x, y, SNOW_S)
                elif d < BANK_W + 6 and (x + y) % 2 == 0:
                    c.set(x, y, SNOW_S)

    # Snowfield details on an 8px grid so they repeat into the same tiles.
    def ripple(tx, ty, flip):
        pts = [(0, 3), (1, 2), (2, 2), (3, 1), (4, 1), (5, 1), (6, 1), (7, 1), (8, 2), (9, 2), (10, 3)]
        for px, py in pts:
            xx = tx + (10 - px if flip else px) + 2
            wset(xx, ty + py + 2, SNOW_S)
            wset(xx, ty + py + 1, WHITE)

    def sparkle(tx, ty):
        wset(tx + 3, ty + 2, WHITE)
        wset(tx + 2, ty + 3, WHITE)
        wset(tx + 3, ty + 3, WHITE)
        wset(tx + 4, ty + 3, WHITE)
        wset(tx + 3, ty + 4, WHITE)
        wset(tx + 3, ty + 5, SNOW_S)

    def dot(tx, ty):
        wset(tx + 5, ty + 5, WHITE)
        wset(tx + 5, ty + 6, SNOW_S)

    def mound(tx, ty):
        # A soft snow bump, 16x8.
        for xx in range(2, 14):
            h = 3 - abs(xx - 7.5) / 2.5
            for yy in range(int(5 - h), 6):
                wset(tx + xx, ty + yy, WHITE)
            wset(tx + xx, ty + 6, SNOW_S)
        for xx in range(4, 12):
            wset(tx + xx, ty + 7, SNOW_S)

    inner_min, inner_max = 8 + BANK_W + 8, 247 - BANK_W - 8 - 16
    used = set()

    def free(tx, ty, w):
        return all(((tx + i * 8) // 8, (ty // 8)) not in used for i in range(w))

    def claim(tx, ty, w):
        for i in range(-1, w + 1):
            for j in (-1, 0, 1):
                used.add(((tx // 8) + i, (ty // 8 + j) % 32))

    for kind, count, w in (("mound", 7, 2), ("ripple", 14, 2), ("sparkle", 18, 1), ("dot", 20, 1)):
        n = 0
        while n < count:
            tx = rnd.randrange(inner_min // 8, inner_max // 8) * 8
            ty = rnd.randrange(0, 32) * 8
            if not free(tx, ty, w):
                continue
            claim(tx, ty, w)
            if kind == "mound":
                mound(tx, ty)
            elif kind == "ripple":
                ripple(tx, ty, rnd.random() < 0.5)
            elif kind == "sparkle":
                sparkle(tx, ty)
            else:
                dot(tx, ty)
            n += 1

    # Forest banks: rows of small snowy pines, partly hidden by the screen edges.
    tree = Canvas(20, 28)
    pine(tree, 10, [(1, 9, 4), (5, 15, 7), (10, 22, 9)], 21, 25, trunk_w=2)
    outline(tree)
    for y0 in range(0, H, 32):
        for x0, flip in ((-6, False), (8, False), (227, True), (241, True)):
            yy = y0 + (0 if x0 in (-6, 241) else 16)
            for ty in range(tree.h):
                for tx in range(tree.w):
                    v = tree.get(tx, ty)
                    if v is not None:
                        wset(x0 + (tree.w - 1 - tx if flip else tx), yy + ty, v)
    return c


# ---------------------------------------------------------------------------------- the race course
# Gates, ramps, logs, the sled's shadow and the finish banner share ONE palette (one sprite palette slot).
COURSE_PAL = [OUTLINE, WHITE, SNOW, SNOW_S, SNOW_D, RED, RED_D, BLUE, BLUE_D, WOOD, WOOD_D, WOOD_L,
              rgb("#303038"), GOLD]
CHECK_BLACK = rgb("#303038")


def frame_gate(flag, flag_d):
    """A slalom pole with a flag: 32x32, the pole stands at x=15, its foot at row 28."""
    c = Canvas(32, 32)
    for y in range(4, 29):
        c.set(15, y, WHITE if (y // 3) % 2 else flag)
        c.set(16, y, SNOW_S if (y // 3) % 2 else flag_d)
    for y in range(4, 13):                          # the flag, waving to the side
        w = 9 - abs(y - 8)
        for x in range(17, 17 + w):
            c.set(x, y, flag if x < 17 + w - 2 else flag_d)
    c.set(15, 3, GOLD)
    c.set(16, 3, GOLD)
    for x in range(12, 20):                         # a little snow heap at the foot
        c.set(x, 29, SNOW_S)
    outline(c)
    return c


def frame_ramp():
    """A wide snow kicker you ride up: 64x32, the lip (where you take off) at the top, foot at row 28.
    The lip is 36 px wide and the foot 44 px, to match RAMP_HALF = 20 in sled.cpp (hit it with the middle
    of the sled within 20 px of the middle of the ramp)."""
    c = Canvas(64, 32)
    for y in range(12, 29):
        t = (y - 12) / 16
        half = int(18 + 4 * t)
        for x in range(32 - half, 32 + half):
            col = WHITE if y < 15 else SNOW if x < 32 else SNOW_S
            if x in (32 - half, 32 + half - 1):
                col = SNOW_D
            c.set(x, y, col)
    for x in range(13, 51):                         # wooden lip
        c.set(x, 12, WOOD)
        c.set(x, 13, WOOD_D)
    for y in range(18, 28, 3):                      # ridges showing the slope
        for x in range(22, 43):
            if (x + y) % 3:
                c.set(x, y, SNOW_S)
    for x in (16, 47):                              # little marker flags on the corners of the lip
        for y in range(5, 12):
            c.set(x, y, WOOD_D)
        for y in range(5, 8):
            for dx in range(1, 4 - (y - 5)):
                c.set(x + (dx if x < 32 else -dx), y, GOLD)
    outline(c)
    return c


def frame_log(half):
    """Half of a 64 px wide log lying across the track (two frames: left half, right half). Foot at row 28."""
    c = Canvas(32, 32)
    for x in range(32):
        gx = x if half == 0 else x + 32
        if gx < 3 or gx > 60:
            continue
        for y in range(20, 28):
            col = WOOD if y < 24 else WOOD_D
            if y == 21 and gx % 7 == 0:
                col = WOOD_L
            c.set(x, y, col)
        for y in range(18, 20):                     # snow on top
            if (gx // 5) % 3 != 2:
                c.set(x, y, WHITE)
    if half == 0:                                   # the cut end with rings
        for y in range(20, 28):
            for x in range(3, 7):
                c.set(x, y, WOOD_L)
        c.set(4, 23, WOOD_D)
        c.set(5, 24, WOOD_D)
    outline(c)
    return c


def frame_shadow():
    """The sled's shadow on the snow while it flies off a ramp."""
    c = Canvas(32, 32)
    for y in range(14, 19):
        for x in range(6, 26):
            dx, dy = (x - 15.5) / 10, (y - 16) / 2.6
            if dx * dx + dy * dy <= 1:
                c.set(x, y, SNOW_D)
    return c


def frame_banner(half):
    """The finish banner: 64x32 halves (left, right) with posts at both ends and a checkered flag band."""
    c = Canvas(64, 32)
    for x in range(64):
        gx = x if half == 0 else x + 64
        if gx in (2, 3) or gx in (124, 125):        # the posts
            for y in range(4, 30):
                c.set(x, y, WOOD if gx in (2, 124) else WOOD_D)
            continue
        if 4 <= gx <= 123:
            for y in range(5, 15):
                c.set(x, y, CHECK_BLACK if ((gx // 4) + (y - 5) // 5) % 2 else WHITE)
            c.set(x, 4, RED)
            c.set(x, 15, RED)
    outline(c)
    return c


def stack(frames):
    w, h = frames[0].w, frames[0].h
    s = Canvas(w, h * len(frames))
    for i, f in enumerate(frames):
        s.blit(f, 0, h * i)
    return s


def save_sprite(name, frames, preview_dir, palette=None):
    s = stack(frames)
    n = len(s.colors())
    if n > 15:
        sys.exit(f"{name}: {n} colours, max 15")
    s.save_bmp(os.path.join(OUT, name + ".bmp"), bpp=4, palette=palette)
    write_json(os.path.join(OUT, name + ".json"),
               '{\n    "type": "sprite",\n    "height": %d\n}' % frames[0].h)
    if preview_dir:
        save_png(os.path.join(preview_dir, name + ".png"), s, scale=4)


def main():
    preview_dir = sys.argv[1] if len(sys.argv) > 1 else None
    os.makedirs(OUT, exist_ok=True)
    save_sprite("sled_sled", [frame_sled(0), frame_sled(-1), frame_sled(1)], preview_dir)
    save_sprite("sled_obstacles", [frame_pine(), frame_snowy_pine(), frame_rock(), frame_rocks(),
                                   frame_snowman()], preview_dir)
    save_sprite("sled_present", [frame_present(RED, RED_D, GOLD, GOLD_D),
                                 frame_present(GREEN, GREEN_D, RED, RED_D),
                                 frame_present(BLUE, BLUE_D, WHITE, SNOW_S)], preview_dir)
    save_sprite("sled_fx", [frame_sparkle(True), frame_sparkle(False), frame_crash()], preview_dir)
    save_sprite("sled_puff", [frame_puff(2), frame_puff(1), frame_puff(0)], preview_dir)
    save_sprite("sled_track", [frame_track(0), frame_track(-1), frame_track(1)], preview_dir)
    save_sprite("sled_course", [frame_gate(RED, RED_D), frame_gate(BLUE, BLUE_D), frame_log(0),
                                frame_log(1), frame_shadow()], preview_dir, COURSE_PAL)
    save_sprite("sled_ramp", [frame_ramp()], preview_dir, COURSE_PAL)
    save_sprite("sled_banner", [frame_banner(0), frame_banner(1)], preview_dir, COURSE_PAL)

    bg = hill()
    bg.save_bmp(os.path.join(OUT, "sled_hill.bmp"), bpp=8)
    write_json(os.path.join(OUT, "sled_hill.json"),
               '{\n    "type": "regular_bg",\n    "bpp_mode": "bpp_4_auto"\n}')
    if preview_dir:
        save_png(os.path.join(preview_dir, "sled_hill.png"), bg, scale=2)
    print("Sled Dash art written to", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
