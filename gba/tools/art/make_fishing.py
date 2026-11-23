"""Generates the Ice Fishing art in gba/graphics/fishing/.

    python3 tools/art/make_fishing.py [preview_dir]

Background (256x256, only the top-left 240x160 is on screen):
  fishing_bg       frozen pond in cross-section: winter sky, far shore with pines and a cabin,
                   the ice with a fishing hole, layered water, reeds and rocks on the bottom.
Sprites (all face RIGHT; flip them in code to face left):
  fishing_rod      32x32, 3 frames: 0 held out, 1 raised (charging a cast), 2 bent (fish on).
                   The hand is at pixel (3, 27); the tip is listed in ROD_TIPS below.
  fishing_line     8x8, a 1px vertical line in column 0 (stacked to draw the fishing line).
  fishing_bobber   8x8, 2 frames: 0 floating, 1 dipped (bite).
  fishing_hook     8x8, hook with a worm; the line ties on at pixel (3, 0).
  fishing_bang     16x16 "!" speech bubble, 2 frames (wobble).
  fishing_meter    16x32 cast power meter, 13 frames (0 = empty .. 12 = full).
  fishing_bar      32x16 reel bar pieces, 3 frames: 0 left end, 1 middle, 2 right end.
  fishing_zone     8x16 green "hit" zone piece, 2 frames: 0 normal, 1 glowing (marker inside).
  fishing_marker   8x16 sweeping marker.
  fishing_splash   16x16 splash, 3 frames.
  fishing_sparkle  8x8 twinkle, 2 frames (golden carp, catches).
  fishing_bubble   8x8 small air bubble.
  fishing_<fish>   32x16 fish, 2 swim frames each: minnow, perch, trout, salmon, carp, boot.
                   The mouth is at the x listed in FISH_MOUTH (y = 8).
"""
import os
import struct
import sys

from canvas import Canvas, rgb, save_png, write_json

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
OUT = os.path.join(ROOT, "graphics", "fishing")

OUTLINE = rgb("#202838")
WHITE = rgb("#f8f8f8")

# Layout of the scene in screen pixels (keep in sync with src/games/fishing.cpp).
ICE_TOP = 64          # top of the ice slab
ICE_BOTTOM = 72       # first water row under the ice
HOLE_X0, HOLE_X1 = 58, 72   # the fishing hole (inclusive x range)
STOOL_X = 28          # left edge of the 16 px wide character sprite that sits on the stool
FLOOR_Y = 148         # top of the sandy lake bed (roughly)

ROD_TIPS = [(29, 4), (9, 0), (29, 12)]   # rod tip pixel in each rod frame
CHIMNEY_TOP = (185, 26)   # where the smoke puffs start (the cabin is drawn at x 158, base 55)
BUCKET_TOP = (14, 55)     # middle of the bucket's rim (the caught fish pile up above it)


def sprite_json(height):
    return '{\n    "type": "sprite",\n    "height": %d\n}' % height


def save_sprite(name, canvas, frame_h, palette):
    # Sprites share a few palettes (the GBA only has 16 sprite palettes for everything on screen),
    # so every image is saved with one of the shared palettes below, in the same order.
    canvas.save_bmp(os.path.join(OUT, name + ".bmp"), bpp=4, palette=palette)
    write_json(os.path.join(OUT, name + ".json"), sprite_json(frame_h))


def outline_all(c, color=OUTLINE):
    """1px outline around every non-transparent pixel."""
    marks = []
    for y in range(c.h):
        for x in range(c.w):
            if c.get(x, y) is not None:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                v = c.get(x + dx, y + dy)
                if v is not None and v != color:
                    marks.append((x, y))
                    break
    for x, y in marks:
        c.set(x, y, color)


def line(c, x0, y0, x1, y1, col):
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
    err = dx + dy
    while True:
        c.set(x0, y0, col)
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def stack(frames):
    w, h = frames[0].w, frames[0].h
    s = Canvas(w, h * len(frames))
    for i, f in enumerate(frames):
        s.blit(f, 0, i * h)
    return s


# =============================================================================== background
SKY = [rgb("#b8e0f8"), rgb("#c8e8f8"), rgb("#d8f0f8")]
CLOUD, CLOUD_S = WHITE, rgb("#d0e0f0")
HILL, HILL_S = rgb("#a8b8e0"), rgb("#9098c8")
SNOW, SNOW_S, SNOW_D = WHITE, rgb("#d0e0f0"), rgb("#a0b8d8")
PINE, PINE_L, PINE_D = rgb("#307850"), rgb("#48a060"), rgb("#205038")
TRUNK = rgb("#704830")
LOG, LOG_D, LOG_L = rgb("#b06838"), rgb("#804820"), rgb("#c88850")
ROOF = rgb("#c83838")
WINDOW, WINDOW_L = rgb("#f8d040"), rgb("#f8f0a0")
BRICK = rgb("#a84030")
SMOKE = rgb("#e8e8f0")
ICE_TOP_C, ICE, ICE_M, ICE_D = rgb("#e8f8f8"), rgb("#b0e0f0"), rgb("#88c8e8"), rgb("#5898c8")
WATER = [rgb("#4898d8"), rgb("#3880c0"), rgb("#2868a8"), rgb("#205090"), rgb("#183c78")]
RAY = [rgb("#68b0e8"), rgb("#5098d0"), rgb("#3c80b8"), rgb("#3068a0"), rgb("#285488")]
SAND, SAND_D = rgb("#a89068"), rgb("#786848")
ROCK, ROCK_L = rgb("#687890"), rgb("#98a8b8")
REED, REED_D = rgb("#58a048"), rgb("#387838")
WOOD, WOOD_D = rgb("#c08048"), rgb("#885028")
BUCKET, BUCKET_D = rgb("#e05050"), rgb("#a03038")

# Water bands: (first row, colour index)
WATER_BANDS = [(ICE_BOTTOM, 0), (90, 1), (108, 2), (126, 3), (142, 4)]


def water_index(y):
    idx = 0
    for y0, i in WATER_BANDS:
        if y >= y0:
            idx = i
    return idx


