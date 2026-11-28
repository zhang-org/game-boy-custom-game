"""Generates the Ping Pong Pals art in gba/graphics/pingpong/.

    python3 tools/art/make_pingpong.py [preview_dir]

Backgrounds (256x256, only the top-left 240x160 is on screen; created at (8, 48) so
canvas pixel (0, 0) is the top-left corner of the screen):
  pingpong_hall   the cosy rec hall: wall with window, garland, wreath, score plaque,
                  Christmas tree, wooden floor and a rug          (BG priority 3)
  pingpong_table  the table, net and legs on their own layer, so the CPU player
                  (who stands behind the table) gets their legs hidden by it (BG priority 2)
Sprites:
  pingpong_ball    8x8, 3 frames: small / normal / big (the ball grows a little when high)
  pingpong_shadow  8x8, 2 frames: normal / small
  pingpong_paddle  16x16, 5 frames, blade to the RIGHT of the handle (flip for backhand):
                   0 rest (blade up), 1 swing (blade level), 2 follow-through (blade down),
                   3 smash wind-up (blade 45 degrees down), 4 smash rising (blade 45 degrees up)
  pingpong_flake   8x8, 2 frames: snowflakes falling behind the window glass
  pingpong_light   8x8, 2 frames: a tree light (on / off) for twinkling

If you pass a folder, previews (3x PNGs) are written there.
Keep TABLE_* in sync with the constants at the top of src/games/pingpong.cpp.
"""
import math
import os
import sys

from canvas import Canvas, rgb, write_json, save_png

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
OUT = os.path.join(ROOT, "graphics", "pingpong")

# Table top in screen pixels (inclusive). Must match pingpong.cpp: x -44..43, z -37..36,
# net at screen y 85 (Butano y +5).
TABLE_X0, TABLE_X1 = 76, 163
TABLE_Y0, TABLE_Y1 = 48, 121
NET_Y = 85
FACE_H = 4       # front face thickness
LEG_H = 6        # visible legs below the face

OUTLINE = rgb("#282030")

# ---------------------------------------------------------------- palette
WALL = rgb("#f0d8a8")
WALL_STRIPE = rgb("#e0c090")
WAIN = rgb("#b07040")
WAIN_DARK = rgb("#885030")
WAIN_LIGHT = rgb("#c88850")
BASE = rgb("#603820")

FLOOR = rgb("#d09058")
FLOOR_SHADE = rgb("#b87848")
FLOOR_LIGHT = rgb("#e0a868")
FLOOR_SEAM = rgb("#905830")

RUG = rgb("#c03840")
RUG_DARK = rgb("#902830")
RUG_BORDER = rgb("#f0d8a8")
RUG_GREEN = rgb("#389050")
RUG_SHADOW = rgb("#701820")

GLASS_TOP = rgb("#203068")
GLASS = rgb("#304888")
GLASS_LOW = rgb("#4868a8")
SNOW = rgb("#f8f8f8")
SNOW_SHADE = rgb("#b8d0f0")
FRAME = rgb("#f8f0e0")
FRAME_SHADE = rgb("#c8b8a0")
PINE = rgb("#183850")
STAR = rgb("#f8f0a0")
CURTAIN = rgb("#c83040")
CURTAIN_DARK = rgb("#902030")
CURTAIN_LIGHT = rgb("#e86068")

GREEN = rgb("#38a048")
GREEN_DARK = rgb("#206838")
GREEN_LIGHT = rgb("#70c860")
RED = rgb("#e03838")
GOLD = rgb("#f8c838")
GOLD_DARK = rgb("#c08820")
BLUE_BAUBLE = rgb("#4890f0")
WHITE = rgb("#f8f8f8")

PLAQUE = rgb("#604028")
PLAQUE_DARK = rgb("#402818")
PLAQUE_LIGHT = rgb("#806040")

TRUNK = rgb("#704020")

TABLE = rgb("#3070d0")
TABLE_SHADE = rgb("#2858b0")
TABLE_LIGHT = rgb("#4888e0")
TABLE_FACE = rgb("#204088")
LINE = rgb("#f8f8f8")
NET = rgb("#383848")
NET_LIGHT = rgb("#9098a8")
LEG = rgb("#484858")
LEG_LIGHT = rgb("#707888")


