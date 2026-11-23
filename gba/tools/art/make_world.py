"""Generates the town map: gba/graphics/world/world_map.bmp (512x512) and include/sb_world_layout.h.

The town is a 32x32 grid of 16x16 cells. build_layout() decides what goes in each cell; paint()
draws it. The layout header tells the game which cells are walkable and where the six places are.

Layout characters:
  .  snow (walk)        :  path (walk)          T  pine tree        ~  frozen pond
  #  fence              o  rock / bush          B  building/object (blocked, drawn by a stamp)
  1-6  doorstep of a place (walk; see sb_games.h: 1 flappy, 2 ping pong, 3 sled, 4 catch,
       5 fishing, 6 horse)
  N  a family member stands here (walk tile, blocked while they stand on it)
  P  where you start
To move things around, edit build_layout() and STAMPS, then run:
    python3 tools/art/make_world.py
"""
import os
import sys
from canvas import Canvas, rgb, write_json, save_png

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
OUT = os.path.join(ROOT, "graphics", "world")
N = 32
CELL = 16

# ----------------------------------------------------------------------------- palette
OUTL = rgb("#283048")
SNOW, SNOW_S, SNOW_D, SPARK = rgb("#f0f4fc"), rgb("#d8e0f0"), rgb("#b8c8e8"), rgb("#ffffff")
PATH, PATH_D, PATH_L = rgb("#d0b898"), rgb("#a88868"), rgb("#e8d8c0")
TREE_D, TREE_M, TREE_L, TRUNK, TRUNK_D = rgb("#205838"), rgb("#307848"), rgb("#48a060"), rgb("#805030"), rgb("#583820")
ICE, ICE_L, ICE_D, ICE_DD = rgb("#a8d8f0"), rgb("#d8f0f8"), rgb("#80b8e0"), rgb("#5890c8")
WOOD, WOOD_D, WOOD_L = rgb("#b87840"), rgb("#784820"), rgb("#d8a060")
ROCK, ROCK_D = rgb("#98a0b0"), rgb("#687080")
RED, RED_D, RED_L = rgb("#d84040"), rgb("#982830"), rgb("#f07070")
BLUE, BLUE_D = rgb("#4878d0"), rgb("#304890")
GREEN, GREEN_D = rgb("#40a050"), rgb("#287038")
WALL, WALL_D = rgb("#f0e0c0"), rgb("#c8b090")
WIN, WIN_D = rgb("#f8e070"), rgb("#d8a030")
GOLD, PINK, PURPLE = rgb("#f8d040"), rgb("#f080b0"), rgb("#9060c8")
BROWN, BROWN_L = rgb("#8a5a3a"), rgb("#b88060")
CARROT = rgb("#f08030")
BLACK = rgb("#303038")