def draw_sky(c):
    # Soft bands with a checker dither between them.
    bands = [(0, 0), (18, 1), (34, 2)]
    for y in range(0, ICE_TOP):
        for x in range(240):
            i = 0
            for y0, b in bands:
                if y >= y0:
                    i = b
            # dither 2 rows above each band change
            for y0, b in bands[1:]:
                if y0 - 2 <= y < y0 and (x + y) % 2 == 0:
                    i = b
            c.set(x, y, SKY[i])


def cloud(c, x, y, w):
    for i, (dx, r) in enumerate([(0, 4), (5, 6), (12, 5), (18, 3)]):
        if dx + r > w + 4:
            break
        c.ellipse(x + dx, y, r + 1, r * 0.7 + 1, CLOUD)
    for xx in range(x - 6, x + w + 6):
        for yy in range(y, y + 6):
            if c.get(xx, yy) == CLOUD and c.get(xx, yy + 1) != CLOUD:
                c.set(xx, yy, CLOUD_S)


def hills(c):
    import math
    for x in range(240):
        top = int(40 + 5 * math.sin(x / 23.0) + 3 * math.sin(x / 9.0 + 1))
        for y in range(top, 56):
            c.set(x, y, HILL)
        c.set(x, top, HILL_S if x % 3 else HILL)
        # snowy tops
        c.set(x, top, SNOW_S)
        c.set(x, top + 1, SNOW_S if x % 4 == 0 else HILL)


