"""Generates the Gift Tower (was Gift Catch) art in gba/graphics/catch/.

    python3 tools/art/make_catch.py [PREVIEW_DIR]   (PREVIEW_DIR: also write scaled PNG previews there)

Files (all names start with catch_ because Butano asset names are global):
  catch_bg       256x256 background: toy workshop street at dusk (screen = pixels 8..247 x 48..207)
  catch_items    16x16 sprites, frames: 0-2 small presents, 3-4 big presents, 5-6 star present
                 (two sparkle frames), 7-8 coal (two grumpy frames)
  catch_basket   32x16 wicker basket held over the player's head, frames: 0 empty, 1 a few gifts, 2 full
                 (not used since Gift Tower: the presents stack in your arms)
  catch_sleigh   64x32 Santa's sleigh + reindeer, facing right, 2 gallop frames
  catch_parked   32x32 Santa's sleigh parked in the street (where you deliver), 0 normal, 1 sack glowing
  catch_popup    32x16 floating score numbers: +10 +20 +25 +30 +50 +75 +100 +150, +heart, -heart
                 (not used since Gift Tower: the scores are text)
"""
import os
import sys
from canvas import Canvas, rgb, write_json, save_png

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
OUT = os.path.join(ROOT, "graphics", "catch")

OUTLINE = rgb("#282028")
WHITE = rgb("#f8f8f8")


def outline(c, col=OUTLINE):
    """1px outline around everything that isn't transparent (4-neighbours)."""
    marks = []
    for y in range(c.h):
        for x in range(c.w):
            if c.get(x, y) is not None:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                v = c.get(x + dx, y + dy)
                if v is not None and v != col:
                    marks.append((x, y))
                    break
    for x, y in marks:
        c.set(x, y, col)