def build_layout():
    g = [["." for _ in range(N)] for _ in range(N)]

    def fill(x0, y0, x1, y1, ch):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                g[y][x] = ch

    # Forest border.
    fill(0, 0, N - 1, 1, "T")
    fill(0, N - 3, N - 1, N - 1, "T")
    fill(0, 0, 1, N - 1, "T")
    fill(N - 2, 0, N - 1, N - 1, "T")
    for x, y in ((2, 2), (3, 2), (2, 3), (11, 2), (12, 2), (20, 2), (2, 9), (2, 10), (29, 9), (29, 10), (2, 20),
                 (29, 19), (29, 20), (2, 28), (3, 28), (29, 28), (28, 28), (19, 2), (10, 8), (21, 9), (11, 19),
                 (20, 19), (14, 28), (15, 28), (13, 28)):
        g[y][x] = "T"

    # Main paths: a ring road and a cross through the square.
    fill(4, 17, 27, 17, ":")       # east-west road
    fill(15, 6, 16, 27, ":")       # north-south avenue (2 wide)
    fill(5, 7, 26, 7, ":")         # northern lane
    fill(5, 7, 5, 17, ":")
    fill(26, 7, 26, 17, ":")
    fill(9, 17, 9, 21, ":")        # to the pond
    fill(16, 17, 24, 17, ":")
    fill(22, 20, 27, 20, ":")      # to the houses
    fill(22, 17, 22, 20, ":")
    # Town square around the big tree.
    fill(12, 10, 19, 16, ":")

    # Places (footprints are drawn by STAMPS below).
    fill(3, 3, 6, 5, "B")          # Reindeer Roost barn
    fill(7, 4, 8, 5, "B")          # reindeer + sleigh
    g[6][5] = "1"
    fill(13, 3, 18, 5, "B")        # Toy Workshop
    g[6][15] = "4"
    fill(22, 2, 28, 6, "B")        # Big Hill
    g[7][25] = "3"
    fill(3, 12, 6, 14, "B")        # Stable
    g[15][5] = "6"
    fill(7, 12, 10, 15, "#")       # paddock fence
    fill(8, 13, 9, 14, "B")        # horse in the paddock
    fill(23, 12, 27, 14, "B")      # Rec Hall
    g[15][25] = "2"
    for y in range(22, 28):        # Frozen Pond
        for x in range(5, 14):
            dx, dy = (x - 9) / 4.6, (y - 24.6) / 3.0
            if dx * dx + dy * dy <= 1.0:
                g[y][x] = "~"
    g[21][9] = "5"
    fill(15, 11, 16, 13, "B")      # Christmas tree

    # Homes, lamps, decorations.
    fill(20, 22, 22, 24, "B")
    fill(25, 22, 27, 24, "B")
    fill(23, 26, 25, 28, "B")
    for x, y in ((14, 9), (17, 9), (14, 16), (17, 16), (6, 18), (25, 18)):
        g[y][x] = "B"              # lamp posts
    for x, y in ((12, 13), (19, 13)):
        g[y][x] = "B"              # benches
    for x, y in ((11, 15), (19, 21), (8, 9)):
        g[y][x] = "B"              # snowmen
    for x, y in ((17, 4 + 2), (13, 6)):
        pass
    for x, y in ((3, 7), (28, 9), (3, 22), (28, 25), (11, 24), (20, 11), (11, 11), (18, 26)):
        if g[y][x] == ".":
            g[y][x] = "o"
    fill(11, 6, 11, 6, "B")         # present pile by the workshop
    fill(19, 6, 19, 6, "B")

    # People and start.
    for x, y in ((7, 8), (20, 8), (24, 9), (4, 16), (13, 15), (18, 12), (11, 21), (24, 19), (20, 25), (7, 19)):
        g[y][x] = "N"
    g[18][15] = "P"
    return g


STAMPS = []  # (kind, cell_x, cell_y) filled in paint()


# ----------------------------------------------------------------------------- painters
def cell_hash(x, y):
    return (x * 73856093 ^ y * 19349663) & 0xFFFF


def paint_snow(c, x, y):
    px, py = x * CELL, y * CELL
    c.rect(px, py, CELL, CELL, SNOW)
    h = cell_hash(x, y) % 6
    if h == 0:
        c.ascii(px + 3, py + 4, ["..s..", ".sSs."], {"s": SNOW_S, "S": SNOW_D})
    elif h == 1:
        c.set(px + 10, py + 9, SPARK)
        c.set(px + 4, py + 12, SNOW_S)
        c.set(px + 5, py + 12, SNOW_S)
    elif h == 2:
        c.ascii(px + 8, py + 2, ["SsS"], {"s": SNOW_S, "S": SNOW_D})


def is_path(g, x, y):
    return 0 <= x < N and 0 <= y < N and g[y][x] in ":123456P"