def draw_wall(c):
    # Wallpaper with soft stripes.
    c.rect(0, 0, 240, 26, WALL)
    for x in range(0, 256, 8):
        c.vline(x + 3, 0, 25, WALL_STRIPE)
        c.vline(x + 4, 0, 25, WALL_STRIPE)
    # Wooden wainscot with panels.
    c.rect(0, 26, 256, 12, WAIN)
    c.hline(0, 255, 26, WAIN_LIGHT)
    for x in range(0, 256, 16):
        c.rect(x + 3, 29, 10, 7, WAIN_DARK)
        c.rect(x + 4, 30, 8, 5, WAIN)
        c.hline(x + 4, x + 11, 30, WAIN_LIGHT)
    c.rect(0, 38, 256, 4, BASE)
    c.hline(0, 255, 38, WAIN_DARK)


def draw_floor(c):
    c.rect(0, 42, 256, 214, FLOOR)
    for row, y in enumerate(range(42, 256, 8)):
        c.hline(0, 255, y, FLOOR_LIGHT)
        c.hline(0, 255, y + 6, FLOOR_SHADE)
        c.hline(0, 255, y + 7, FLOOR_SEAM)
        off = 24 if row % 2 else 0
        for x in range(off, 256, 48):
            c.vline(x, y, y + 6, FLOOR_SEAM)
            c.vline(x + 1, y, y + 6, FLOOR_LIGHT)
        # a little wood grain
        gx = (row * 20 + 10) % 48
        for x in range(gx, 256, 48):
            c.hline(x, x + 5, y + 3, FLOOR_SHADE)


def draw_rug(c):
    x0, y0, x1, y1 = 48, 46, 191, 153
    c.rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, RUG)
    # border: outline, cream band, green stripe
    for i, col in enumerate([RUG_DARK, RUG_BORDER, RUG_BORDER, RUG_GREEN, RUG_BORDER, RUG_DARK]):
        c.hline(x0 + i, x1 - i, y0 + i, col)
        c.hline(x0 + i, x1 - i, y1 - i, col)
        c.vline(x0 + i, y0 + i, y1 - i, col)
        c.vline(x1 - i, y0 + i, y1 - i, col)
    # diamond pattern inside
    for y in range(y0 + 10, y1 - 8, 16):
        for x in range(x0 + 12, x1 - 8, 16):
            for d in range(3):
                c.set(x + d, y - 2 + d, RUG_DARK)
                c.set(x + d, y + 2 - d, RUG_DARK)
                c.set(x - d, y - 2 + d, RUG_DARK)
                c.set(x - d, y + 2 - d, RUG_DARK)
            c.set(x, y, RUG_BORDER)
    # tassels on the short sides
    for x in range(x0 + 2, x1 - 1, 4):
        c.vline(x, y0 - 2, y0 - 1, RUG_BORDER)
        c.vline(x, y1 + 1, y1 + 2, RUG_BORDER)
    # the table's shadow on the rug
    sx0, sx1 = TABLE_X0 + 2, TABLE_X1 - 2
    sy0 = TABLE_Y1 + FACE_H + 1
    for y in range(sy0, sy0 + LEG_H + 2):
        c.hline(sx0, sx1, y, RUG_SHADOW)