def pine(c, cx, base, h):
    """Snowy pine tree, base = y of the ground."""
    tiers = 3 if h < 20 else 4
    trunk_h = 3
    c.rect(cx - 1, base - trunk_h, 2, trunk_h, TRUNK)
    top = base - h
    tier_h = (h - trunk_h) // tiers + 2
    for t in range(tiers):
        y0 = top + t * (tier_h - 2)
        for dy in range(tier_h):
            half = 1 + (dy * (3 + t * 2 + h // 12)) // tier_h
            for x in range(cx - half, cx + half + 1):
                col = PINE
                if x > cx + half // 3:
                    col = PINE_D
                elif x < cx - half // 2:
                    col = PINE_L
                c.set(x, y0 + dy, col)
        # snow on the tier
        for dy in range(2):
            half = 1 + (dy * (3 + t * 2 + h // 12)) // tier_h
            for x in range(cx - half - 1, cx + half + 1):
                c.set(x, y0 + dy, SNOW)
        bh = 1 + ((tier_h - 1) * (3 + t * 2 + h // 12)) // tier_h
        for x in range(cx - bh, cx - bh // 3, 2):
            c.set(x, y0 + tier_h - 1, SNOW)
    c.set(cx, top - 1, SNOW)


def cabin(c, x, base):
    w, wall_h = 34, 13
    y = base - wall_h
    # walls (logs)
    for yy in range(y, base):
        for xx in range(x, x + w):
            c.set(xx, yy, LOG_D if (yy - y) % 3 == 2 else LOG)
    for yy in range(y, base, 3):
        c.set(x - 1, yy, LOG_L)
        c.set(x + w, yy, LOG_L)
    # door
    c.rect(x + 22, base - 9, 6, 9, LOG_D)
    c.rect(x + 23, base - 8, 4, 8, TRUNK)
    c.set(x + 26, base - 4, WINDOW)
    # window with warm light
    c.rect(x + 5, y + 3, 9, 7, OUTLINE)
    c.rect(x + 6, y + 4, 7, 5, WINDOW)
    c.rect(x + 6, y + 4, 3, 2, WINDOW_L)
    c.vline(x + 9, y + 4, y + 8, LOG_D)
    c.hline(x + 6, x + 12, y + 6, LOG_D)
    # roof: red with a thick layer of snow
    rh = 11
    for i in range(rh):
        x0, x1 = x - 4 + i, x + w + 3 - i
        yy = y - 1 - i
        for xx in range(x0, x1 + 1):
            c.set(xx, yy, ROOF)
    for i in range(rh):
        x0, x1 = x - 4 + i, x + w + 3 - i
        yy = y - 1 - i
        for xx in range(x0, x1 + 1):
            if i >= 2 or xx % 5 not in (1, 2):
                c.set(xx, yy - 1, SNOW)
        c.set(x0, yy, SNOW_S)
    c.hline(x - 4, x + w + 3, y - 1, ROOF)
    c.hline(x - 4, x + w + 3, y, OUTLINE)
    # chimney (the smoke is animated sprites: fishing_smoke, CHIMNEY_TOP)
    cx = x + w - 9
    c.rect(cx, y - 13, 5, 7, BRICK)
    c.rect(cx - 1, y - 15, 7, 2, SNOW)


def snow_bank(c):
    import math
    for x in range(240):
        top = int(53 + 2 * math.sin(x / 13.0) + math.sin(x / 5.0))
        for y in range(top, ICE_TOP - 4):
            c.set(x, y, SNOW if y < top + 3 else SNOW_S)
        c.set(x, top - 1, SNOW_S if x % 7 == 0 else c.get(x, top - 1))


def ice(c):
    # Ice surface seen slightly from above (4 rows), then the slab's front face.
    for x in range(240):
        c.set(x, ICE_TOP - 4, SNOW_D)
        c.set(x, ICE_TOP - 3, ICE)
        c.set(x, ICE_TOP - 2, ICE_TOP_C)
        c.set(x, ICE_TOP - 1, ICE_TOP_C)
        c.set(x, ICE_TOP, WHITE)
        for y in range(ICE_TOP + 1, ICE_BOTTOM):
            col = ICE if y < ICE_TOP + 4 else ICE_M
            if y == ICE_BOTTOM - 1:
                col = ICE_D
            c.set(x, y, col)
    # streaks on the surface and cracks on the face
    for x0 in (14, 92, 150, 205):
        c.hline(x0, x0 + 9, ICE_TOP - 3, ICE_TOP_C)
        c.hline(x0 + 3, x0 + 12, ICE_TOP - 2, ICE)
    for x0 in (20, 110, 176, 228):
        c.set(x0, ICE_TOP + 2, ICE_TOP_C)
        c.set(x0 + 1, ICE_TOP + 3, ICE_TOP_C)
        c.set(x0 + 1, ICE_TOP + 4, ICE_TOP_C)
        c.set(x0 + 2, ICE_TOP + 5, WHITE)
    # the hole: an opening through the slab filled with dark water
    c.rect(HOLE_X0 - 2, ICE_TOP - 4, HOLE_X1 - HOLE_X0 + 5, 4, SNOW_D)
    c.rect(HOLE_X0 - 1, ICE_TOP - 3, HOLE_X1 - HOLE_X0 + 3, 3, ICE_D)
    for y in range(ICE_TOP, ICE_BOTTOM):
        for x in range(HOLE_X0, HOLE_X1 + 1):
            c.set(x, y, WATER[0] if y > ICE_TOP + 1 else RAY[0])
        c.set(HOLE_X0 - 1, y, ICE_D)
        c.set(HOLE_X1 + 1, y, ICE_D)
    # a little snow heap by the hole
    for x in range(HOLE_X1 + 2, HOLE_X1 + 10):
        h = 2 - abs(x - (HOLE_X1 + 5)) // 2
        for y in range(ICE_TOP - 4 - h, ICE_TOP - 3):
            c.set(x, y, WHITE)


def water(c):
    for y in range(ICE_BOTTOM, 160):
        for x in range(240):
            i = water_index(y)
            # dither into the next band
            for y0, b in WATER_BANDS[1:]:
                if y0 - 2 <= y < y0 and (x + y) % 2 == 0:
                    i = b
            c.set(x, y, WATER[i])
    # just under the ice the water is a bit brighter
    for x in range(240):
        c.set(x, ICE_BOTTOM, RAY[0])
        if x % 2 == 0:
            c.set(x, ICE_BOTTOM + 1, RAY[0])
    # light rays slanting down from the hole and gaps in the ice
    for rx, width, length in ((HOLE_X0 + 2, 9, 70), (120, 6, 50), (172, 8, 60), (220, 5, 40)):
        for y in range(ICE_BOTTOM, ICE_BOTTOM + length):
            off = (y - ICE_BOTTOM) // 3
            fade = (y - ICE_BOTTOM) / length
            for x in range(rx + off, rx + off + width):
                if fade > 0.6 and (x + y) % 2:
                    continue
                if 0 <= x < 240:
                    i = water_index(y)
                    if c.get(x, y) in WATER:
                        i = WATER.index(c.get(x, y))
                    c.set(x, y, RAY[i])


def lake_bed(c):
    import math
    for x in range(240):
        top = int(FLOOR_Y + 2 * math.sin(x / 11.0) + 2 * math.sin(x / 4.3 + 2))
        for y in range(top, 160):
            c.set(x, y, SAND if y < top + 2 or (x // 3 + y) % 5 else SAND_D)
        c.set(x, top, SAND_D if x % 5 == 0 else SAND)
    # reeds
    for x0, h, lean in ((8, 30, 1), (14, 22, -1), (100, 18, 1), (104, 26, 0), (190, 34, -1),
                        (196, 24, 1), (232, 20, -1)):
        base = FLOOR_Y + 1
        for i in range(h):
            xx = x0 + (lean * i) // 10
            c.set(xx, base - i, REED if i % 7 != 3 else REED_D)
            c.set(xx + 1, base - i, REED_D)
        c.ellipse(x0 + (lean * h) // 10 + 1, base - h, 1.5, 3, REED_D)
    # rocks
    for x0, w, h in ((30, 14, 7), (42, 8, 5), (140, 18, 9), (160, 9, 5), (214, 12, 7)):
        c.ellipse(x0 + w / 2, FLOOR_Y + 6, w / 2, h / 2 + 1, ROCK)
        for yy in range(FLOOR_Y, FLOOR_Y + 6):
            for xx in range(x0, x0 + w // 2):
                if c.get(xx, yy) == ROCK and c.get(xx, yy - 2) not in (ROCK, ROCK_L):
                    c.set(xx, yy, ROCK_L)
    # outline rocks
    marks = []
    for y in range(FLOOR_Y - 6, 160):
        for x in range(240):
            if c.get(x, y) in (ROCK, ROCK_L):
                continue
            if c.get(x, y) in (REED, REED_D):
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if c.get(x + dx, y + dy) in (ROCK, ROCK_L):
                    marks.append((x, y))
                    break
    for x, y in marks:
        c.set(x, y, OUTLINE)


def stool_and_bucket(c):
    # Wooden stool the character sits on (seat top = ICE_TOP - 7).
    sx = STOOL_X + 3
    seat = ICE_TOP - 7
    c.rect(sx, seat, 10, 2, WOOD)
    c.hline(sx, sx + 9, seat + 1, WOOD_D)
    for lx in (sx + 1, sx + 7):
        c.rect(lx, seat + 2, 2, ICE_TOP - seat - 2, WOOD_D)
    c.hline(sx - 1, sx + 10, seat - 1, OUTLINE)
    c.vline(sx - 1, seat, seat + 1, OUTLINE)
    c.vline(sx + 10, seat, seat + 1, OUTLINE)
    # Red bucket for the catch.
    bx, by = 8, ICE_TOP - 9
    for i in range(9):
        c.hline(bx + (i // 4), bx + 11 - (i // 4), by + i, BUCKET if i > 0 else BUCKET_D)
    c.hline(bx, bx + 11, by + 1, BUCKET_D)
    for i in range(9):
        c.set(bx - 1 + (i // 4), by + i, OUTLINE)
        c.set(bx + 12 - (i // 4), by + i, OUTLINE)
    c.hline(bx - 1, bx + 12, by - 1, OUTLINE)
    c.hline(bx + 2, bx + 9, by + 9, OUTLINE)
    c.hline(bx + 1, bx + 10, by, rgb("#183c78"))
    # handle
    for x in range(bx + 1, bx + 11):
        c.set(x, by - 3 - (1 if 3 <= x - bx <= 8 else 0) - (1 if 5 <= x - bx <= 6 else 0), OUTLINE)


def make_background():
    c = Canvas(256, 256)
    draw_sky(c)
    cloud(c, 96, 12, 18)
    cloud(c, 196, 20, 14)
    cloud(c, 28, 26, 10)
    hills(c)
    for cx, h in ((6, 18), (20, 24), (34, 16), (82, 20), (96, 28), (110, 18), (130, 22),
                  (226, 26), (238, 18)):
        pine(c, cx, 56, h)
    cabin(c, 158, 55)
    for cx, h in ((146, 14), (204, 16), (214, 22)):
        pine(c, cx, 56, h)
    snow_bank(c)
    ice(c)
    water(c)
    lake_bed(c)
    stool_and_bucket(c)
    # Off-screen filler (outside 240x160): plain deep water, so it costs one tile.
    for y in range(256):
        for x in range(256):
            if x >= 240 or y >= 160:
                c.set(x, y, WATER[4])
    return c


def tile_stats(c):
    tiles = set()
    worst = 0
    for ty in range(0, c.h, 8):
        for tx in range(0, c.w, 8):
            block = tuple(c.px[(ty + y) * c.w + tx + x] for y in range(8) for x in range(8))
            tiles.add(block)
            worst = max(worst, len(set(block)))
    return len(tiles), worst


# =============================================================================== rod, line, ui bits
ROD, ROD_L = rgb("#383848"), rgb("#8890a8")
CORK, CORK_D = rgb("#d8a868"), rgb("#a87840")
REEL, REEL_L = rgb("#a0a8b8"), rgb("#e0e8f0")
LINE = rgb("#f0f8f8")


def rod_frame(tip, bend=0):
    c = Canvas(32, 32)
    hx, hy = 3, 27
    tx, ty = tip
    # main shaft: straight from the handle, with an optional droop near the tip
    if bend:
        mx, my = (hx + tx) // 2 + 4, (hy + ty) // 2 - 6
        line(c, hx, hy, mx, my, ROD)
        line(c, mx, my, tx, ty, ROD)
    else:
        line(c, hx, hy, tx, ty, ROD)
    # handle (cork) at the hand, two pixels thick
    for i in range(6):
        px = hx - 2 + (i * (tx - hx)) // max(1, abs(ty - hy) + abs(tx - hx)) * 0
    steps = 6
    dx = (tx - hx) / max(abs(tx - hx), abs(ty - hy))
    dy = (ty - hy) / max(abs(tx - hx), abs(ty - hy))
    for i in range(-2, steps):
        x, y = round(hx + dx * i), round(hy + dy * i)
        c.set(x, y, CORK)
        c.set(x, y + 1, CORK_D)
    # reel just above the hand, under the rod
    rx, ry = round(hx + dx * 4), round(hy + dy * 4)
    c.ellipse(rx + 0.5, ry + 3.5, 2, 2, REEL)
    c.set(rx, ry + 3, LINE)
    c.set(rx + 1, ry + 2, ROD)
    # line guides (tiny highlights)
    for f in (0.45, 0.7):
        gx, gy = round(hx + (tx - hx) * f), round(hy + (ty - hy) * f)
        if not bend:
            c.set(gx, gy + 1, ROD_L)
    c.set(tx, ty, ROD_L)
    return c


def make_rod():
    return stack([rod_frame(ROD_TIPS[0]), rod_frame(ROD_TIPS[1]), rod_frame(ROD_TIPS[2], bend=1)])


def make_line():
    c = Canvas(8, 8)
    c.vline(0, 0, 7, LINE)
    return c


BOB_R, BOB_W = rgb("#e83838"), WHITE


def make_bobber():
    frames = []
    for dip in (0, 1):
        c = Canvas(8, 8)
        c.ellipse(3.5, 4.5, 3, 3, BOB_W)
        for y in range(8):
            for x in range(8):
                if c.get(x, y) == BOB_W and y >= 4:
                    c.set(x, y, BOB_R)
        c.set(2, 3, rgb("#ffffff") if False else BOB_W)
        c.set(3, 0, OUTLINE)
        c.set(3, 1, OUTLINE)
        outline_all(c)
        c.set(2, 3, rgb("#f8d0d0"))
        if dip:
            # little ripple marks either side
            c.set(0, 7, LINE)
            c.set(7, 7, LINE)
        frames.append(c)
    return stack(frames)


def make_hook():
    c = Canvas(8, 8)
    hook = REEL
    c.set(3, 0, hook)
    c.vline(3, 1, 5, hook)
    c.ascii(0, 4, ["..wp...", ".pw.p..", "..pppp.", "...pp.."], {"w": hook, "p": rgb("#f088a0")})
    c.set(1, 2, hook)
    c.set(1, 3, hook)
    c.set(2, 6, hook)
    c.set(3, 6, hook)
    c.set(4, 6, hook)
    c.set(5, 5, hook)
    c.set(5, 4, hook)
    return c


def make_bang():
    frames = []
    for wob in (0, 1):
        c = Canvas(16, 16)
        c.ellipse(8, 7 - wob, 6.5, 6.5, WHITE, outline=OUTLINE)
        # tail
        c.set(4, 13 - wob, WHITE)
        c.set(3, 14 - wob, WHITE)
        c.set(4, 14 - wob, OUTLINE)
        c.set(2, 15 - wob, OUTLINE)
        c.set(3, 15 - wob, OUTLINE)
        c.set(5, 13 - wob, WHITE)
        red = BOB_R
        c.rect(7, 3 - wob, 2, 6, red)
        c.rect(7, 10 - wob, 2, 2, red)
        frames.append(c)
    return stack(frames)


def make_meter():
    frames = []
    cols = [rgb("#48c048"), rgb("#a8d838"), rgb("#f8d030"), rgb("#f89030"), rgb("#e83838")]
    for level in range(13):
        c = Canvas(16, 32)
        c.rect(4, 1, 8, 30, OUTLINE)
        c.rect(5, 2, 6, 28, rgb("#384868"))
        c.rect(5, 2, 1, 28, rgb("#283450"))
        h = level * 28 // 12
        for i in range(h):
            y = 29 - i
            col = cols[min(4, i * 5 // 28)]
            c.hline(5, 10, y, col)
            c.set(6, y, WHITE if i % 4 == 1 else col)
        # tick marks
        for y in (9, 16, 23):
            c.set(3, y, OUTLINE)
            c.set(12, y, OUTLINE)
        frames.append(c)
    return stack(frames)


BAR_IN, BAR_IN_D, BAR_FR = rgb("#e8e0d0"), rgb("#c8c0b0"), rgb("#7890c8")


def make_bar():
    frames = []
    for kind in ("left", "mid", "right"):
        c = Canvas(32, 16)
        c.rect(0, 3, 32, 10, BAR_FR)
        c.hline(0, 31, 2, OUTLINE)
        c.hline(0, 31, 13, OUTLINE)
        c.rect(0, 5, 32, 6, BAR_IN)
        c.hline(0, 31, 10, BAR_IN_D)
        c.hline(0, 31, 4, OUTLINE)
        c.hline(0, 31, 11, OUTLINE)
        if kind == "left":
            c.rect(0, 0, 4, 16, None)
            c.rect(4, 2, 1, 12, OUTLINE)
            c.rect(5, 3, 2, 10, BAR_FR)
            c.rect(7, 4, 1, 8, OUTLINE)
            for x in range(0, 4):
                for y in range(16):
                    c.set(x, y, None)
        frames.append(c)
    right = Canvas(32, 16)
    right.blit(frames[0], 0, 0, flip_x=True)
    frames[2] = right
    return stack(frames)


def make_zone():
    frames = []
    for glow in (0, 1):
        c = Canvas(8, 16)
        g = rgb("#48c048") if not glow else rgb("#88e888")
        gd = rgb("#30a040") if not glow else rgb("#48c048")
        c.rect(0, 5, 8, 6, g)
        c.hline(0, 7, 10, gd)
        c.hline(0, 7, 5, WHITE if glow else rgb("#88e888"))
        frames.append(c)
    return stack(frames)


def make_marker():
    c = Canvas(8, 16)
    red, red_d = rgb("#e83838"), rgb("#283450")
    c.ascii(0, 0, ["..ooo...", ".orrro..", "..oro...", "..oro...", "..oro...", "..oro...", "..oro...",
                   "..oro...", "..oro...", "..oro...", "..oro...", "..oro...", "..oDo...", ".orrro..",
                   "..ooo...", "........"], {"o": OUTLINE, "r": red, "D": red_d})
    c.set(3, 2, WHITE)
    return c


def make_splash():
    frames = []
    s, sd = WHITE, rgb("#a8d8f8")
    shapes = [
        ["................", "................", "................", "................", "................",
         "................", "................", "................", "......s..s......", ".....s.ss.s.....",
         "....sddssdds....", "...............", "", "", "", ""],
        ["................", "................", "................", "....s......s....", "...s..s..s..s...",
         "......s..s......", "..s...s..s...s..", "...s..dssd..s...", "...d.dssssd.d...", "..sdddsddsddds..",
         ".ssdddddddddss..", "", "", "", "", ""],
        ["................", ".s............s.", "..s..........s..", "................", "s..............s",
         "................", "..s..........s..", "................", ".s............s.", "..d..........d..",
         "...dd......dd...", "", "", "", "", ""],
    ]
    for rows in shapes:
        c = Canvas(16, 16)
        c.ascii(0, 0, rows, {"s": s, "d": sd})
        frames.append(c)
    return stack(frames)


def make_sparkle():
    frames = []
    y, w = rgb("#f8e858"), WHITE
    for big in (1, 0):
        c = Canvas(8, 8)
        if big:
            c.ascii(0, 0, ["...y....", "...y....", "..ywy...", "yywwwyy.", "..ywy...", "...y....",
                           "...y....", "........"], {"y": y, "w": w})
        else:
            c.ascii(0, 0, ["........", "........", "...y....", "..ywy...", "...y....", "........",
                           "........", "........"], {"y": y, "w": w})
        frames.append(c)
    return stack(frames)


def make_bubble():
    c = Canvas(8, 8)
    c.ellipse(3, 3, 2.5, 2.5, rgb("#a8d8f8"))
    c.ellipse(3, 3, 1.5, 1.5, None) if False else None
    for yy in range(8):
        for xx in range(8):
            if c.get(xx, yy) and 1 < xx < 4 and 1 < yy < 4:
                c.set(xx, yy, rgb("#68b0e8"))
    c.set(2, 2, WHITE)
    return c


# =============================================================================== fish
FISH_MOUTH = {}


def fish_body(c, x0, x1, h, back, side, belly, cy=8):
    """Fills a fish body between x0 (tail end) and x1 (nose). Head is rounder than the tail."""
    length = x1 - x0
    for x in range(x0, x1 + 1):
        t = (x - x0) / length          # 0 at tail, 1 at nose
        if t < 0.65:
            prof = 0.35 + 0.65 * (t / 0.65) ** 0.7
        else:
            u = (t - 0.65) / 0.35
            prof = (1 - u ** 2.2) ** 0.5
        half = max(1.0, h / 2 * prof)
        top = round(cy - half)
        bot = round(cy + half) - 1
        for y in range(top, bot + 1):
            rel = (y - top) / max(1, bot - top)
            col = back if rel < 0.35 else side if rel < 0.7 else belly
            c.set(x, y, col)


def tail(c, x, cy, size, col, phase):
    """Forked tail joined to the body at x, fanning out to the left. phase 1 = wagged 1px down."""
    for i in range(size):
        xx = x - i
        spread = 1 + (i * 4) // (size + 1)
        shift = phase if i > size // 2 else 0
        notch = i - (size * 3) // 5
        for y in range(cy - 1 - spread, cy + spread + 1):
            d = abs(y - cy + 0.5)
            if notch > 0 and d < notch:
                continue
            c.set(xx, y + shift, col)


def eye(c, x, y):
    c.set(x, y, OUTLINE)
    c.set(x, y - 1, WHITE)
    c.set(x - 1, y, WHITE)
    c.set(x - 1, y - 1, WHITE)


def make_fish(kind, phase):
    c = Canvas(32, 16)
    if kind == "minnow":
        back, side, belly = rgb("#5878a8"), rgb("#a8c8e0"), rgb("#f0f0e0")
        x0, x1, h = 12, 25, 6
        tail(c, x0 + 1, 8, 5, rgb("#88a8c8"), phase)
        fish_body(c, x0, x1, h, back, side, belly)
        c.hline(x0 + 2, x1 - 3, 8, rgb("#304878"))
        c.set(x0 + 7, 5, rgb("#88a8c8"))
        c.set(x0 + 8, 4, rgb("#88a8c8"))
        outline_all(c)
        eye(c, x1 - 2, 7)
        FISH_MOUTH[kind] = x1 + 1
    elif kind == "perch":
        back, side, belly = rgb("#68a030"), rgb("#c8d040"), rgb("#f0f0b0")
        fin = rgb("#f07830")
        x0, x1, h = 7, 26, 10
        tail(c, x0 + 1, 8, 6, fin, phase)
        # spiky dorsal fin
        for i, x in enumerate(range(x0 + 6, x0 + 14)):
            c.vline(x, 2 + (i % 2), 4, fin if i < 5 else back)
        fish_body(c, x0, x1, h, back, side, belly)
        for x in (x0 + 5, x0 + 9, x0 + 13):
            c.vline(x, 4, 9, rgb("#406820"))
            c.vline(x + 1, 4, 7, rgb("#406820"))
        c.set(x0 + 11, 13, fin)
        c.set(x0 + 12, 13, fin)
        c.set(x0 + 12, 14 - phase, fin)
        outline_all(c)
        eye(c, x1 - 3, 7)
        c.set(x1 - 1, 9, OUTLINE)
        FISH_MOUTH[kind] = x1 + 1
    elif kind == "trout":
        back, side, belly = rgb("#708848"), rgb("#e88890"), rgb("#f0f0e0")
        fin = rgb("#a8a868")
        x0, x1, h = 7, 28, 9
        tail(c, x0 + 1, 8, 6, fin, phase)
        c.hline(x0 + 9, x0 + 14, 3, fin)
        c.hline(x0 + 10, x0 + 13, 2, fin)
        fish_body(c, x0, x1, h, back, side, belly)
        for x, y in ((x0 + 5, 5), (x0 + 9, 4), (x0 + 12, 6), (x0 + 16, 4), (x0 + 19, 5), (x0 + 8, 10),
                     (x0 + 14, 9)):
            c.set(x, y, rgb("#304828"))
        c.set(x0 + 13, 12, fin)
        c.set(x0 + 14, 13 - phase, fin)
        outline_all(c)
        eye(c, x1 - 3, 7)
        c.set(x1 - 1, 9, OUTLINE)
        FISH_MOUTH[kind] = x1 + 1
    elif kind == "salmon":
        back, side, belly = rgb("#5878a8"), rgb("#f07890"), rgb("#f8d8d8")
        fin = rgb("#c05070")
        x0, x1, h = 7, 29, 10
        tail(c, x0 + 1, 8, 6, fin, phase)
        c.hline(x0 + 10, x0 + 15, 3, fin)
        c.hline(x0 + 11, x0 + 14, 2, fin)
        fish_body(c, x0, x1, h, back, side, belly)
        c.hline(x0 + 6, x1 - 6, 7, rgb("#f8a8b8"))
        for x, y in ((x0 + 7, 5), (x0 + 11, 4), (x0 + 15, 5), (x0 + 19, 4)):
            c.set(x, y, rgb("#304878"))
        c.set(x0 + 15, 13, fin)
        c.set(x0 + 16, 14 - phase, fin)
        outline_all(c)
        eye(c, x1 - 3, 7)
        c.set(x1 - 1, 9, OUTLINE)
        c.set(x1 - 2, 9, OUTLINE)
        FISH_MOUTH[kind] = x1 + 1
    elif kind == "carp":
        back, side, belly = rgb("#e89820"), rgb("#f8c830"), rgb("#f0f0b0")
        fin = rgb("#f07830")
        x0, x1, h = 7, 28, 12
        tail(c, x0 + 1, 8, 6, fin, phase)
        for i, x in enumerate(range(x0 + 8, x0 + 18)):
            c.vline(x, 1 + (1 if i in (0, 9) else 0), 3, fin)
        fish_body(c, x0, x1, h, back, side, belly)
        # scale pattern
        for y in (5, 8, 11):
            for x in range(x0 + 5 + (y % 2) * 2, x1 - 5, 4):
                c.set(x, y, rgb("#f0f0b0") if y < 8 else rgb("#e89820"))
                c.set(x + 1, y - 1, rgb("#f0f0b0") if y < 8 else rgb("#e89820"))
        c.set(x0 + 14, 14, fin)
        c.set(x0 + 15, 14 + 0 - phase, fin)
        outline_all(c)
        eye(c, x1 - 4, 7)
        # whiskers
        c.set(x1, 10, OUTLINE)
        c.set(x1 + 1, 11, rgb("#986038"))
        c.set(x1 + 2, 12 - phase, rgb("#986038"))
        c.set(x1 - 1, 9, OUTLINE)
        c.set(x0 + 9, 6, WHITE)
        FISH_MOUTH[kind] = x1 + 1
    elif kind == "boot":
        leather, leather_d, sole = rgb("#986038"), rgb("#684028"), rgb("#403030")
        weed = rgb("#68a030")
        rows = [
            "..........LLLLLLL...",
            "..........LllllllL..",
            "..........LlwlwllL..",
            "..........LllllllL..",
            "..........LlwlwllL..",
            "..........LllllllL..",
            "....LLLLLLlllllllL..",
            "..LLllllllllllllL...",
            ".LlllllllllllllllL..",
            ".LddddddddddddddddL.",
            ".LssssssssssssssssL.",
            "..LLLLLLLLLLLLLLLL..",
        ]
        c.ascii(5, 2 + phase, rows, {"L": OUTLINE, "l": leather, "d": leather_d, "s": sole, "w": rgb("#f0f0b0")})
        # a strand of pond weed draped over the boot
        for i, (x, y) in enumerate(((17, 2), (18, 3), (19, 4), (20, 5), (21, 6), (22, 7), (22, 8))):
            c.set(x, y + phase, weed)
        c.set(13, 8 + phase, leather_d)
        FISH_MOUTH[kind] = 26
    return c


def make_pike(phase):
    """The hungry pike: a long green fish with yellow spots and a duckbill snout full of teeth, 64x32
    (the fish is in rows 8..23). Faces right like the others; phase 1 = tail wagged."""
    c = Canvas(64, 32)
    back, side, belly = rgb("#406820"), rgb("#68a030"), rgb("#f0f0b0")
    x0, x1, h = 12, 56, 11
    tail(c, x0 + 1, 16, 9, back, phase)
    fish_body(c, x0, x1, h, back, side, belly, cy=16)
    for x in range(x1 - 7, x1 + 4):          # long flat snout
        c.set(x, 15, side)
        c.set(x, 16, belly)
    for x in range(x0 + 8, x1 - 8, 5):       # yellow spots
        c.set(x, 14 + (x // 5) % 2, rgb("#c8d040"))
        c.set(x + 1, 14 + (x // 5) % 2, rgb("#c8d040"))
    for x in range(x0 + 18, x0 + 25):        # dorsal fin set far back
        c.set(x, 10 - (1 if x < x0 + 22 else 0), back)
    outline_all(c)
    for x in range(x1 - 6, x1 + 3, 2):       # teeth
        c.set(x, 17, WHITE)
    eye(c, x1 - 9, 13)
    return c


def make_ice_chunk(variant):
    """A slab of broken ice drifting just under the ice, 32x16."""
    c = Canvas(32, 16)
    w = 24 if variant == 0 else 18
    x0 = (32 - w) // 2
    for y in range(4, 12):
        inset = 2 if y in (4, 11) else 1 if y in (5, 10) else 0
        for x in range(x0 + inset + (y % 3 == 0), x0 + w - inset - (variant and y > 8)):
            c.set(x, y, WHITE if y < 6 else rgb("#a8d8f8") if y < 10 else rgb("#68b0e8"))
    c.set(x0 + 5, 7, WHITE)
    c.set(x0 + 6, 8, WHITE)
    outline_all(c)
    return c


def make_bucket_fill():
    """The catch piling up in the bucket: 16x16 frames 0 (empty) .. 5 (heaped). The bucket rim is at
    row 13; only the forked fish tails sticking out above it are drawn (the sprite sits on top of the
    background bucket)."""
    frames = []
    # (x of the tail's left edge, top row, colour, colour of the fork), one more per frame
    pieces = [(6, 7, rgb("#5878a8"), rgb("#88a8c8")), (10, 6, rgb("#c05070"), rgb("#f07890")),
              (2, 8, rgb("#a8a868"), rgb("#708848")), (8, 4, rgb("#5878a8"), rgb("#a8c8e0")),
              (12, 8, rgb("#c05070"), rgb("#e88890"))]
    for n in range(6):
        c = Canvas(16, 16)
        for (x, top, body, fork) in pieces[:n]:
            for y in range(top + 3, 14):           # the tail end of the body, 2 px wide
                c.set(x, y, body)
                c.set(x + 1, y, body)
            for dx in (-1, 0, 1, 2):               # the forked tail fanning out at the top
                c.set(x + dx, top + 2, fork)
            for dx in (-2, -1, 2, 3):
                c.set(x + dx, top + 1, fork)
            c.set(x - 2, top, fork)
            c.set(x + 3, top, fork)
        outline_all(c)
        for x in range(16):
            for y in range(14, 16):
                c.set(x, y, None)
        frames.append(c)
    return stack(frames)


def make_smoke():
    """A puff of chimney smoke, 16x16: 3 frames, growing and thinning as it rises."""
    frames = []
    for i, r in enumerate((3, 4.5, 5.5)):
        c = Canvas(16, 16)
        c.ellipse(8, 8, r, r * 0.8, WHITE if i < 2 else LINE)
        if i == 2:
            for y in range(16):
                for x in range(16):
                    if c.get(x, y) is not None and (x + y) % 3 == 0:
                        c.set(x, y, None)
        frames.append(c)
    return stack(frames)


FISH_KINDS = ["minnow", "perch", "trout", "salmon", "carp", "boot"]

# ---- shared sprite palettes (15 colours max each)
PAL_TACKLE = [OUTLINE, WHITE, ROD, ROD_L, CORK, CORK_D, REEL, LINE, BOB_R, rgb("#f8d0d0"), rgb("#f088a0"),
              rgb("#a8d8f8"), rgb("#68b0e8"), rgb("#f8e858")]
PAL_GAUGE = [OUTLINE, WHITE, rgb("#384868"), rgb("#283450"), rgb("#48c048"), rgb("#a8d838"), rgb("#f8d030"),
             rgb("#f89030"), rgb("#e83838"), BAR_IN, BAR_IN_D, BAR_FR, rgb("#30a040"), rgb("#88e888")]
PAL_FISH_A = [OUTLINE, WHITE, rgb("#5878a8"), rgb("#a8c8e0"), rgb("#304878"), rgb("#88a8c8"), rgb("#708848"),
              rgb("#e88890"), rgb("#f0f0e0"), rgb("#a8a868"), rgb("#304828"), rgb("#f07890"), rgb("#f8d8d8"),
              rgb("#c05070"), rgb("#f8a8b8")]
PAL_FISH_B = [OUTLINE, WHITE, rgb("#68a030"), rgb("#c8d040"), rgb("#f0f0b0"), rgb("#f07830"), rgb("#406820"),
              rgb("#e89820"), rgb("#f8c830"), rgb("#986038"), rgb("#684028"), rgb("#403030")]
FISH_PALETTE = {"minnow": PAL_FISH_A, "trout": PAL_FISH_A, "salmon": PAL_FISH_A,
                "perch": PAL_FISH_B, "carp": PAL_FISH_B, "boot": PAL_FISH_B}


# =============================================================================== preview helpers
def load_bmp(path):
    """Reads a 4bpp indexed BMP written by canvas.save_bmp (index 0 = transparent)."""
    with open(path, "rb") as f:
        data = f.read()
    offset = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    pal = []
    for i in range(16 if bpp == 4 else 256):
        b, g, r, _ = struct.unpack_from("<BBBB", data, 54 + i * 4)
        pal.append((r, g, b))
    c = Canvas(w, h)
    stride = (((w * bpp + 7) // 8) + 3) & ~3
    for y in range(h):
        row = offset + (h - 1 - y) * stride
        for x in range(w):
            if bpp == 4:
                v = data[row + x // 2]
                i = (v >> 4) if x % 2 == 0 else (v & 15)
            else:
                i = data[row + x]
            if i:
                c.set(x, y, pal[i])
    return c


def frame_of(sheet, fh, i):
    c = Canvas(sheet.w, fh)
    for y in range(fh):
        for x in range(sheet.w):
            c.set(x, y, sheet.get(x, i * fh + y))
    return c


def main():
    os.makedirs(OUT, exist_ok=True)
    preview_dir = sys.argv[1] if len(sys.argv) > 1 else None

    bg = make_background()
    tiles, worst = tile_stats(bg)
    print(f"fishing_bg: {tiles} unique tiles, at most {worst} colours in one tile, "
          f"{len(bg.colors())} colours in total")
    if worst > 15:
        sys.exit("a background tile has more than 15 colours")
    bg.save_bmp(os.path.join(OUT, "fishing_bg.bmp"), bpp=8)
    write_json(os.path.join(OUT, "fishing_bg.json"),
               '{\n    "type": "regular_bg",\n    "bpp_mode": "bpp_4_auto"\n}')

    sprites = {
        "fishing_rod": (make_rod(), 32, PAL_TACKLE),
        "fishing_line": (make_line(), 8, PAL_TACKLE),
        "fishing_bobber": (make_bobber(), 8, PAL_TACKLE),
        "fishing_hook": (make_hook(), 8, PAL_TACKLE),
        "fishing_bang": (make_bang(), 16, PAL_TACKLE),
        "fishing_splash": (make_splash(), 16, PAL_TACKLE),
        "fishing_sparkle": (make_sparkle(), 8, PAL_TACKLE),
        "fishing_bubble": (make_bubble(), 8, PAL_TACKLE),
        "fishing_meter": (make_meter(), 32, PAL_GAUGE),
        "fishing_bar": (make_bar(), 16, PAL_GAUGE),
        "fishing_zone": (make_zone(), 16, PAL_GAUGE),
        "fishing_marker": (make_marker(), 16, PAL_GAUGE),
    }
    for kind in FISH_KINDS:
        sprites["fishing_" + kind] = (stack([make_fish(kind, 0), make_fish(kind, 1)]), 16, FISH_PALETTE[kind])
    sprites["fishing_pike"] = (stack([make_pike(0), make_pike(1)]), 32, PAL_FISH_B)
    sprites["fishing_ice"] = (stack([make_ice_chunk(0), make_ice_chunk(1)]), 16, PAL_TACKLE)
    sprites["fishing_bucket"] = (make_bucket_fill(), 16, PAL_FISH_A)
    sprites["fishing_smoke"] = (make_smoke(), 16, PAL_TACKLE)
    for name, (canvas, fh, pal) in sprites.items():
        save_sprite(name, canvas, fh, pal)
    sprites = {k: (v[0], v[1]) for k, v in sprites.items()}
    print("fish mouths (x):", FISH_MOUTH)
    print(f"Wrote {1 + len(sprites)} images to {os.path.normpath(OUT)}")

    if preview_dir:
        os.makedirs(preview_dir, exist_ok=True)
        # Whole scene mock-up with a character, the rod, line, bobber, hook and some fish.
        scene = Canvas(240, 160)
        scene.blit(bg, 0, 0)
        chr_sheet = load_bmp(os.path.join(ROOT, "graphics", "characters", "chr_charly.bmp"))
        scene.blit(frame_of(chr_sheet, 32, 10), STOOL_X, ICE_TOP - 36)
        rod = sprites["fishing_rod"][0]
        hand = (STOOL_X + 11, ICE_TOP - 36 + 25)
        rx, ry = hand[0] - 3, hand[1] - 27
        scene.blit(frame_of(rod, 32, 0), rx, ry)
        tip = (rx + ROD_TIPS[0][0], ry + ROD_TIPS[0][1])
        hook_y = 110
        for y in range(tip[1] + 1, hook_y):
            scene.set(tip[0], y, LINE)
        scene.blit(sprites["fishing_bobber"][0], tip[0] - 3, ICE_TOP - 4)
        scene.blit(sprites["fishing_hook"][0], tip[0] - 3, hook_y)
        for i, kind in enumerate(FISH_KINDS):
            f = frame_of(sprites["fishing_" + kind][0], 16, i % 2)
            scene.blit(f, 90 + (i % 3) * 48, 76 + i * 12, flip_x=(i % 2 == 1))
        scene.blit(frame_of(sprites["fishing_bang"][0], 16, 0), STOOL_X + 14, ICE_TOP - 50)
        scene.blit(frame_of(sprites["fishing_meter"][0], 32, 8), 10, 24)
        bar = sprites["fishing_bar"][0]
        for i, fr in enumerate((0, 1, 1, 2)):
            scene.blit(frame_of(bar, 16, fr), 104 + i * 32, 20)
        z = sprites["fishing_zone"][0]
        for i in range(3):
            scene.blit(frame_of(z, 16, 0), 150 + i * 8, 20)
        scene.blit(sprites["fishing_marker"][0], 160, 20)
        save_png(os.path.join(preview_dir, "fishing_scene.png"), scene, scale=3)
        # Sprite sheet overview
        sheet = Canvas(400, 140)
        x = 2
        for name, (canvas, fh) in sprites.items():
            if x + canvas.w > 398:
                break
            sheet.blit(canvas, x, 2) if canvas.h <= 136 else sheet.blit(frame_of(canvas, fh, 0), x, 2)
            x += canvas.w + 4
        save_png(os.path.join(preview_dir, "fishing_sprites.png"), sheet, scale=3)
        fish = Canvas(32 * 2 + 6, 18 * len(FISH_KINDS))
        for i, kind in enumerate(FISH_KINDS):
            for p in (0, 1):
                fish.blit(frame_of(sprites["fishing_" + kind][0], 16, p), p * 36, i * 18)
        save_png(os.path.join(preview_dir, "fishing_fish.png"), fish, scale=6, background=WATER[1])


if __name__ == "__main__":
    main()