def paint_path(c, g, x, y):
    px, py = x * CELL, y * CELL
    c.rect(px, py, CELL, CELL, PATH)
    # cobbles
    for (sx, sy) in ((1, 1), (9, 1), (5, 6), (13, 6), (1, 11), (9, 11)):
        c.rect(px + sx, py + sy, 5, 4, PATH_L)
        c.hline(px + sx, px + sx + 4, py + sy + 4, PATH_D)
        c.vline(px + sx + 5, py + sy, py + sy + 4, PATH_D)
    # snowy edges where the path meets snow
    if not is_path(g, x, y - 1):
        c.rect(px, py, CELL, 2, SNOW)
        c.hline(px, px + CELL - 1, py + 2, SNOW_S)
    if not is_path(g, x, y + 1):
        c.rect(px, py + CELL - 2, CELL, 2, SNOW)
        c.hline(px, px + CELL - 1, py + CELL - 3, SNOW_S)
    if not is_path(g, x - 1, y):
        c.rect(px, py, 2, CELL, SNOW)
        c.vline(px + 2, py, py + CELL - 1, SNOW_S)
    if not is_path(g, x + 1, y):
        c.rect(px + CELL - 2, py, 2, CELL, SNOW)
        c.vline(px + CELL - 3, py, py + CELL - 1, SNOW_S)


def paint_doorstep(c, x, y):
    px, py = x * CELL, y * CELL
    c.rect(px + 2, py + 3, 12, 10, RED_D)
    c.rect(px + 3, py + 4, 10, 8, RED)
    c.hline(px + 4, px + 11, py + 6, GOLD)
    c.hline(px + 4, px + 11, py + 9, GOLD)


def paint_tree(c, x, y, top=0):
    """Pine tree in a cell; its crown pokes 8px into the cell above."""
    px, py = x * CELL, y * CELL - 8 + top
    rows = [
        ".......o........",
        "......oLo.......",
        ".....oLwLo......",
        ".....oMLMo......",
        "....oMwMMMo.....",
        "....oMMLMMo.....",
        "...oMMwwMMMo....",
        "...oMMMMMwMo....",
        "..owwMMMMMMDo...",
        "..oMMMwMMMMDo...",
        ".oMMMMMMwwMDDo..",
        ".oMwwMMMMMMDDo..",
        "oMMMMMMMMMMMDDo.",
        "oDDMMMMMMMMDDDo.",
        ".ooDDDDDDDDDDo..",
        "...ooooooooo....",
        "......oTTo......",
        "......oTdo......",
        "......oood......",
    ]
    rows = [r.ljust(16, ".") for r in rows]
    shifted = [r[15:] + r[:15] for r in rows]  # centre in the cell
    c.ascii(px, py, shifted, {"o": OUTL, "L": TREE_L, "M": TREE_M, "D": TREE_D, "w": SNOW, "T": TRUNK,
                              "d": TRUNK_D})
    c.ascii(px, py + 18, [".....SSSSS......".ljust(16, ".")[15:] + ".....SSSSS......"[:15]], {"S": SNOW_S})


def paint_ice(c, g, x, y):
    px, py = x * CELL, y * CELL
    c.rect(px, py, CELL, CELL, ICE)
    if (x + y) % 3 == 0:
        c.hline(px + 3, px + 7, py + 5, ICE_L)
        c.hline(px + 8, px + 10, py + 6, ICE_L)
    if (x * 3 + y) % 4 == 1:
        c.ascii(px + 6, py + 9, ["d..", ".dd", "...d"], {"d": ICE_D})

    def ice(xx, yy):
        return 0 <= xx < N and 0 <= yy < N and g[yy][xx] == "~"

    if not ice(x, y - 1):
        c.rect(px, py, CELL, 3, SNOW)
        c.hline(px, px + CELL - 1, py + 3, ICE_DD)
    if not ice(x, y + 1):
        c.rect(px, py + CELL - 3, CELL, 3, SNOW)
        c.hline(px, px + CELL - 1, py + CELL - 4, ICE_L)
    if not ice(x - 1, y):
        c.rect(px, py, 3, CELL, SNOW)
        c.vline(px + 3, py, py + CELL - 1, ICE_DD)
    if not ice(x + 1, y):
        c.rect(px + CELL - 3, py, 3, CELL, SNOW)
        c.vline(px + CELL - 4, py, py + CELL - 1, ICE_DD)