def draw_window(c):
    x0, y0, x1, y1 = 14, 4, 57, 32
    # curtains behind the frame edges
    for cx, flip in ((x0 - 7, False), (x1 + 2, True)):
        c.rect(cx, 1, 6, 34, CURTAIN)
        for i in range(0, 34, 1):
            c.set(cx + (4 if not flip else 1), 1 + i, CURTAIN_DARK)
            c.set(cx + (1 if not flip else 4), 1 + i, CURTAIN_LIGHT)
        c.vline(cx - 1, 1, 34, OUTLINE)
        c.vline(cx + 6, 1, 34, OUTLINE)
        c.hline(cx - 1, cx + 6, 35, OUTLINE)
        # tie-back
        c.hline(cx, cx + 5, 24, GOLD)
    # rod
    c.hline(x0 - 9, x1 + 9, 0, OUTLINE)
    c.hline(x0 - 9, x1 + 9, 1, GOLD_DARK)
    # glass: night sky
    for y in range(y0, y1 + 1):
        col = GLASS_TOP if y < y0 + 9 else GLASS if y < y0 + 19 else GLASS_LOW
        c.hline(x0, x1, y, col)
    for sx, sy in ((19, 8), (30, 6), (46, 10), (52, 7), (24, 15), (40, 13)):
        c.set(sx, sy, STAR)
    # snowy hills and a little pine
    for x in range(x0, x1 + 1):
        h = int(26 + 2.5 * math.sin((x - x0) / 6.0)) + (1 if x > 36 else 0)
        c.vline(x, h, y1, SNOW)
        c.set(x, h, SNOW_SHADE) if (x % 5 == 0) else None
    for i in range(7):
        c.hline(44 - i // 2, 44 + i // 2, 18 + i, PINE)
    c.vline(44, 25, 26, PINE)
    for (sx, sy) in ((43, 20), (45, 22), (42, 24)):
        c.set(sx, sy, SNOW)
    # frame
    for y in range(y0 - 2, y1 + 3):
        for x in (x0 - 2, x0 - 1, x1 + 1, x1 + 2):
            c.set(x, y, FRAME)
    for x in range(x0 - 2, x1 + 3):
        for y in (y0 - 2, y0 - 1, y1 + 1, y1 + 2):
            c.set(x, y, FRAME)
    c.vline((x0 + x1) // 2, y0, y1, FRAME)
    c.hline(x0, x1, 17, FRAME)
    c.hline(x0, x1, y1 + 2, FRAME_SHADE)
    # outline around the frame
    c.hline(x0 - 3, x1 + 3, y0 - 3, OUTLINE)
    c.vline(x0 - 3, y0 - 3, y1 + 3, OUTLINE)
    c.vline(x1 + 3, y0 - 3, y1 + 3, OUTLINE)
    # sill
    c.rect(x0 - 5, y1 + 3, x1 - x0 + 11, 3, FRAME)
    c.hline(x0 - 5, x1 + 5, y1 + 5, FRAME_SHADE)
    c.hline(x0 - 6, x1 + 6, y1 + 6, OUTLINE)
    c.vline(x0 - 6, y1 + 3, y1 + 6, OUTLINE)
    c.vline(x1 + 6, y1 + 3, y1 + 6, OUTLINE)


def draw_garland(c, x0, x1):
    """A swag of pine garland hanging from the ceiling between x0 and x1, with baubles."""
    span = x1 - x0
    swags = max(1, round(span / 32))
    w = span / swags
    for s in range(swags):
        a = x0 + s * w
        for i in range(int(w) + 1):
            x = int(a + i)
            t = i / w
            y = int(1 + 6 * math.sin(math.pi * t))
            c.vline(x, y - 1, y + 1, GREEN)
            c.set(x, y - 2 if x % 3 == 0 else y + 2, GREEN_DARK)
            if x % 4 == 1:
                c.set(x, y, GREEN_LIGHT)
        mx = int(a + w / 2)
        my = int(1 + 6)
        cols = [RED, GOLD, BLUE_BAUBLE]
        col = cols[s % 3]
        c.vline(mx, my + 2, my + 3, GOLD_DARK)
        c.ellipse(mx + 0.5, my + 6, 2.5, 2.5, col)
        c.set(mx - 1, my + 5, WHITE)
        # small berries along the swag
        c.set(int(a + w * 0.25), int(1 + 6 * math.sin(math.pi * 0.25)) + 2, RED)
        c.set(int(a + w * 0.75), int(1 + 6 * math.sin(math.pi * 0.75)) + 2, RED)


def draw_plaque(c):
    x0, y0, x1, y1 = 68, 0, 171, 17
    c.rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, PLAQUE)
    c.hline(x0, x1, y1, OUTLINE)
    c.vline(x0, y0, y1, OUTLINE)
    c.vline(x1, y0, y1, OUTLINE)
    c.hline(x0 + 1, x1 - 1, y1 - 1, PLAQUE_DARK)
    c.vline(x0 + 1, y0, y1 - 1, PLAQUE_LIGHT)
    c.vline(x1 - 1, y0, y1 - 1, PLAQUE_DARK)
    # little holly sprigs at the corners
    for hx in (x0 + 5, x1 - 5):
        c.set(hx - 1, 14, GREEN)
        c.set(hx + 1, 14, GREEN)
        c.set(hx, 13, GREEN_LIGHT)
        c.set(hx, 14, RED)


def draw_wreath(c):
    cx, cy = 182, 17
    for y in range(cy - 8, cy + 9):
        for x in range(cx - 8, cx + 9):
            d = math.hypot(x + 0.5 - cx - 0.5, y + 0.5 - cy - 0.5)
            if 3.5 <= d <= 7.5:
                ang = math.atan2(y - cy, x - cx)
                col = GREEN_LIGHT if (d < 5 and ang < 0) else GREEN_DARK if d > 6.5 else GREEN
                c.set(x, y, col)
    c.outline_color(GREEN_DARK, OUTLINE) if False else None
    for ang in range(0, 360, 60):
        bx = int(round(cx + 5.5 * math.cos(math.radians(ang + 20))))
        by = int(round(cy + 5.5 * math.sin(math.radians(ang + 20))))
        c.set(bx, by, RED)
    # bow at the bottom
    c.rect(cx - 3, cy + 6, 3, 3, RED)
    c.rect(cx + 1, cy + 6, 3, 3, RED)
    c.set(cx, cy + 7, CURTAIN_DARK)
    c.set(cx - 1, cy + 10, RED)
    c.set(cx + 2, cy + 10, RED)


def draw_tree(c):
    cx = 217
    # pot
    c.rect(cx - 7, 58, 15, 10, RED)
    c.hline(cx - 7, cx + 7, 60, GOLD)
    c.hline(cx - 8, cx + 8, 57, OUTLINE)
    c.vline(cx - 8, 57, 68, OUTLINE)
    c.vline(cx + 8, 57, 68, OUTLINE)
    c.hline(cx - 8, cx + 8, 68, OUTLINE)
    c.vline(cx - 6, 61, 66, CURTAIN_LIGHT)
    c.rect(cx - 2, 54, 5, 3, TRUNK)
    # three tiers, drawn bottom first
    tiers = [(30, 56, 20), (18, 44, 16), (6, 32, 11)]
    hall = c
    c = Canvas(256, 80)
    for top, bottom, half in tiers:
        for y in range(top, bottom + 1):
            t = (y - top) / (bottom - top)
            hw = int(1 + t * half)
            for x in range(cx - hw, cx + hw + 1):
                # scalloped bottom edge
                if y >= bottom - 1 and (x - cx) % 5 == 0 and abs(x - cx) > 1:
                    continue
                col = GREEN
                if x >= cx + hw - 2 or (y >= bottom - 1):
                    col = GREEN_DARK
                elif x <= cx - hw + 2 and t > 0.15:
                    col = GREEN_LIGHT
                c.set(x, y, col)
    marks = [(x, y) for y in range(c.h) for x in range(c.w) if c.get(x, y) is None and
             any(c.get(x + dx, y + dy) is not None for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))]
    for x, y in marks:
        c.set(x, y, OUTLINE)
    # tinsel and baubles
    for y, xa, xb in ((24, -6, 8), (38, -10, 12), (50, -15, 15)):
        for x in range(xa, xb + 1):
            yy = y + int((x - xa) * 0.25)
            if c.get(cx + x, yy) in (GREEN, GREEN_DARK, GREEN_LIGHT):
                c.set(cx + x, yy, GOLD)
    for bx, by, col in ((-3, 20, RED), (4, 27, BLUE_BAUBLE), (-7, 33, GOLD), (6, 42, RED),
                        (-2, 44, BLUE_BAUBLE), (-12, 52, RED), (10, 53, GOLD), (1, 54, RED)):
        c.rect(cx + bx, by, 2, 2, col)
        c.set(cx + bx, by, WHITE)
    hall.blit(c, 0, 0)
    c = hall
    # star
    c.ascii(cx - 3, 0, ["...#...", "..###..", "#######", ".#####.", "..###..", ".##.##."],
            {"#": GOLD})
    c.set(cx, 2, STAR)
    # presents under the tree
    for px, py, w, h, col, rib in ((cx - 24, 58, 12, 10, BLUE_BAUBLE, GOLD), (cx + 10, 61, 11, 8, GOLD, RED)):
        c.rect(px, py, w, h, col)
        c.vline(px + w // 2, py, py + h - 1, rib)
        c.hline(px, px + w - 1, py + h // 2 - 1, rib)
        c.hline(px - 1, px + w, py - 1, OUTLINE)
        c.hline(px - 1, px + w, py + h, OUTLINE)
        c.vline(px - 1, py - 1, py + h, OUTLINE)
        c.vline(px + w, py - 1, py + h, OUTLINE)
        c.set(px + w // 2 - 1, py - 2, rib)
        c.set(px + w // 2 + 1, py - 2, rib)


# The flip scoreboard in the bottom-left corner. The game writes the two scores on its white cards
# (centres at screen x=16 and x=35, y=118: keep SCOREBOARD_* in pingpong.cpp in sync).
def draw_scoreboard(c):
    x0, y0, w, h = 6, 106, 40, 26
    # legs
    for lx in (x0 + 4, x0 + w - 6):
        c.rect(lx, y0 + h, 2, 8, WAIN_DARK)
        c.vline(lx - 1, y0 + h, y0 + h + 7, OUTLINE)
        c.vline(lx + 2, y0 + h, y0 + h + 7, OUTLINE)
    # wooden frame
    c.rect(x0, y0, w, h, WAIN)
    c.hline(x0, x0 + w - 1, y0, WAIN_LIGHT)
    c.hline(x0, x0 + w - 1, y0 + h - 1, WAIN_DARK)
    c.vline(x0 - 1, y0 - 1, y0 + h, OUTLINE)
    c.vline(x0 + w, y0 - 1, y0 + h, OUTLINE)
    c.hline(x0 - 1, x0 + w, y0 - 1, OUTLINE)
    c.hline(x0 - 1, x0 + w, y0 + h, OUTLINE)
    # two white flip cards with a hinge ring on top and a fold line across the middle
    for cx in (x0 + 3, x0 + 22):
        c.rect(cx, y0 + 4, 15, 19, WHITE)
        c.hline(cx, cx + 14, y0 + 13, SNOW_SHADE)
        c.vline(cx - 1, y0 + 4, y0 + 22, OUTLINE)
        c.vline(cx + 15, y0 + 4, y0 + 22, OUTLINE)
        c.hline(cx - 1, cx + 15, y0 + 23, OUTLINE)
        c.hline(cx - 1, cx + 15, y0 + 3, OUTLINE)
        for rx in (cx + 3, cx + 11):
            c.rect(rx, y0 + 1, 2, 3, PLAQUE_DARK)
    # little colour tabs: green = you (left card), red = them (right card)
    c.rect(x0 + 3, y0 + h - 3, 15, 2, GREEN)
    c.rect(x0 + 22, y0 + h - 3, 15, 2, RED)
    # shadow on the floor
    c.hline(x0, x0 + w + 1, y0 + h + 8, FLOOR_SEAM)


def draw_hall():
    c = Canvas(256, 256)
    draw_floor(c)
    draw_wall(c)
    draw_rug(c)
    draw_window(c)
    draw_garland(c, 0, 66)
    draw_plaque(c)
    draw_wreath(c)
    draw_tree(c)
    draw_scoreboard(c)
    return c


def draw_table():
    c = Canvas(256, 256)
    x0, x1, y0, y1 = TABLE_X0, TABLE_X1, TABLE_Y0, TABLE_Y1
    # legs (behind the face) and the floor bar
    for lx in (x0 + 4, x1 - 6, (x0 + x1) // 2 - 1):
        c.rect(lx, y1 + FACE_H, 3, LEG_H + 1, LEG)
        c.vline(lx, y1 + FACE_H, y1 + FACE_H + LEG_H, LEG_LIGHT)
    # top
    c.rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, TABLE)
    for y in range(y0, y1 + 1, 4):
        pass
    # soft shading: the far half a touch darker, highlight strip near the front
    c.rect(x0, y0, x1 - x0 + 1, 3, TABLE_SHADE)
    c.rect(x0, y1 - 3, x1 - x0 + 1, 2, TABLE_LIGHT)
    # white lines: edge band and centre line
    c.hline(x0 + 1, x1 - 1, y0 + 1, LINE)
    c.hline(x0 + 1, x1 - 1, y1 - 1, LINE)
    c.vline(x0 + 1, y0 + 1, y1 - 1, LINE)
    c.vline(x1 - 1, y0 + 1, y1 - 1, LINE)
    mid = (x0 + x1) // 2
    c.vline(mid, y0 + 1, y1 - 1, LINE)
    # outline
    c.hline(x0, x1, y0, OUTLINE)
    c.vline(x0, y0, y1 + FACE_H - 1, OUTLINE)
    c.vline(x1, y0, y1 + FACE_H - 1, OUTLINE)
    # front face
    c.rect(x0 + 1, y1 + 1, x1 - x0 - 1, FACE_H - 1, TABLE_FACE)
    c.hline(x0, x1, y1, LINE)
    c.hline(x0, x1, y1 + FACE_H, OUTLINE)
    # net: a shadow on the table, mesh, white tape, posts sticking out past the sides
    c.hline(x0 + 1, x1 - 1, NET_Y + 1, TABLE_SHADE)
    for y in range(NET_Y - 6, NET_Y + 1):
        for x in range(x0 - 2, x1 + 3):
            if (x + y) % 2 == 0 or y == NET_Y:
                c.set(x, y, NET)
    c.hline(x0 - 2, x1 + 2, NET_Y - 7, LINE)
    c.hline(x0 - 2, x1 + 2, NET_Y - 8, OUTLINE)
    for px in (x0 - 4, x1 + 3):
        c.rect(px, NET_Y - 8, 2, 10, NET_LIGHT)
        c.vline(px - 1, NET_Y - 9, NET_Y + 2, OUTLINE)
        c.vline(px + 2, NET_Y - 9, NET_Y + 2, OUTLINE)
        c.hline(px - 1, px + 2, NET_Y - 9, OUTLINE)
        c.hline(px - 1, px + 2, NET_Y + 2, OUTLINE)
    return c


# ---------------------------------------------------------------- sprites
BALL = rgb("#f8f8f8")
BALL_SHADE = rgb("#d0d8e8")
BALL_OUT = rgb("#403848")
SHADOW = rgb("#202838")
SHADOW_SOFT = rgb("#384058")


def make_ball():
    c = Canvas(8, 24)
    shapes = [
        ["........", "........", "...##...", "..#ww#..", "..#ws#..", "...##...", "........", "........"],
        ["........", "...##...", "..#ww#..", ".#wwws#.", ".#wwss#.", "..#ss#..", "...##...", "........"],
        ["..####..", ".#wwww#.", "#wwwwws#", "#wwwwss#", "#wwwwss#", "#wwssss#", ".#ssss#.", "..####.."],
    ]
    for i, rows in enumerate(shapes):
        c.ascii(0, i * 8, rows, {"#": BALL_OUT, "w": BALL, "s": BALL_SHADE})
    return c


def make_shadow():
    c = Canvas(8, 16)
    c.ascii(0, 0, ["........", "........", "........", "..####..", ".######.", "..####..", "........", "........"],
            {"#": SHADOW})
    c.ascii(0, 8, ["........", "........", "........", "........", "..####..", "........", "........", "........"],
            {"#": SHADOW_SOFT})
    return c


PAD = rgb("#e83838")
PAD_DARK = rgb("#a02028")
PAD_LIGHT = rgb("#f88080")
HANDLE = rgb("#c88850")
HANDLE_DARK = rgb("#885030")


def make_paddle():
    c = Canvas(16, 80)
    legend = {"o": OUTLINE, "r": PAD, "d": PAD_DARK, "l": PAD_LIGHT, "h": HANDLE, "H": HANDLE_DARK}
    # Hand position in every frame is about (5, 11).
    frames = [
        # rest: blade up and a little right
        ["................",
         "................",
         "......oooo......",
         ".....olllro.....",
         "....olrrrrdo....",
         "....orrrrrdo....",
         "....orrrrrdo....",
         "....odrrrddo....",
         ".....oddddo.....",
         "......ohho......",
         "......ohHo......",
         "......ohHo......",
         "......oooo......",
         "................",
         "................",
         "................"],
        # swing: blade level to the right
        ["................",
         "................",
         "................",
         "................",
         "..........ooo...",
         "........oolllo..",
         ".......olrrrrdo.",
         "..oooooorrrrrdo.",
         "..ohhhhHrrrrrdo.",
         "..oooooodrrrddo.",
         "........oddddo..",
         "..........ooo...",
         "................",
         "................",
         "................",
         "................"],
        # follow-through: blade down-right
        ["................",
         "................",
         "................",
         "................",
         "................",
         "......oooo......",
         "......ohHo......",
         "......ohHo......",
         ".....oohhoo.....",
         "....olllrro.....",
         "....olrrrrdo....",
         "....orrrrrdo....",
         "....odrrrddo....",
         ".....oddddo.....",
         "......oooo......",
         "................"],
        # smash wind-up: blade 45 degrees down-right
        ["................",
         "................",
         "..ooo...........",
         "..oHho..........",
         "...oHho.........",
         "....oHhoo.......",
         ".....oollroo....",
         "......olrrrdo...",
         ".....olrrrrrdo..",
         ".....orrrrrrdo..",
         ".....orrrrrrdo..",
         "......odrrrdo...",
         ".......odddo....",
         "........ooo.....",
         "................",
         "................"],
        # smash rising: blade 45 degrees up-right
        ["................",
         "................",
         "........ooo.....",
         ".......olllo....",
         "......olrrrdo...",
         ".....olrrrrrdo..",
         ".....orrrrrrdo..",
         ".....orrrrrrdo..",
         "......odrrrdo...",
         ".....oodddoo....",
         "....oHhoo.......",
         "...oHho.........",
         "..oHho..........",
         "..ooo...........",
         "................",
         "................"],
    ]
    for i, rows in enumerate(frames):
        c.ascii(0, i * 16, rows, legend)
    return c


def make_flake():
    c = Canvas(8, 16)
    c.ascii(0, 0, ["........", "........", "........", "...#....", "..###...", "...#....", "........", "........"],
            {"#": SNOW})
    c.ascii(0, 8, ["........", "........", "........", "...#....", "........", "........", "........", "........"],
            {"#": SNOW})
    return c


LIGHT_ON = rgb("#f8f080")
LIGHT_GLOW = rgb("#f8b830")


def make_light():
    c = Canvas(8, 16)
    c.ascii(0, 0, ["........", "........", "...g....", "..gwg...", "...g....", "........", "........", "........"],
            {"g": LIGHT_GLOW, "w": LIGHT_ON})
    c.ascii(0, 8, ["........", "........", "........", "...g....", "........", "........", "........", "........"],
            {"g": GOLD_DARK})
    return c


METER_PALETTE = [OUTLINE, rgb("#283040"), rgb("#58c060"), rgb("#f8d040"), rgb("#f05030"), rgb("#f8f8f8"),
                 rgb("#f8a8a8")]


def make_meter():
    """Power meter shown above you while you charge a smash: 32x8, 9 frames (empty .. full).
    The last quarter is gold: release there (with good timing) for a PERFECT shot."""
    out_, back, green, gold, red, white = METER_PALETTE[:6]
    c = Canvas(32, 72)
    for f in range(9):
        y0 = f * 8
        c.rect(0, y0 + 1, 32, 6, out_)
        c.rect(1, y0 + 2, 30, 4, back)
        fill = round(30 * f / 8)
        for x in range(fill):
            col = green if x < 22 else gold
            c.rect(1 + x, y0 + 2, 1, 4, col)
        c.vline(23, y0 + 2, y0 + 5, white)          # where "perfect" starts
        if f == 8:
            c.hline(1, 30, y0 + 2, white)
    return c


def make_vmeter():
    """The smash meter, standing up beside the paddle while you charge: 8x32, 9 frames (empty .. full),
    filling from the bottom. The top quarter is gold: release there (with good timing) for a PERFECT shot."""
    out_, back, green, gold, red, white = METER_PALETTE[:6]
    c = Canvas(8, 32 * 9)
    for f in range(9):
        y0 = f * 32
        c.rect(1, y0, 6, 32, out_)
        c.rect(2, y0 + 1, 4, 30, back)
        fill = round(30 * f / 8)
        for i in range(fill):
            col = green if i < 22 else gold
            c.rect(2, y0 + 30 - i, 4, 1, col)
        c.hline(2, 5, y0 + 30 - 22, white)          # where "perfect" starts
        if f == 8:
            c.vline(2, y0 + 1, y0 + 30, white)
    return c


def make_burst():
    """A star burst at the ball for a perfect hit: 16x16, 2 frames (big, fading)."""
    out_, back, green, gold, red, white, pink = METER_PALETTE
    c = Canvas(16, 32)
    for f, r in ((0, 7), (1, 5)):
        cx, cy = 8, f * 16 + 8
        for i in range(8):
            dx, dy = [(1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)][i]
            ln = r if i % 2 == 0 else r - 2
            for k in range(2, ln + 1):
                c.set(cx + dx * k, cy + dy * k, gold if k < ln - 1 else (white if f == 0 else pink))
        c.rect(cx - 1, cy - 1, 3, 3, white)
    return c


def make_serve_bar():
    """The serve bar is 64 px tall, made of two 8x32 pieces: frame 0 = bottom half, frame 1 = top half."""
    out_, back = METER_PALETTE[0], METER_PALETTE[1]
    c = Canvas(8, 64)
    for f in range(2):
        y0 = f * 32
        c.rect(1, y0, 6, 32, out_)
        c.rect(2, y0 + (0 if f == 0 else 1), 4, 31, back)
    return c


# Serve zones per difficulty (pixels from the middle of the target): perfect, strong, ok. Keep in sync
# with serve_perfect / serve_strong / serve_ok in LEVELS (pingpong.cpp).
SERVE_ZONES = [(2, 8, 14), (1, 7, 12), (1, 5, 10)]


def make_serve_zones():
    """8x32 frames (Easy, Medium, Hard): the target on the serve bar, centred on row 16:
    red box = perfect, green = strong, gold = ok (anything else is weak)."""
    out_, back, green, gold, red, white = METER_PALETTE[:6]
    c = Canvas(8, 96)
    for f, (perfect, strong, ok) in enumerate(SERVE_ZONES):
        y0 = f * 32
        for d in range(0, 16):
            col = red if d <= perfect else green if d <= strong else gold if d <= ok else None
            if col is None:
                continue
            for y in (y0 + 16 - d, y0 + 16 + d):
                if 0 <= y - y0 < 32:
                    c.rect(2, y, 4, 1, col)
        # the red box sticks out of the bar a little, with white corners, so it's easy to see
        c.rect(1, y0 + 16 - perfect, 6, 2 * perfect + 1, red)
        for y in (y0 + 16 - perfect - 1, y0 + 16 + perfect + 1):
            c.rect(1, y, 6, 1, white)
    return c


def make_serve_marks():
    """8x8: the marker that climbs the bar."""
    white = METER_PALETTE[5]
    c = Canvas(8, 8)
    c.rect(0, 3, 8, 2, white)
    c.set(0, 2, white); c.set(0, 5, white); c.set(7, 2, white); c.set(7, 5, white)
    return c


def save_sprite(name, canvas, height, palette=None):
    canvas.save_bmp(os.path.join(OUT, name + ".bmp"), palette=palette)
    write_json(os.path.join(OUT, name + ".json"), '{\n    "type": "sprite",\n    "height": %d\n}' % height)


def save_bg(name, canvas):
    canvas.save_bmp(os.path.join(OUT, name + ".bmp"), bpp=8)
    write_json(os.path.join(OUT, name + ".json"),
               '{\n    "type": "regular_bg",\n    "bpp_mode": "bpp_4_auto"\n}')


def count_tiles(c):
    tiles = set()
    for ty in range(0, c.h, 8):
        for tx in range(0, c.w, 8):
            tiles.add(tuple(c.px[(ty + y) * c.w + tx + x] for y in range(8) for x in range(8)))
    return len(tiles)


def main():
    os.makedirs(OUT, exist_ok=True)
    hall = draw_hall()
    table = draw_table()
    save_bg("pingpong_hall", hall)
    save_bg("pingpong_table", table)
    sprites = {
        "pingpong_ball": (make_ball(), 8),
        "pingpong_shadow": (make_shadow(), 8),
        "pingpong_paddle": (make_paddle(), 16),
        "pingpong_flake": (make_flake(), 8),
        "pingpong_light": (make_light(), 8),
    }
    shared = []
    for canvas, _ in sprites.values():
        for col in canvas.colors():
            if col not in shared:
                shared.append(col)
    assert len(shared) <= 15, f"the small ping pong sprites use {len(shared)} colours; they must share 15"
    for name, (canvas, h) in sprites.items():
        save_sprite(name, canvas, h, shared)
    # The meter and the burst share one palette, so together they use one of the 16 sprite palettes.
    save_sprite("pingpong_meter", make_meter(), 8, METER_PALETTE)
    save_sprite("pingpong_vmeter", make_vmeter(), 32, METER_PALETTE)
    save_sprite("pingpong_burst", make_burst(), 16, METER_PALETTE)
    save_sprite("pingpong_vbar", make_serve_bar(), 32, METER_PALETTE)
    save_sprite("pingpong_vzone", make_serve_zones(), 32, METER_PALETTE)
    save_sprite("pingpong_vmark", make_serve_marks(), 8, METER_PALETTE)
    print("hall: %d colours, %d unique tiles" % (len(hall.colors()), count_tiles(hall)))
    print("table: %d colours, %d unique tiles" % (len(table.colors()), count_tiles(table)))

    if len(sys.argv) > 1:
        d = sys.argv[1]
        os.makedirs(d, exist_ok=True)
        combo = Canvas(240, 160)
        combo.blit(hall, 0, 0)
        combo.blit(table, 0, 0)
        save_png(os.path.join(d, "pp_scene.png"), combo, scale=3)
        sheet = Canvas(80, 48)
        x = 0
        for name, (canvas, h) in sprites.items():
            sheet.blit(canvas, x, 0)
            x += canvas.w + 2
        save_png(os.path.join(d, "pp_sprites.png"), sheet, scale=6)
    print("Wrote Ping Pong Pals art to", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