def disc(c, cx, cy, r, col):
    for y in range(int(cy - r) - 1, int(cy + r) + 2):
        for x in range(int(cx - r) - 1, int(cx + r) + 2):
            if (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= r * r:
                c.set(x, y, col)


def save_sprite(canvas, name, frame_h, previews):
    canvas.frame_h = frame_h
    n = len(canvas.colors())
    if n > 15:
        sys.exit(f"{name}: {n} colours, max 15")
    canvas.save_bmp(os.path.join(OUT, name + ".bmp"), bpp=4)
    write_json(os.path.join(OUT, name + ".json"), '{\n    "type": "sprite",\n    "height": %d\n}' % frame_h)
    previews.append((name, canvas))


# ============================================================================ items
RED, RED_S = rgb("#e84050"), rgb("#a02838")
GREEN, GREEN_S = rgb("#48b850"), rgb("#287838")
BLUE, BLUE_S = rgb("#4888e8"), rgb("#2c50a8")
GOLD, GOLD_S, GOLD_L = rgb("#f8c830"), rgb("#c88018"), rgb("#f8f0a0")
COAL, COAL_D, COAL_L = rgb("#4c4858"), rgb("#363040"), rgb("#8c88a0")
ICE = rgb("#c0c8e0")  # shade for white ribbons


def present(body, body_s, rib, rib_s, big):
    c = Canvas(16, 16)
    if big:
        bx0, bx1, by0, by1 = 2, 13, 7, 14   # box body
        lx0, lx1, ly0, ly1 = 1, 14, 4, 6    # lid
        bow_y = 1
    else:
        bx0, bx1, by0, by1 = 4, 11, 9, 14
        lx0, lx1, ly0, ly1 = 3, 12, 7, 8
        bow_y = 4
    c.rect(bx0, by0, bx1 - bx0 + 1, by1 - by0 + 1, body)
    c.rect(bx1 - 1, by0, 2, by1 - by0 + 1, body_s)     # right side in shadow
    c.hline(bx0, bx1, by1, body_s)                     # bottom edge
    c.hline(bx0, bx1, by0, body_s)                     # shadow under the lid
    c.rect(lx0, ly0, lx1 - lx0 + 1, ly1 - ly0 + 1, body)
    c.hline(lx0 + 1, lx1 - 2, ly0, WHITE if body != GOLD else GOLD_L)  # lid shine
    c.set(lx1, ly1, body_s)
    c.set(lx1, ly0, body_s)
    # Ribbon (2 px wide) down the middle and across the lid.
    c.rect(7, ly0, 2, by1 - ly0 + 1, rib)
    c.vline(8, ly0, by1, rib_s)
    if big:
        c.rect(bx0, 10, bx1 - bx0 + 1, 2, rib)
        c.hline(bx0, bx1, 11, rib_s)
        c.rect(7, 10, 2, 2, rib)
    # Bow: two loops and a knot.
    if big:
        loops = [(3, bow_y + 1), (4, bow_y), (5, bow_y), (6, bow_y + 1), (4, bow_y + 1), (5, bow_y + 2),
                 (9, bow_y + 1), (10, bow_y), (11, bow_y), (12, bow_y + 1), (11, bow_y + 1), (10, bow_y + 2),
                 (6, bow_y + 2), (9, bow_y + 2)]
    else:
        loops = [(5, bow_y), (6, bow_y), (5, bow_y + 1), (6, bow_y + 2),
                 (9, bow_y), (10, bow_y), (10, bow_y + 1), (9, bow_y + 2)]
    for x, y in loops:
        c.set(x, y, rib)
    c.rect(7, bow_y + 1, 2, 2, rib_s)
    outline(c)
    # Inner lines: between lid and box.
    for x in range(lx0, lx1 + 1):
        if c.get(x, ly1 + 1) not in (None, OUTLINE) and c.get(x, ly1 + 1) != rib and c.get(x, ly1 + 1) != rib_s:
            pass
    if big:
        c.set(4, bow_y + 1, OUTLINE)
        c.set(11, bow_y + 1, OUTLINE)
    return c


def star_present(sparkle):
    c = present(GOLD, GOLD_S, RED, RED_S, True)
    # Replace the bow with a shiny star topper.
    for y in range(0, 4):
        for x in range(0, 16):
            c.set(x, y, None)
    star = ["...w...",
            "..wWw..",
            "wwwWwww",
            ".wWWWw.",
            ".wW.Ww."]
    top = Canvas(16, 16)
    for y, row in enumerate(star):
        for x, ch in enumerate(row):
            if ch in "wW":
                top.set(4 + x, 1 + y, GOLD_L if ch == "w" else GOLD)
    outline(top)
    c.blit(top, 0, 0)
    # Shine on the box.
    for x, y in ((3, 8), (3, 9), (4, 8), (12, 12)):
        c.set(x, y, GOLD_L)
    if sparkle:
        for (x, y) in ((1, 2), (14, 6)):
            c.set(x, y, WHITE)
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if c.get(x + dx, y + dy) is None:
                    c.set(x + dx, y + dy, GOLD_L)
    else:
        c.set(1, 8, WHITE)
        c.set(14, 2, WHITE)
    return c


COAL_SHAPE = [
    "................",
    "......oooo......",
    "....oollllooo...",
    "...olllllcccdo..",
    "..ollccccccccdo.",
    ".olllccccccccddo",
    ".ollcccccccccddo",
    ".olccccccccccddo",
    ".olccccccccccddo",
    ".olccccccccccddo",
    ".occccccccccdddo",
    "..occcccccccddo.",
    "..ocdccccccdddo.",
    "...oddddddddoo..",
    "....oooooooo....",
    "................",
]


def coal(look_left):
    c = Canvas(16, 16)
    legend = {"o": OUTLINE, "c": COAL, "d": COAL_D, "l": COAL_L}
    c.ascii(0, 0, COAL_SHAPE, legend)
    # Big grumpy eyes under angry brows.
    for ex in (4, 9):
        for x in range(ex, ex + 3):
            for y in (7, 8):
                c.set(x, y, WHITE)
    px = 0 if look_left else 2
    for ex in (4, 9):
        c.set(ex + px, 8, OUTLINE)
        c.set(ex + px, 7, OUTLINE)
    for x, y in ((3, 5), (4, 5), (5, 6), (6, 6), (12, 5), (11, 5), (10, 6), (9, 6)):
        c.set(x, y, OUTLINE)
    # Frown.
    for x, y in ((5, 12), (6, 11), (7, 11), (8, 11), (9, 11), (10, 12)):
        c.set(x, y, OUTLINE)
    c.set(6, 3, WHITE)  # glint
    return c


def make_items(previews):
    frames = [
        present(RED, RED_S, WHITE, ICE, False),
        present(GREEN, GREEN_S, RED, RED_S, False),
        present(BLUE, BLUE_S, GOLD, GOLD_S, False),
        present(RED, RED_S, GOLD, GOLD_S, True),
        present(BLUE, BLUE_S, WHITE, ICE, True),
        star_present(False),
        star_present(True),
        coal(False),
        coal(True),
    ]
    sheet = Canvas(16, 16 * len(frames))
    for i, f in enumerate(frames):
        sheet.blit(f, 0, 16 * i)
    save_sprite(sheet, "catch_items", 16, previews)


# ============================================================================ basket
WICKER, WICKER_S, WICKER_L = rgb("#c07838"), rgb("#885020"), rgb("#e0a860")
INSIDE = rgb("#583018")
HOLLY, HOLLY_S = rgb("#40a048"), rgb("#286030")


def basket(fill):
    c = Canvas(32, 16)
    rim_y = 6
    # Peeking gifts first (the rim is drawn over them).
    if fill >= 1:
        c.rect(8, 3, 6, 5, RED)
        c.vline(10, 3, 7, WHITE)
        c.rect(18, 4, 6, 4, BLUE)
        c.vline(21, 4, 7, GOLD)
    if fill >= 2:
        c.rect(12, 0, 7, 8, GREEN)
        c.vline(15, 0, 7, RED)
        c.rect(5, 4, 3, 4, GOLD)
        c.rect(23, 2, 4, 6, RED)
        c.vline(25, 2, 7, GOLD)
    if fill >= 1:
        outline(c)
    # Opening: dark inside, seen from slightly above.
    for x in range(4, 28):
        for y in range(rim_y - 1, rim_y + 2):
            if c.get(x, y) is None:
                c.set(x, y, INSIDE)
    # Rim.
    c.rect(2, rim_y + 1, 28, 2, WICKER_L)
    c.hline(2, 29, rim_y + 2, WICKER)
    # Body tapering towards the bottom, with a woven pattern.
    for y in range(rim_y + 3, 15):
        inset = (y - rim_y - 3) // 3
        for x in range(3 + inset, 29 - inset):
            weave = ((x // 2) + (y // 2)) % 2
            col = WICKER if weave else WICKER_S
            if x >= 26 - inset:
                col = WICKER_S
            c.set(x, y, col)
        c.set(3 + inset, y, WICKER_L)
    # Holly on the front.
    for x, y in ((5, 10), (6, 10), (6, 11), (7, 11), (10, 10), (9, 10), (9, 11), (8, 11)):
        c.set(x, y, HOLLY)
    c.set(6, 11, HOLLY_S)
    c.set(9, 11, HOLLY_S)
    c.set(7, 10, RED)
    c.set(8, 10, RED)
    c.set(8, 9, RED)
    outline(c)
    # Rim edge lines.
    c.hline(4, 27, rim_y - 2, OUTLINE) if fill == 0 else None
    return c


def make_basket(previews):
    sheet = Canvas(32, 48)
    for i in range(3):
        sheet.blit(basket(i), 0, 16 * i)
    save_sprite(sheet, "catch_basket", 16, previews)


# ============================================================================ sleigh
SKIN = rgb("#f8d0b0")
FUR, FUR_S = rgb("#a86840"), rgb("#704020")
TAN = rgb("#e0c090")
SACK, SACK_S = GREEN, GREEN_S
SNOWW = rgb("#d0d8f0")


def sleigh(step):
    c = Canvas(64, 32)
    # --- Sleigh body (left part), x 2..30
    for y in range(16, 26):
        x0 = 3 if y > 17 else 6
        x1 = 30 - max(0, y - 22) * 2
        c.hline(x0, x1, y, RED)
    c.rect(1, 12, 5, 9, RED)          # high back of the sleigh
    c.hline(1, 5, 12, RED_S)
    c.vline(5, 13, 20, RED_S)
    c.hline(3, 30, 18, GOLD)          # gold trim
    c.hline(4, 27, 25, RED_S)
    c.hline(5, 27, 24, RED_S)
    # Runners.
    c.hline(4, 30, 28, GOLD_S)
    c.set(31, 27, GOLD_S)
    c.set(32, 26, GOLD_S)
    c.set(3, 27, GOLD_S)
    c.vline(10, 26, 27, GOLD_S)
    c.vline(24, 26, 27, GOLD_S)
    # Gift sack in the back.
    disc(c, 10, 11, 5.5, SACK)
    c.rect(8, 4, 4, 3, SACK)
    for x, y in ((12, 13), (13, 12), (14, 11), (13, 14), (12, 15), (11, 15)):
        c.set(x, y, SACK_S)
    c.hline(8, 11, 6, GOLD)           # tie
    c.rect(6, 3, 3, 2, RED)           # a present poking out
    c.rect(12, 2, 3, 3, BLUE)
    # --- Santa (in the front seat)
    c.rect(17, 9, 8, 8, RED)          # coat
    c.rect(17, 14, 8, 1, WHITE)       # belt fur
    c.rect(20, 14, 2, 1, OUTLINE)     # buckle
    c.rect(24, 10, 3, 3, RED)         # arm reaching to the reins
    c.set(27, 12, WHITE)
    c.rect(19, 3, 6, 5, SKIN)         # face
    c.rect(18, 6, 8, 4, WHITE)        # beard
    c.rect(19, 9, 6, 1, WHITE)
    c.set(24, 4, OUTLINE)             # eye
    c.set(25, 5, rgb("#f09090"))      # cheek
    c.rect(18, 1, 7, 2, RED)          # hat
    c.rect(16, 2, 3, 2, RED)
    c.rect(18, 3, 8, 1, WHITE)        # hat fur
    c.set(14, 3, WHITE)               # pompom
    c.set(15, 3, WHITE)
    c.set(14, 4, WHITE)
    # Reins.
    for x in range(28, 46):
        c.set(x, 12 + (x - 28) // 6, FUR_S)
    # --- Reindeer (x 38..62), facing right
    body_y = 13
    c.rect(40, body_y, 15, 7, FUR)
    c.hline(41, 53, body_y + 6, TAN)  # belly
    c.rect(38, body_y + 1, 2, 3, FUR)
    c.set(37, body_y + 1, TAN)        # tail
    c.rect(52, body_y - 5, 4, 7, FUR)  # neck
    c.rect(54, body_y - 8, 6, 5, FUR)  # head
    c.rect(59, body_y - 6, 3, 3, FUR)  # snout
    c.set(62, body_y - 6, RED)        # shiny red nose
    c.set(62, body_y - 5, RED)
    c.set(57, body_y - 7, OUTLINE)    # eye
    c.set(55, body_y - 9, FUR_S)      # ear
    c.set(54, body_y - 9, FUR_S)
    # Antlers.
    for x, y in ((56, 3), (56, 2), (55, 1), (57, 1), (58, 2), (59, 1), (53, 2), (54, 3), (52, 1)):
        c.set(x, y, TAN)
    # Harness with a bell.
    c.vline(51, body_y, body_y + 3, RED)
    c.set(51, body_y + 4, GOLD)
    # Legs: two poses.
    if step == 0:
        legs = [((41, body_y + 7), (-1, 1)), ((44, body_y + 7), (1, 1)), ((50, body_y + 7), (-1, 1)),
                ((53, body_y + 7), (1, 1))]
    else:
        legs = [((41, body_y + 7), (-1, 0)), ((44, body_y + 7), (-1, 1)), ((50, body_y + 7), (1, 0)),
                ((53, body_y + 7), (1, 1))]
    for (lx, ly), (dx, bend) in legs:
        for i in range(5):
            c.set(lx + (dx * i) // 2, ly + i, FUR_S if i < 4 else OUTLINE)
        c.set(lx + dx * 2, ly + 4, TAN)
    outline(c)
    # A few inner outline details.
    c.hline(17, 24, 9, RED_S)
    c.vline(16, 10, 15, OUTLINE)
    return c


def parked_sleigh(glow):
    """Santa's sleigh parked in the street, waiting for your stack: 32x32, runners on row 30, a big open
    sack at the back. glow = the frame shown while you deliver (the sack shines)."""
    c = Canvas(32, 32)
    for y in range(19, 28):                         # body
        x0 = 4 if y > 20 else 7
        x1 = 29 - max(0, y - 24) * 2
        c.hline(x0, x1, y, RED)
    c.rect(2, 14, 5, 10, RED)                       # high back
    c.hline(2, 6, 14, RED_S)
    c.vline(6, 15, 22, RED_S)
    c.hline(4, 29, 21, GOLD)                        # gold trim
    c.hline(5, 26, 26, RED_S)
    c.hline(5, 28, 30, GOLD_S)                      # runners
    c.set(29, 29, GOLD_S)
    c.set(30, 28, GOLD_S)
    c.set(4, 29, GOLD_S)
    c.vline(11, 28, 29, GOLD_S)
    c.vline(24, 28, 29, GOLD_S)
    # The open sack on the seat, with presents in it.
    disc(c, 18, 14, 7.5, SACK)
    for x, y in ((22, 17), (23, 16), (24, 15), (23, 18), (22, 19), (21, 19)):
        c.set(x, y, SACK_S)
    c.hline(12, 24, 8, SACK_S)                      # the open mouth of the sack
    c.hline(13, 23, 7, GOLD if glow else SACK_S)
    c.rect(13, 4, 4, 4, RED)                        # presents poking out
    c.rect(18, 3, 4, 5, BLUE)
    c.vline(15, 4, 7, GOLD)
    c.vline(20, 3, 7, WHITE)
    outline(c)
    if glow:
        for (x, y) in ((11, 2), (25, 1), (18, 0), (27, 5)):
            c.set(x, y, GOLD_L)
    return c


def make_sleigh(previews):
    sheet = Canvas(64, 64)
    for i in range(2):
        sheet.blit(sleigh(i), 0, 32 * i)
    save_sprite(sheet, "catch_sleigh", 32, previews)
    parked = Canvas(32, 64)
    parked.blit(parked_sleigh(False), 0, 0)
    parked.blit(parked_sleigh(True), 0, 32)
    save_sprite(parked, "catch_parked", 32, previews)


# ============================================================================ score popups
DIGITS = {
    "0": [".##.", "#..#", "#..#", "#..#", "#..#", "#..#", ".##."],
    "1": [".#", "##", ".#", ".#", ".#", ".#", ".#"],
    "2": [".##.", "#..#", "...#", "..#.", ".#..", "#...", "####"],
    "3": ["###.", "...#", "...#", ".##.", "...#", "...#", "###."],
    "5": ["####", "#...", "###.", "...#", "...#", "#..#", ".##."],
    "7": ["####", "...#", "..#.", "..#.", ".#..", ".#..", ".#.."],
    "+": ["...", "...", ".#.", "###", ".#.", "...", "..."],
    "-": ["...", "...", "...", "###", "...", "...", "..."],
    "@": [".....", ".#.#.", "#####", "#####", ".###.", "..#..", "....."],
}


def popup(text, ink, ink_s):
    c = Canvas(32, 16)
    gap = 2
    width = sum(len(DIGITS[ch][0]) + gap for ch in text) - gap
    x = (32 - width) // 2
    for ch in text:
        g = DIGITS[ch]
        for gy, row in enumerate(g):
            for gx, v in enumerate(row):
                if v == "#":
                    c.set(x + gx, 4 + gy, ink_s if gy >= len(g) - 2 else ink)
        x += len(g[0]) + gap
    outline(c)
    return c


POPUPS = ["+10", "+20", "+25", "+30", "+50", "+75", "+100", "+150"]


def make_popups(previews):
    frames = [popup(t, GOLD_L, GOLD) for t in POPUPS] + [popup("+@", RED, RED_S), popup("-@", RED, RED_S)]
    sheet = Canvas(32, 16 * len(frames))
    for i, f in enumerate(frames):
        sheet.blit(f, 0, 16 * i)
    save_sprite(sheet, "catch_popup", 16, previews)


# ============================================================================ background
SKY = [rgb(h) for h in ("#202860", "#283070", "#383880", "#504088", "#704890", "#985090", "#c06090",
                        "#e07888", "#f09880", "#f8b888")]
HILL, HILL_S = rgb("#6870b0"), rgb("#5058a0")
FAR_SNOW = rgb("#a8b0e0")
HOUSE_DARK = rgb("#403868")
SNOW, SNOW_S, SNOW_D = rgb("#f8f8f8"), rgb("#c8d0f0"), rgb("#98a0d8")
WALL, WALL_S, WALL_L = rgb("#b05038"), rgb("#803828"), rgb("#d06848")
WOOD, WOOD_S = rgb("#784830"), rgb("#503020")
LIGHT, LIGHT_S, LIGHT_L = rgb("#f8c850"), rgb("#e08838"), rgb("#f8f0a8")
BLUEWALL, BLUEWALL_S = rgb("#5878c0"), rgb("#3c5498")
GREENWALL, GREENWALL_S = rgb("#58a080"), rgb("#3c7860")
PINE, PINE_S = rgb("#287050"), rgb("#1c5040")
STONE = rgb("#707098")
BULBS = [rgb("#f84848"), rgb("#48e058"), rgb("#f8d840"), rgb("#50a8f8")]
ROAD, ROAD_S = rgb("#d8dcf4"), rgb("#b0b8e8")


def sky(c):
    band_h = 11
    for y in range(c.h):
        b = min(len(SKY) - 1, y // band_h)
        col = SKY[b]
        nxt = SKY[min(len(SKY) - 1, b + 1)]
        within = y % band_h
        for x in range(c.w):
            # Two dithered rows at the bottom of each band blend into the next colour.
            if within == band_h - 2 and (x + y) % 4 == 0:
                c.set(x, y, nxt)
            elif within == band_h - 1 and (x + y) % 2 == 0:
                c.set(x, y, nxt)
            else:
                c.set(x, y, col)


def star(c, x, y, big=False):
    c.set(x, y, WHITE)
    if big:
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            c.set(x + dx, y + dy, LIGHT_L)


def moon(c, cx, cy, r):
    disc(c, cx, cy, r, rgb("#f8f0c8"))
    disc(c, cx + 2, cy + 1, r - 2.5, rgb("#e8e0b0"))
    disc(c, cx + 4, cy - 2, r - 1, None)  # crescent bite: restore sky afterwards
    return (cx, cy, r)


def window(c, x, y, w, h, frame=WOOD_S):
    c.rect(x - 1, y - 1, w + 2, h + 2, frame)
    c.rect(x, y, w, h, LIGHT)
    c.rect(x, y, w, 2, LIGHT_L)
    c.rect(x + w - 1, y, 1, h, LIGHT_S)
    c.vline(x + w // 2, y, y + h - 1, frame)
    c.hline(x, x + w - 1, y + h // 2, frame)
    c.hline(x - 2, x + w + 1, y + h + 1, SNOW)   # snow on the sill


def snow_roof(c, x0, x1, top_y, peak_x, eave_y, col_snow=SNOW, col_shade=SNOW_S):
    """Triangular roof covered in snow, lit from the left, with a dark wooden edge under the snow."""
    prev_l, prev_r = peak_x, peak_x
    for y in range(top_y, eave_y + 1):
        t = (y - top_y) / max(1, eave_y - top_y)
        left = int(round(peak_x - (peak_x - x0) * t))
        right = int(round(peak_x + (x1 - peak_x) * t))
        c.hline(left, peak_x, y, col_snow)
        c.hline(peak_x + 1, right, y, col_shade)
        # Continuous outline along both slopes.
        c.hline(left, prev_l, y, OUTLINE)
        c.hline(prev_r, right, y, OUTLINE)
        prev_l, prev_r = left, right
    # Soft lumps of snow: a brighter band along the bottom edge.
    c.hline(x0 + 1, x1 - 1, eave_y - 1, col_snow)
    c.hline(x0 + 1, x1 - 1, eave_y, col_shade)
    c.hline(x0, x1, eave_y + 1, OUTLINE)
    c.hline(x0 + 2, x1 - 2, eave_y + 2, WOOD_S)
    c.hline(x0 + 2, x1 - 2, eave_y + 3, OUTLINE)
    # Icicles along the eave.
    for x in range(x0 + 4, x1 - 3, 6):
        c.set(x, eave_y + 2, col_shade)
        c.set(x, eave_y + 3, col_shade)
        c.set(x, eave_y + 4, SNOW_D)


def string_lights(c, x0, x1, y0, y1):
    """A wire with coloured bulbs drooping between (x0, y0) and (x1, y1) in little swags."""
    n = 0
    span = 12
    for x in range(x0, x1 + 1):
        base = y0 + (y1 - y0) * (x - x0) / max(1, x1 - x0)
        phase = ((x - x0) % span) / span
        sag = int(3 * 4 * phase * (1 - phase))
        y = int(base) + sag
        c.set(x, y, OUTLINE)
        if (x - x0) % span == span // 2:
            col = BULBS[n % len(BULBS)]
            c.set(x, y + 1, col)
            c.set(x, y + 2, col)
            c.set(x + 1, y + 1, col)
            c.set(x - 1, y + 1, col)
            n += 1


def pine(c, cx, base_y, h):
    for i in range(h):
        half = 1 + (i * 9) // h if i % 6 > 1 else (i * 9) // h
        y = base_y - h + i
        c.hline(cx - half, cx + half, y, PINE)
        c.hline(cx + 1, cx + half, y, PINE_S)
        if i % 6 == 2:
            c.hline(cx - half + 1, cx, y, SNOW)
    c.rect(cx - 1, base_y, 3, 3, WOOD_S)


def far_hills(c):
    import math
    for x in range(c.w):
        h = 94 + int(5 * math.sin(x / 23.0) + 3 * math.sin(x / 9.0 + 1))
        for y in range(h, 112):
            c.set(x, y, HILL if y > h + 1 else FAR_SNOW)
    # Little distant houses with lit windows.
    for hx in (8, 46, 196, 222):
        c.rect(hx, 92, 11, 10, HOUSE_DARK)
        for i in range(7):
            c.hline(hx - 1 + i, hx + 11 - i, 91 - i, FAR_SNOW if i < 2 else HOUSE_DARK)
        c.rect(hx + 4, 95, 3, 3, LIGHT)


def workshop(c):
    x0, x1 = 66, 174            # wall
    top, ground = 74, 132
    # Chimney (behind the roof) with a puff of smoke.
    c.rect(140, 36, 10, 22, rgb("#904040"))
    c.rect(140, 36, 10, 2, SNOW)
    c.rect(138, 34, 14, 3, SNOW)
    c.hline(138, 151, 33, OUTLINE)
    c.vline(139, 37, 57, OUTLINE)
    c.vline(150, 37, 57, OUTLINE)
    for i, (sx, sy, r) in enumerate(((146, 28, 3), (150, 22, 4), (156, 16, 3.5))):
        disc(c, sx, sy, r, rgb("#b8b0d0") if i else rgb("#d0c8e0"))
    # Wall.
    c.rect(x0, top, x1 - x0 + 1, ground - top, WALL)
    for y in range(top + 4, ground, 6):
        c.hline(x0, x1, y, WALL_S)          # planks
    c.rect(x0, top, 3, ground - top, WALL_L)
    c.rect(x1 - 2, top, 3, ground - top, WALL_S)
    c.vline(x0 - 1, top, ground, OUTLINE)
    c.vline(x1 + 1, top, ground, OUTLINE)
    # Big roof.
    snow_roof(c, x0 - 10, x1 + 10, 40, 120, top - 1)
    # Round attic window, warmly lit.
    disc(c, 120, 58, 7.5, OUTLINE)
    disc(c, 120, 58, 6.5, WOOD_S)
    disc(c, 120, 58, 5, LIGHT)
    disc(c, 119, 57, 3, LIGHT_L)
    c.vline(120, 53, 63, WOOD_S)
    c.hline(115, 125, 58, WOOD_S)
    # String lights along the eaves and the roof edges.
    string_lights(c, x0 - 6, x1 + 6, top + 4, top + 4)
    # Windows.
    window(c, 76, 88, 20, 16)
    window(c, 144, 88, 20, 16)
    # Door with a wreath.
    c.rect(106, 96, 28, 36, WOOD_S)
    c.rect(108, 98, 24, 34, WOOD)
    c.vline(120, 98, 131, WOOD_S)
    for y in (104, 116):
        c.hline(109, 118, y, WOOD_S)
        c.hline(122, 131, y, WOOD_S)
    c.set(117, 112, LIGHT)
    c.set(123, 112, LIGHT)
    # Wreath.
    for y in range(-6, 7):
        for x in range(-6, 7):
            d = x * x + y * y
            if 12 <= d <= 36:
                c.set(120 + x, 107 + y, GREENWALL_S if (x + y) % 3 == 0 else PINE)
    for x, y in ((115, 103), (124, 104), (117, 111), (123, 110), (125, 107)):
        c.set(x, y, BULBS[0])
    c.rect(118, 112, 5, 2, BULBS[0])        # bow
    c.set(117, 114, BULBS[0])
    c.set(123, 114, BULBS[0])
    # Sign "TOYS" above the door.
    c.rect(102, 82, 36, 11, WOOD_S)
    c.rect(103, 83, 34, 9, LIGHT_L)
    letters = {
        "T": ["###", ".#.", ".#.", ".#.", ".#."],
        "O": ["###", "#.#", "#.#", "#.#", "###"],
        "Y": ["#.#", "#.#", ".#.", ".#.", ".#."],
        "S": ["###", "#..", "###", "..#", "###"],
    }
    lx = 106
    for ch in "TOYS":
        for gy, row in enumerate(letters[ch]):
            for gx, v in enumerate(row):
                if v == "#":
                    c.set(lx + gx * 2, 85 + gy, WALL_S)
                    c.set(lx + gx * 2 + 1, 85 + gy, WALL_S)
        lx += 8
    # Snow drifts along the bottom of the wall.
    for x in range(x0, x1 + 1):
        h = 2 + (x * 7 % 5 == 0)
        c.vline(x, ground - h, ground - 1, SNOW_S)


def house(c, x0, x1, top, ground, wall, wall_s, roof_top):
    c.rect(x0, top, x1 - x0 + 1, ground - top, wall)
    c.rect(x1 - 2, top, 3, ground - top, wall_s)
    for y in range(top + 5, ground, 7):
        c.hline(x0, x1, y, wall_s)
    c.vline(x0 - 1, top, ground, OUTLINE)
    c.vline(x1 + 1, top, ground, OUTLINE)
    snow_roof(c, x0 - 6, x1 + 6, roof_top, (x0 + x1) // 2, top - 1)


def lamp(c, x, base_y, top_y):
    c.rect(x, top_y, 2, base_y - top_y, OUTLINE)
    c.rect(x - 1, base_y - 3, 4, 3, OUTLINE)
    # Lantern.
    c.rect(x - 3, top_y - 9, 8, 9, OUTLINE)
    c.rect(x - 2, top_y - 8, 6, 7, LIGHT)
    c.rect(x - 1, top_y - 7, 3, 4, LIGHT_L)
    c.rect(x - 4, top_y - 11, 10, 2, OUTLINE)
    c.rect(x - 3, top_y - 12, 8, 1, SNOW)


def ground(c):
    top = 132
    for y in range(top, c.h):
        for x in range(c.w):
            c.set(x, y, SNOW if y < 140 else ROAD)
    c.hline(0, c.w - 1, top, SNOW_S)
    # Soft blue shadows / tracks on the street.
    for y in range(146, 160, 5):
        for x in range(c.w):
            if (x // 16 + y // 5) % 3 == 0:
                c.set(x, y, ROAD_S)
    for x in range(c.w):
        c.set(x, 140, SNOW_S if x % 2 else SNOW)
    # Snow mounds.
    for mx, mw in ((10, 22), (190, 30)):
        for i in range(mw):
            h = int(5 * (1 - abs((i - mw / 2) / (mw / 2)) ** 2))
            c.vline(mx + i, top - h, top, SNOW)
            c.set(mx + i, top - h - 1, OUTLINE) if h > 0 else None


def make_bg(previews):
    scr = Canvas(240, 160)
    sky(scr)
    for x, y, big in ((20, 30, True), (52, 12, False), (84, 26, False), (110, 8, True), (150, 20, False),
                      (172, 40, False), (218, 32, True), (36, 52, False), (96, 46, False), (232, 50, False),
                      (128, 30, False), (70, 6, False)):
        star(scr, x, y, big)
    # Crescent moon.
    disc(scr, 196, 28, 8, rgb("#f8f0c8"))
    disc(scr, 196, 28, 8, rgb("#f8f0c8"))
    for y in range(18, 38):
        for x in range(186, 210):
            if (x + 0.5 - 200) ** 2 + (y + 0.5 - 25) ** 2 <= 7 * 7 and scr.get(x, y) == rgb("#f8f0c8"):
                scr.set(x, y, SKY[y // 11])
    far_hills(scr)
    # Left house (blue) and right house (green).
    house(scr, 6, 52, 92, 132, BLUEWALL, BLUEWALL_S, 70)
    window(scr, 20, 104, 14, 12)
    house(scr, 190, 236, 94, 132, GREENWALL, GREENWALL_S, 72)
    window(scr, 206, 106, 14, 12)
    string_lights(scr, 0, 58, 92, 92)
    string_lights(scr, 184, 240, 94, 94)
    pine(scr, 60, 132, 38)
    pine(scr, 182, 132, 34)
    workshop(scr)
    ground(scr)
    lamp(scr, 34, 132, 104)  # in front of the left house
    # Full 256x256 background: screen pixel (sx, sy) is background pixel (sx + 8, sy + 48).
    bg = Canvas(256, 256)
    for y in range(256):
        sy = min(159, max(0, y - 48))
        for x in range(256):
            sx = min(239, max(0, x - 8))
            bg.set(x, y, scr.get(sx, sy))
    n = len(bg.colors())
    if n > 255:
        sys.exit(f"catch_bg: {n} colours")
    bg.save_bmp(os.path.join(OUT, "catch_bg.bmp"), bpp=8)
    write_json(os.path.join(OUT, "catch_bg.json"), '{\n    "type": "regular_bg",\n    "bpp_mode": "bpp_4_auto"\n}')
    previews.append(("catch_bg", scr))
    # Report unique tiles (helps keep VRAM use low).
    tiles = set()
    for ty in range(32):
        for tx in range(32):
            t = tuple(bg.get(tx * 8 + i, ty * 8 + j) for j in range(8) for i in range(8))
            tiles.add(t)
    print(f"catch_bg: {n} colours, {len(tiles)} unique tiles (before flip reduction)")


def main():
    os.makedirs(OUT, exist_ok=True)
    previews = []
    make_items(previews)
    make_basket(previews)
    make_sleigh(previews)
    make_popups(previews)
    make_bg(previews)
    if len(sys.argv) > 1:
        os.makedirs(sys.argv[1], exist_ok=True)
        sheets = [cv for name, cv in previews if name != "catch_bg"]
        width = max(cv.w * (cv.h // cv.frame_h) + cv.h // cv.frame_h for cv in sheets)
        overview = Canvas(width, sum(cv.frame_h + 2 for cv in sheets))
        oy = 0
        for cv in sheets:
            for i in range(cv.h // cv.frame_h):
                for y in range(cv.frame_h):
                    for x in range(cv.w):
                        overview.set(i * (cv.w + 1) + x, oy + y, cv.get(x, i * cv.frame_h + y))
            oy += cv.frame_h + 2
        save_png(os.path.join(sys.argv[1], "catch_sprites.png"), overview, scale=4)
        save_png(os.path.join(sys.argv[1], "catch_bg.png"), previews[-1][1], scale=3)
    print(f"Wrote Gift Catch art to {os.path.normpath(OUT)}")


if __name__ == "__main__":
    main()