def paint_fence(c, g, x, y):
    px, py = x * CELL, y * CELL

    def fence(xx, yy):
        return 0 <= xx < N and 0 <= yy < N and g[yy][xx] == "#"

    horiz = fence(x - 1, y) or fence(x + 1, y)
    vert = fence(x, y - 1) or fence(x, y + 1)
    if horiz:
        for yy in (5, 10):
            c.rect(px, py + yy, CELL, 2, WOOD)
            c.hline(px, px + CELL - 1, py + yy + 2, WOOD_D)
        c.hline(px, px + CELL - 1, py + 4, SNOW)
    if vert:
        c.rect(px + 6, py, 4, CELL, WOOD)
        c.vline(px + 10, py, py + CELL - 1, WOOD_D)
    c.rect(px + 6, py + 2, 4, 12, WOOD_L)
    c.rect(px + 6, py + 2, 4, 2, SNOW)
    c.outline_color(WOOD_L, OUTL) if False else None
    c.vline(px + 5, py + 2, py + 13, OUTL)
    c.vline(px + 10, py + 2, py + 13, OUTL)
    c.hline(px + 6, px + 9, py + 14, OUTL)


def paint_rock(c, x, y):
    px, py = x * CELL, y * CELL
    c.ascii(px, py + 2, [
        "................",
        "......oooo......",
        "....ooSSSSoo....",
        "...oSSSSSSSSo...",
        "..oSwSSSSSSSSo..",
        "..oRRSSSSSSRRo..",
        ".oRRRRRRRRRRRDo.",
        ".oRRRRRRRRRDDDo.",
        ".oDRRRRRRRDDDDo.",
        "..oDDDDDDDDDDo..",
        "...oooooooooo...",
        "....ssssssss....",
    ], {"o": OUTL, "S": SNOW, "w": SPARK, "R": ROCK, "D": ROCK_D, "s": SNOW_S})


def paint_lamp(c, x, y):
    px, py = x * CELL, y * CELL - 12
    c.ascii(px, py, [
        "......oooo......",
        ".....oYYYYo.....",
        ".....oYwYYo.....",
        ".....oYYYYo.....",
        "......oooo......",
        ".......kk.......",
        ".......kk.......",
        "......rkkr......",
        ".......kk.......",
        ".......kk.......",
        ".......kk.......",
        ".......kk.......",
        ".......kk.......",
        ".......kk.......",
        ".......kk.......",
        ".......kk.......",
        ".......kk.......",
        "......okkoo.....",
        ".....okkkkko....",
        "......SSSSS.....",
    ], {"o": OUTL, "Y": WIN, "w": SPARK, "k": BLACK, "r": RED, "S": SNOW_S})
    c.rect(px + 6, py + 7, 4, 1, GREEN)


def paint_bench(c, x, y):
    px, py = x * CELL, y * CELL
    c.ascii(px, py + 3, [
        "oooooooooooooooo",
        "oSSSSSSSSSSSSSSo",
        "oWWWWWWWWWWWWWWo",
        "oDDDDDDDDDDDDDDo",
        "oWWWWWWWWWWWWWWo",
        "oDDDDDDDDDDDDDDo",
        ".ok..........ko.",
        ".ok..........ko.",
        ".oo..........oo.",
    ], {"o": OUTL, "S": SNOW, "W": WOOD, "D": WOOD_D, "k": BLACK})


def paint_snowman(c, x, y):
    px, py = x * CELL, y * CELL - 10
    c.ascii(px, py, [
        "......oooo......",
        ".....okkkko.....",
        "....oookkooo....",
        ".....owwwwo.....",
        "....owkwwkwo....",
        "....owwccwwo....",
        ".....owwwwo.....",
        "....orrrrrro....",
        "...owwwwwwwwo...",
        "..owwwwkwwwwwo..",
        "..owwwwwwwwwso..",
        "..owwwwkwwwsso..",
        "...owwwwwwsso...",
        "..owwwwwwwwwso..",
        ".owwwwwwwwwwsso.",
        ".owwwwwwwwwwsso.",
        ".owwwwwwwwwssso.",
        "..owwwwwwwssso..",
        "...ooooooooooo..",
        "....SSSSSSSSS...",
    ], {"o": OUTL, "k": BLACK, "w": SPARK, "c": CARROT, "r": RED, "s": SNOW_S, "S": SNOW_D})


def paint_presents(c, x, y):
    px, py = x * CELL, y * CELL
    def box(bx, by, w, h, col, ribbon):
        c.rect(px + bx, py + by, w, h, OUTL)
        c.rect(px + bx + 1, py + by + 1, w - 2, h - 2, col)
        c.vline(px + bx + w // 2, py + by + 1, py + by + h - 2, ribbon)
        c.hline(px + bx + 1, px + bx + w - 2, py + by + h // 2, ribbon)
    box(0, 7, 9, 9, RED, GOLD)
    box(7, 9, 9, 7, BLUE, SNOW)
    box(4, 1, 8, 8, GREEN, RED)
    c.ascii(px + 6, py, ["o.o", ".o."], {"o": RED})


def paint_house(c, cx, cy, roof, roof_d, wall=WALL, wall_d=WALL_D, w=3, h=3, door=True, sign=None):
    """A cottage occupying w x h cells, roof with snow, door at the bottom middle."""
    px, py, pw, ph = cx * CELL, cy * CELL - 8, w * CELL, h * CELL + 8
    roof_h = ph // 2 + 2
    # roof
    for i in range(roof_h):
        inset = max(0, 3 - i // 2)
        c.hline(px + inset, px + pw - 1 - inset, py + i, roof if i % 4 != 3 else roof_d)
    c.rect(px + 2, py, pw - 4, 4, SNOW)
    c.hline(px + 2, px + pw - 3, py + 4, SNOW_S)
    for i in range(0, pw - 6, 5):
        c.set(px + 4 + i, py + 5, SNOW)
        c.set(px + 5 + i, py + 5, SNOW)
        c.set(px + 4 + i, py + 6, SNOW_S)
    c.hline(px, px + pw - 1, py + roof_h - 1, roof_d)
    # chimney
    c.rect(px + pw - 14, py - 6, 6, 8, RED_D if roof != RED else WALL_D)
    c.rect(px + pw - 14, py - 7, 6, 2, SNOW)
    # walls
    wy = py + roof_h
    c.rect(px + 2, wy, pw - 4, ph - roof_h, wall)
    c.rect(px + 2, py + ph - 3, pw - 4, 3, wall_d)
    # windows (warm light)
    for wx in (px + 6, px + pw - 16):
        c.rect(wx, wy + 4, 10, 8, OUTL)
        c.rect(wx + 1, wy + 5, 8, 6, WIN)
        c.hline(wx + 1, wx + 8, wy + 8, WIN_D)
        c.vline(wx + 5, wy + 5, wy + 10, WIN_D)
        c.rect(wx, wy + 12, 10, 2, SNOW)
    if door:
        dx = px + pw // 2 - 5
        c.rect(dx, py + ph - 15, 10, 15, OUTL)
        c.rect(dx + 1, py + ph - 14, 8, 14, BROWN)
        c.vline(dx + 5, py + ph - 14, py + ph - 1, BROWN_L)
        c.set(dx + 7, py + ph - 8, GOLD)
        # wreath
        c.ascii(dx + 2, py + ph - 13, [".gg.", "g..g", "grrg", ".gg."], {"g": GREEN, "r": RED})
    # outline around the whole building
    c.rect(px, py + roof_h - 1, 2, ph - roof_h + 1, OUTL)
    c.rect(px + pw - 2, py + roof_h - 1, 2, ph - roof_h + 1, OUTL)
    c.hline(px, px + pw - 1, py + ph - 1, OUTL)
    if sign:
        sign(c, px, py, pw, ph)


def paint_lights(c, px, py, w):
    cols = [RED, GOLD, GREEN, BLUE]
    for i in range(0, w, 4):
        c.set(px + i, py + (1 if (i // 4) % 2 else 0), OUTL)
        c.set(px + i + 2, py + 2, cols[(i // 4) % 4])


def paint_roost(c):
    paint_house(c, 3, 3, WOOD, WOOD_D, wall=rgb("#c89060"), wall_d=WOOD_D, w=4, h=3)
    # reindeer
    px, py = 7 * CELL, 4 * CELL - 6
    c.ascii(px, py, [
        "...d.d..........",
        "...dd.d.........",
        "....dd..........",
        "....oBBo........",
        "...oBBkBo.......",
        "...oBBBBBo......",
        "...rBBo.oBBBBBBo",
        "....oo..oBBBBBBBo",
        ".........oBBBBBBo",
        ".........oBLBBLBo",
        ".........oB.oB.oB",
        ".........oB..B..B",
        ".........ok..k..k",
    ], {"d": BROWN_L, "o": OUTL, "B": BROWN, "k": BLACK, "r": RED, "L": BROWN_L})
    # sleigh
    px, py = 7 * CELL + 2, 5 * CELL + 2
    c.ascii(px, py, [
        "o..........oo...",
        "orrrrrrrrrrrro..",
        "orrGrrrrrrGrro..",
        ".orrrrrrrrrrro..",
        "..oooooooooooo..",
        ".oYYYYYYYYYYYYo.",
    ], {"o": OUTL, "r": RED, "G": GOLD, "Y": GOLD})


def paint_workshop(c):
    def sign(c, px, py, pw, ph):
        # candy cane pillars + big present sign
        for x0 in (px + 2, px + pw - 6):
            for i in range(ph // 2):
                c.rect(x0, py + ph // 2 + i - 2, 4, 1, RED if (i // 2) % 2 else SNOW)
        c.rect(px + pw // 2 - 8, py + 8, 16, 12, OUTL)
        c.rect(px + pw // 2 - 7, py + 9, 14, 10, GREEN)
        c.vline(px + pw // 2, py + 9, py + 18, GOLD)
        c.hline(px + pw // 2 - 7, px + pw // 2 + 6, py + 13, GOLD)
        paint_lights(c, px + 4, py + ph // 2 - 1, pw - 8)
    paint_house(c, 13, 3, GREEN, GREEN_D, wall=rgb("#f8d8b0"), w=6, h=3, sign=sign)


def paint_rec_hall(c):
    def sign(c, px, py, pw, ph):
        # ping pong paddle sign
        c.ellipse(px + pw // 2, py + 14, 6, 6, RED, OUTL)
        c.rect(px + pw // 2 - 1, py + 19, 3, 5, WOOD_D)
        c.set(px + pw // 2 + 6, py + 9, SPARK)
        paint_lights(c, px + 4, py + ph // 2 - 1, pw - 8)
    paint_house(c, 23, 12, BLUE, BLUE_D, wall=rgb("#e8e0f0"), w=5, h=3, sign=sign)


def paint_stable(c):
    def sign(c, px, py, pw, ph):
        # big barn doors with white X
        dx, dy = px + pw // 2 - 10, py + ph - 20
        c.rect(dx, dy, 20, 20, OUTL)
        c.rect(dx + 1, dy + 1, 18, 19, RED_D)
        for i in range(18):
            c.set(dx + 1 + i, dy + 1 + i, SNOW)
            c.set(dx + 18 - i, dy + 1 + i, SNOW)
        c.vline(dx + 10, dy + 1, dy + 19, OUTL)
    paint_house(c, 3, 12, RED_D, OUTL, wall=RED, wall_d=RED_D, w=4, h=3, door=False, sign=sign)
    # horse in the paddock
    px, py = 8 * CELL, 13 * CELL + 2
    c.ascii(px, py, [
        "..............o.",
        ".............oBo",
        "............oBkBo",
        "...........oMBBBo",
        "..oooooooooMBBo.",
        ".oBBBBBBBBBBBo..",
        "oMBBBBBBBBBBBo..",
        "oMBBBBBBBBBBo...",
        ".oBBBBBBBBBBo...",
        ".oBo.oBo.oBo....",
        ".oBo.oBo.oBo....",
        ".oko.oko.oko....",
    ], {"o": OUTL, "B": BROWN_L, "k": BLACK, "M": WOOD_D})


def paint_hill(c):
    x0, y0, x1, y1 = 22 * CELL, 2 * CELL - 4, 29 * CELL - 1, 7 * CELL - 1
    cx, w = (x0 + x1) // 2, (x1 - x0) // 2
    for y in range(y0, y1 + 1):
        t = (y - y0) / (y1 - y0)
        half = int(w * (0.35 + 0.65 * t ** 0.6))
        for x in range(cx - half, cx + half + 1):
            d = (x - (cx - half)) / max(1, 2 * half)
            col = SNOW if d < 0.55 else SNOW_S if d < 0.8 else SNOW_D
            c.set(x, y, col)
        c.set(cx - half - 1, y, OUTL)
        c.set(cx + half + 1, y, OUTL)
    c.hline(cx - int(w * 0.35), cx + int(w * 0.35), y0 - 1, OUTL)
    # sled tracks down the slope
    for y in range(y0 + 10, y1 - 2, 2):
        t = (y - y0) / (y1 - y0)
        c.set(cx - 4 + int(6 * t), y, SNOW_D)
        c.set(cx + 2 + int(6 * t), y, SNOW_D)
    # flag on top
    c.vline(cx, y0 - 14, y0 + 2, OUTL)
    c.rect(cx + 1, y0 - 14, 8, 5, RED)
    c.hline(cx + 1, cx + 8, y0 - 15, OUTL)
    c.hline(cx + 1, cx + 8, y0 - 9, OUTL)
    # small pines on the hill
    for tx, ty in ((cx - 30, y0 + 40), (cx + 34, y0 + 46)):
        c.ascii(tx, ty, ["..o..", ".oMo.", ".oMo.", "oMMMo", "oMMDo", ".ooo.", "..T.."],
                {"o": OUTL, "M": TREE_M, "D": TREE_D, "T": TRUNK})
    # a red sled waiting at the bottom
    sx, sy = cx - 8, y1 - 10
    c.ascii(sx, sy, ["oooooooooooo..", "oRRRRRRRRRRRo.", "oRRRRRRRRRRRo.", ".oooooooooooo.",
                     "oY.........Yo.", ".oYYYYYYYYYY.."], {"o": OUTL, "R": RED, "Y": GOLD})


def paint_pond_extras(c):
    # fishing hole, stool and a little sign on the north shore of the pond
    px, py = 9 * CELL, 23 * CELL
    c.ellipse(px + 8, py + 6, 5, 3, ICE_DD, OUTL)
    c.ellipse(px + 8, py + 6, 3, 1.6, rgb("#285890"))
    c.ascii(px + 15, py - 2, [".oooo.", "oWWWWo", ".oDDo.", ".o..o."], {"o": OUTL, "W": WOOD, "D": WOOD_D})


def paint_christmas_tree(c):
    px, py = 15 * CELL, 11 * CELL - 14
    w = 32
    for i in range(48):
        half = 2 + i * 13 // 48
        if i % 12 > 9:
            half -= 2
        col = TREE_M if (i // 3) % 3 else TREE_D
        c.hline(px + 16 - half, px + 15 + half, py + 12 + i, col)
        c.set(px + 15 - half, py + 12 + i, OUTL)
        c.set(px + 16 + half, py + 12 + i, OUTL)
    c.rect(px + 13, py + 60, 6, 6, TRUNK)
    c.rect(px + 10, py + 64, 12, 3, RED)
    # star
    c.ascii(px + 11, py + 2, ["....o....", "...oGo...", "ooGGGGGoo", ".oGGwGGo.", "..oGGGo..", ".oGo.oGo.",
                              ".oo...oo."], {"o": OUTL, "G": GOLD, "w": SPARK})
    # ornaments + garland
    cols = [RED, GOLD, BLUE, PINK, SPARK, PURPLE]
    k = 0
    for i in range(16, 56, 6):
        half = 2 + (i - 12) * 13 // 48
        for x in range(px + 16 - half + 2, px + 15 + half - 1, 5):
            c.set(x + (i // 6) % 3, py + i, cols[k % len(cols)])
            k += 1
    for i in range(10):
        c.set(px + 9 + i * 2, py + 30 + (i % 3), WIN)
        c.set(px + 7 + i * 2, py + 46 + (i % 3), WIN)
    # presents under the tree
    for bx, col in ((px + 2, RED), (px + 22, BLUE)):
        c.rect(bx, py + 58, 8, 7, OUTL)
        c.rect(bx + 1, py + 59, 6, 5, col)
        c.vline(bx + 4, py + 59, py + 63, GOLD)


def paint(g):
    c = Canvas(N * CELL, N * CELL)
    # 1. ground
    for y in range(N):
        for x in range(N):
            ch = g[y][x]
            if ch in ":123456P":
                paint_path(c, g, x, y)
            elif ch == "~":
                paint_ice(c, g, x, y)
            else:
                paint_snow(c, x, y)
    for y in range(N):
        for x in range(N):
            if g[y][x] in "123456":
                paint_doorstep(c, x, y)
    # 2. objects, back to front (top rows first so lower things overlap upper ones)
    paint_hill(c)
    for y in range(N):
        for x in range(N):
            ch = g[y][x]
            if ch == "#":
                paint_fence(c, g, x, y)
            elif ch == "o":
                paint_rock(c, x, y)
    paint_roost(c)
    paint_workshop(c)
    paint_stable(c)
    paint_rec_hall(c)
    paint_pond_extras(c)
    paint_house(c, 20, 22, RED, RED_D)
    paint_house(c, 25, 22, PURPLE, rgb("#603890"), wall=rgb("#f8e8d8"))
    paint_house(c, 23, 26, BLUE, BLUE_D, wall=rgb("#e8f0f8"))
    paint_christmas_tree(c)
    for x, y in ((14, 9), (17, 9), (14, 16), (17, 16), (6, 18), (25, 18)):
        paint_lamp(c, x, y)
    for x, y in ((12, 13), (19, 13)):
        paint_bench(c, x, y)
    for x, y in ((11, 15), (19, 21), (8, 9)):
        paint_snowman(c, x, y)
    for x, y in ((11, 6), (19, 6)):
        paint_presents(c, x, y)
    for y in range(N):
        for x in range(N):
            if g[y][x] == "T":
                paint_tree(c, x, y)
    return c


def count_tiles(c):
    seen = set()
    for ty in range(c.h // 8):
        for tx in range(c.w // 8):
            t = tuple(c.get(tx * 8 + x, ty * 8 + y) for y in range(8) for x in range(8))
            flips = [t, tuple(t[y * 8 + 7 - x] for y in range(8) for x in range(8)),
                     tuple(t[(7 - y) * 8 + x] for y in range(8) for x in range(8)),
                     tuple(t[(7 - y) * 8 + 7 - x] for y in range(8) for x in range(8))]
            seen.add(min(flips, key=lambda v: tuple(str(p) for p in v)))
    return len(seen)


def write_layout_header(g):
    lines = ["// Generated by tools/art/make_world.py: the town map layout (32x32 cells of 16x16 px).",
             "// See that script for what each character means.",
             "#ifndef SB_WORLD_LAYOUT_H", "#define SB_WORLD_LAYOUT_H", "", "namespace sb", "{",
             "    constexpr int world_columns = 32;", "    constexpr int world_rows = 32;", "",
             "    constexpr const char* world_layout[world_rows] = {"]
    for row in g:
        lines.append(f'        "{"".join(row)}",')
    lines += ["    };", "}", "", "#endif", ""]
    with open(os.path.join(ROOT, "include", "sb_world_layout.h"), "w") as f:
        f.write("\n".join(lines))


def main():
    os.makedirs(OUT, exist_ok=True)
    g = build_layout()
    c = paint(g)
    # Transparent pixels would show the backdrop: fill them with snow.
    c.px = [SNOW if p is None else p for p in c.px]
    colours = len(c.colors())
    tiles = count_tiles(c)
    print(f"world: {colours} colours, ~{tiles} unique 8x8 tiles (limit 1024)")
    c.save_bmp(os.path.join(OUT, "world_map.bmp"), bpp=8)
    write_json(os.path.join(OUT, "world_map.json"), '{\n    "type": "regular_bg",\n    "bpp_mode": "bpp_4_auto"\n}')
    write_layout_header(g)
    if len(sys.argv) > 1:
        save_png(sys.argv[1], c, scale=1)


if __name__ == "__main__":
    main()
