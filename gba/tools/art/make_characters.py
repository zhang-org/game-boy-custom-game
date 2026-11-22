"""Generates the 10 family character sprite sheets in gba/graphics/characters/.

Each sheet is 16 px wide and 12 frames of 16x32 stacked vertically (16x384), 16 colours.
Frame order (keep it if you redraw a sheet by hand; see include/sb_characters.h):
   0 down stand   1 down walk A   2 down walk B
   3 up stand     4 up walk A     5 up walk B
   6 side stand   7 side walk A   8 side walk B      (side frames face RIGHT; flip for left)
   9 sit (facing down, for the sled)
  10 side sit (facing right, for riding and fishing)
  11 cheer (arms up)

To change how someone looks, edit their line in CHARACTERS below and re-run:
    python3 tools/art/make_characters.py
Or redraw graphics/characters/<name>.bmp by hand (it then stops being generated:
remove that person from CHARACTERS so your drawing isn't overwritten).
"""
import os
import sys
from canvas import Canvas, rgb, write_json, save_png

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
OUT = os.path.join(ROOT, "graphics", "characters")

SKIN = {  # (base, shade)
    "light": ("#f8d8b8", "#e0a888"),
    "fair": ("#f0c8a0", "#d8a078"),
    "medium": ("#d8a878", "#b88058"),
    "tan": ("#c08860", "#986040"),
    "brown": ("#986040", "#784828"),
    "deep": ("#704830", "#583420"),
}
HAIR = {  # (base, shade, light)
    "black": ("#383040", "#201828", "#686078"),
    "darkbrown": ("#583828", "#382018", "#806048"),
    "brown": ("#885830", "#603818", "#b08050"),
    "auburn": ("#b84828", "#802818", "#e07850"),
    "blonde": ("#f0c850", "#c89828", "#f8e8a0"),
    "grey": ("#a8a8b0", "#787880", "#e0e0e8"),
}

# name, skin, hair colour, hair style, coat, pants, accent (scarf/hat), extras
CHARACTERS = [
    ("Adam",    "fair",   "black",     "spiky",    "#d83838", "#384878", "#30a050", ["glasses"]),
    ("Charly",  "fair",   "black",     "short",    "#38a048", "#484050", "#3870d8", ["beanie"]),
    ("Billy",   "light",  "brown",     "short",    "#3870d0", "#505058", "#e04040", ["cap"]),
    ("Bo",      "light",  "blonde",    "bowl",     "#f08828", "#386090", "#f8f8f8", []),
    ("Cherry",  "light",  "auburn",    "long",     "#f080b0", "#584868", "#e03050", ["bow"]),
    ("Lillian", "fair",   "black",     "long",     "#8858c8", "#383850", "#f8d050", ["glasses"]),
    ("Cheng",   "medium", "black",     "short",    "#28a0a0", "#404858", "#e03838", ["santa"]),
    ("Ella",    "light",  "blonde",    "ponytail", "#68b8f0", "#485070", "#f06890", ["earmuffs"]),
    ("Aiden",   "brown",  "darkbrown", "curly",    "#f0c030", "#405070", "#d83838", []),
    ("Alyssa",  "tan",    "brown",     "pigtails", "#d03890", "#403848", "#40b8a0", []),
]

OUTLINE = rgb("#282028")
WHITE = rgb("#f8f8f8")
BLUSH = rgb("#f09898")


def shade(hexcol, f=0.72):
    c = rgb(hexcol)
    return tuple(int(v * f) & 0xF8 for v in c)


# ----------------------------------------------------------------------------- hair and hats
# Masks: (first_y, rows). 'h' hair, 'H' hair shade, 'L' hair light. 'a' accent, 'A' accent shade,
# 'w' white, 'o' outline, '.' leaves what's underneath.
HAIR_MASKS = {
    "short": {
        "down": (7, [".....hhhhhh.....",
                     "...hhhLLhhhhh...",
                     "..hhhLhhhhhhhh..",
                     "..hhhhhhhhhhhh..",
                     "..hhhhhhhhhhhh..",
                     "..hhhHhhhHhhhh..",
                     "..hH.H...H..Hh..",
                     "..h..........h.."]),
        "up": (7, [".....hhhhhh.....",
                   "...hhhLLhhhhh...",
                   "..hhhLhhhhhhhh..",
                   "..hhhhhhhhhhhh..",
                   "..hhhhhhhhhhhh..",
                   "..hhhhhhhhhhhh..",
                   "..hhhhhhhhhhhh..",
                   "..hhhhhhhhhhhh..",
                   "..hhhhhhhhhhhh..",
                   "..HhhhhhhhhhhH..",
                   "...HHHHHHHHHH..."]),
        "side": (7, ["....hhhhhh......",
                     "...hhLLhhhhh....",
                     "..hhLhhhhhhhh...",
                     "..hhhhhhhhhhhh..",
                     "..hhhhhhhhhhhh..",
                     "..hhhhhhhhHhHh..",
                     "..hhhhhhH.......",
                     "..hhhhhH........",
                     "..hhhhhH........",
                     "...hhhH.........",
                     "....HH.........."]),
    },
    "bowl": {
        "down": (6, [".....hhhhhh.....",
                     "...hhhhhhhhhh...",
                     "..hhhLLhhhhhhh..",
                     "..hhLhhhhhhhhh..",
                     "..hhhhhhhhhhhh..",
                     "..hhhhhhhhhhhh..",
                     "..hhhhhhhhhhhh..",
                     "..HHHHHHHHHHHH..",
                     "..hh........hh..",
                     "..HH........HH.."]),
        "up": (6, [".....hhhhhh.....",
                   "...hhhhhhhhhh...",
                   "..hhhLLhhhhhhh..",
                   "..hhLhhhhhhhhh..",
                   "..hhhhhhhhhhhh..",
                   "..hhhhhhhhhhhh..",
                   "..hhhhhhhhhhhh..",
                   "..hhhhhhhhhhhh..",
                   "..hhhhhhhhhhhh..",
                   "..hhhhhhhhhhhh..",
                   "..HHHHHHHHHHHH.."]),
        "side": (6, [".....hhhhhh.....",
                     "...hhhhhhhhhh...",
                     "..hhhLLhhhhhhh..",
                     "..hhLhhhhhhhhh..",
                     "..hhhhhhhhhhhh..",
                     "..hhhhhhhhhhhh..",
                     "..hhhhhhhhhhhh..",
                     "..hhhhhhHHHHHH..",
                     "..hhhhhhh.......",
                     "..hhhhhhH.......",
                     "..HHHHHH........"]),
    },
    "curly": {
        "down": (5, [".....h.hh.h.....",
                     "...hhhhhhhhhh...",
                     "..hhhhhhhhhhhh..",
                     ".hhhLhhhLhhhhhh.",
                     ".hhLhhhLhhhhhhh.",
                     ".hhhhhhhhhhhhhh.",
                     ".hhhhhhhhhhhhhh.",
                     ".hhHhhHhhHhhHhh.",
                     ".hH.H..H..H..Hh.",
                     ".hh..........hh.",
                     ".Hh..........hH."]),
        "up": (5, [".....h.hh.h.....",
                   "...hhhhhhhhhh...",
                   "..hhhhhhhhhhhh..",
                   ".hhhLhhhLhhhhhh.",
                   ".hhLhhhLhhhhhhh.",
                   ".hhhhhhhhhhhhhh.",
                   ".hhhhhhhhhhhhhh.",
                   ".hhhhhhhhhhhhhh.",
                   ".hhhhhhhhhhhhhh.",
                   ".hhhhhhhhhhhhhh.",
                   ".HhhhhhhhhhhhhH.",
                   "..HHhHHhHHhHHH.."]),
        "side": (5, ["....h.hh.h......",
                     "..hhhhhhhhhh....",
                     ".hhhhhhhhhhhh...",
                     ".hhLhhhLhhhhhh..",
                     ".hLhhhLhhhhhhhh.",
                     ".hhhhhhhhhhhhhh.",
                     ".hhhhhhhhhhhhHh.",
                     ".hhhhhhhhHhH.H..",
                     ".hhhhhhhH.......",
                     ".hhhhhhH........",
                     ".HhhhhhH........",
                     "..HHhHH........."]),
    },
}

# Long hair, ponytail, pigtails and spiky are variations of "short".
def _extend(base, view, extra_rows_at, rows):
    y0, base_rows = HAIR_MASKS[base][view]
    out = list(base_rows)
    while len(out) < extra_rows_at - y0 + len(rows):
        out.append("." * 16)
    for i, r in enumerate(rows):
        merged = list(out[extra_rows_at - y0 + i])
        for x, ch in enumerate(r):
            if ch != ".":
                merged[x] = ch
        out[extra_rows_at - y0 + i] = "".join(merged)
    return (y0, out)


def _prepend(mask, rows):
    y0, base_rows = mask
    return (y0 - len(rows), rows + base_rows)


HAIR_MASKS["long"] = {
    "down": _extend("short", "down", 14, ["..hh........hh..", "..hh........hh..", "..hh........hh..",
                                          "..hH........Hh..", "..hH........Hh..", "..HH........HH..",
                                          "..H..........H.."]),
    "up": _extend("short", "up", 16, ["..hhhhhhhhhhhh..", "..hhhhhhhhhhhh..", "..hhhhhhhhhhhh..",
                                      "..hhhhhhhhhhhh..", "...hhhhhhhhhh...", "...hhhhhhhhhh...",
                                      "...HhHhHHhHhH..."]),
    "side": _extend("short", "side", 15, ["..hhhhhH........", "..hhhhhH........", "..hhhhH.........",
                                          "..hhhhH.........", "..hhhhH.........", "..HhhH..........",
                                          "...HH..........."]),
}
HAIR_MASKS["ponytail"] = {
    "down": _extend("short", "down", 9, ["............hh..", ".............hh.", ".............hh.",
                                         ".............hH.", ".............Hh.", "............HH..",
                                         "............H..."]),
    "up": _extend("short", "up", 9, ["......aaaa......", "......hhhh......", ".......hh.......",
                                     ".......hh.......", ".......hh.......", ".......hH.......",
                                     ".......HH.......", ".......hh.......", ".......hh.......",
                                     ".......Hh.......", "........H......."]),
    "side": _extend("short", "side", 9, ["................", "................", ".hh.............",
                                         "hhh.............", "hhH.............", "hH..............",
                                         "hH..............", "H..............."]),
}
HAIR_MASKS["pigtails"] = {
    "down": _extend("short", "down", 11, ["aa..........aa..", "hhh........hhh..", "hhhh......hhhh..",
                                          "hhhH......Hhhh..", ".hH........Hh...", ".H..........H..."]),
    "up": _extend("short", "up", 11, ["aa..........aa..", "hhh........hhh..", "hhhh......hhhh..",
                                      "hhhH......Hhhh..", ".hH........Hh...", ".H..........H..."]),
    "side": _extend("short", "side", 11, [".aa.............", "hhh.............", "hhhh............",
                                          "hhhH............", ".hH.............", ".H.............."]),
}
HAIR_MASKS["spiky"] = {
    "down": _prepend(HAIR_MASKS["short"]["down"], ["....h..h..h.....", "....hh.hh.hh...."]),
    "up": _prepend(HAIR_MASKS["short"]["up"], ["....h..h..h.....", "....hh.hh.hh...."]),
    "side": _prepend(HAIR_MASKS["short"]["side"], ["..h..h..h.......", "..hh.hh.hh......"]),
}
# Pigtail/ponytail hair ties use the accent colour ('a' rows above).

HATS = {
    "beanie": {
        "down": (3, [".......ww.......",
                     "......wwww......",
                     ".......ww.......",
                     "....aaaaaaaa....",
                     "...aaaaaaaaaa...",
                     "..aaaaaaaaaaaa..",
                     "..aaaaaaaaaaaa..",
                     "..AAAAAAAAAAAA..",
                     "..AaAaAaAaAaAa.."]),
        "side": (3, [".....ww.........",
                     "....wwww........",
                     ".....ww.........",
                     "...aaaaaaaa.....",
                     "..aaaaaaaaaa....",
                     "..aaaaaaaaaaa...",
                     "..aaaaaaaaaaaa..",
                     "..AAAAAAAAAAAA..",
                     "..AaAaAaAaAaAa.."]),
    },
    "santa": {
        "down": (1, ["..............ww",
                     "............aaww",
                     "..........aaaa..",
                     "........aaaaa...",
                     "......aaaaaaa...",
                     ".....aaaaaaaa...",
                     "....aaaaaaaaaa..",
                     "...aaaaaaaaaaa..",
                     "..wwwwwwwwwwwwww"[:16],
                     "..wwwwwwwwwwww.."]),
        "side": (1, ["ww..............",
                     "wwaa............",
                     "..aaaa..........",
                     "...aaaaa........",
                     "...aaaaaaa......",
                     "...aaaaaaaa.....",
                     "..aaaaaaaaaa....",
                     "..aaaaaaaaaaa...",
                     "..wwwwwwwwwwww..",
                     "..wwwwwwwwwwww.."]),
    },
    "cap": {
        "down": (5, ["......aaaa......",
                     "....aaaaaaaa....",
                     "...aaaawwaaaa...",
                     "..aaaaaaaaaaaa..",
                     "..aaaaaaaaaaaa..",
                     ".AAAAAAAAAAAAAA.",
                     "..AAAAAAAAAAAA.."]),
        "side": (5, [".....aaaa.......",
                     "...aaaaaaaa.....",
                     "..aaaaaaaaaa....",
                     "..aaaaaaaaaaa...",
                     "..aaaaaaaaaaa...",
                     "..AAAAAAAAAAAAAA",
                     "...........AAAA."]),
    },
    "earmuffs": {
        "down": (6, ["....aaaaaaaa....",
                     "...a........a...",
                     "..a..........a..",
                     "................",
                     "................",
                     "wa..........aw..",
                     "waa........aaw..",
                     "waa........aaw..",
                     ".a..........a..."]),
        "side": (6, ["....aaaaaa......",
                     "...a......a.....",
                     "..a.......a.....",
                     "................",
                     "................",
                     "....waw.........",
                     "...waaaw........",
                     "...waaaw........",
                     "....waw........."]),
    },
    "bow": {
        "down": (5, ["..........aa.aa.",
                     "..........aAaAa.",
                     "...........aAa..",
                     "..........a...a."]),
        "side": (5, ["....aa.aa.......",
                     "....aAaAa.......",
                     ".....aAa........",
                     "....a...a......."]),
    },
}
HATS["beanie"]["up"] = HATS["beanie"]["down"]
HATS["santa"]["up"] = HATS["santa"]["down"]
HATS["cap"]["up"] = (5, ["......aaaa......", "....aaaaaaaa....", "...aaaaaaaaaa...", "..aaaaaaaaaaaa..",
                         "..aaaaaaaaaaaa..", "..AAAAAAAAAAAA..", "................"])
HATS["earmuffs"]["up"] = HATS["earmuffs"]["down"]
HATS["bow"]["up"] = (5, ["..........aa.aa.", "..........aAaAa.", "...........aAa..", "..........a...a."])


def apply_mask(c, mask, legend, dx=0, dy=0):
    y0, rows = mask
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch in legend:
                c.set(x + dx, y0 + y + dy, legend[ch])


# ----------------------------------------------------------------------------- bodies
class Pal:
    def __init__(self, skin, hair, coat, pants, accent):
        self.skin, self.skin_s = rgb(SKIN[skin][0]), rgb(SKIN[skin][1])
        self.hair, self.hair_s, self.hair_l = (rgb(v) for v in HAIR[hair])
        self.coat, self.coat_s = rgb(coat), shade(coat)
        self.pants, self.pants_s = rgb(pants), shade(pants, 0.7)
        self.boots = rgb("#583828")
        self.accent, self.accent_s = rgb(accent), shade(accent)
        if self.accent == WHITE:
            self.accent_s = rgb("#c8d0e0")


def head_front(c, p, dy=0):
    c.rect(4, 8 + dy, 8, 1, p.skin)
    c.rect(3, 9 + dy, 10, 9, p.skin)
    c.rect(4, 18 + dy, 8, 1, p.skin)
    c.rect(3, 16 + dy, 1, 2, p.skin_s)
    c.rect(12, 16 + dy, 1, 2, p.skin_s)


def face_front(c, p, dy=0, mouth=False):
    for x in (5, 10):
        c.set(x, 14 + dy, OUTLINE)
        c.set(x, 15 + dy, OUTLINE)
    c.set(4, 16 + dy, BLUSH)
    c.set(11, 16 + dy, BLUSH)
    if mouth:
        c.rect(7, 17 + dy, 2, 1, OUTLINE)


def head_side(c, p, dy=0):
    c.rect(5, 8 + dy, 7, 1, p.skin)
    c.rect(4, 9 + dy, 9, 9, p.skin)
    c.rect(5, 18 + dy, 7, 1, p.skin)
    c.set(13, 15 + dy, p.skin)  # nose
    c.set(10, 14 + dy, OUTLINE)
    c.set(10, 15 + dy, OUTLINE)
    c.set(11, 16 + dy, BLUSH)


def torso_front(c, p, dy=0, arms=True, arm_swing=0):
    c.rect(4, 19 + dy, 8, 7, p.coat)
    c.rect(10, 20 + dy, 2, 6, p.coat_s)
    c.rect(4, 25 + dy, 8, 1, p.coat_s)
    # coat buttons
    c.set(8, 22 + dy, p.coat_s)
    c.set(8, 24 + dy, p.coat_s)
    # scarf
    c.rect(4, 19 + dy, 8, 2, p.accent)
    c.rect(4, 20 + dy, 8, 1, p.accent_s)
    c.rect(9, 21 + dy, 2, 2, p.accent)
    c.set(10, 22 + dy, p.accent_s)
    if arms:
        for side, x in ((-1, 3), (1, 12)):
            off = arm_swing * side
            c.rect(x, 20 + dy + max(0, off), 1, 5, p.coat_s if x == 12 else p.coat)
            c.set(x, 25 + dy + max(0, off), p.skin)


def legs_front(c, p, dy=0, step=0):
    """step: 0 both down, -1 left leg lifted, 1 right leg lifted."""
    for i, x in enumerate((5, 8)):
        lift = 1 if (step == -1 and i == 0) or (step == 1 and i == 1) else 0
        c.rect(x, 26 + dy, 3, 3 - lift, p.pants)
        c.set(x + 2 if i == 1 else x, 26 + dy, p.pants_s)
        c.rect(x, 29 + dy - lift, 3, 2, p.boots)


def torso_back(c, p, dy=0, arm_swing=0):
    c.rect(4, 19 + dy, 8, 7, p.coat)
    c.rect(4, 20 + dy, 2, 6, p.coat_s)
    c.rect(4, 25 + dy, 8, 1, p.coat_s)
    c.rect(4, 19 + dy, 8, 1, p.accent)
    for side, x in ((-1, 3), (1, 12)):
        off = arm_swing * side
        c.rect(x, 20 + dy + max(0, off), 1, 5, p.coat_s if x == 3 else p.coat)
        c.set(x, 25 + dy + max(0, off), p.skin)


def head_back(c, p, dy=0):
    head_front(c, p, dy)


def torso_side(c, p, dy=0, arm=0):
    c.rect(5, 19 + dy, 6, 7, p.coat)
    c.rect(5, 25 + dy, 6, 1, p.coat_s)
    c.rect(5, 19 + dy, 6, 2, p.accent)
    c.rect(5, 20 + dy, 6, 1, p.accent_s)
    c.rect(4, 21 + dy, 2, 2, p.accent)  # scarf tail behind
    # arm: 0 down, 1 forward, -1 back
    if arm == 0:
        c.rect(7, 21 + dy, 2, 4, p.coat_s)
        c.rect(7, 25 + dy, 2, 1, p.skin)
    elif arm == 1:
        c.rect(8, 21 + dy, 2, 3, p.coat_s)
        c.rect(10, 23 + dy, 1, 1, p.coat_s)
        c.rect(11, 23 + dy, 1, 1, p.skin)
    else:
        c.rect(6, 21 + dy, 2, 3, p.coat_s)
        c.rect(4, 23 + dy, 2, 1, p.coat_s)
        c.rect(3, 23 + dy, 1, 1, p.skin)


def legs_side(c, p, dy=0, step=0):
    if step == 0:
        c.rect(6, 26 + dy, 4, 3, p.pants)
        c.rect(6, 26 + dy, 1, 3, p.pants_s)
        c.rect(6, 29 + dy, 5, 2, p.boots)
    elif step == 1:
        c.rect(8, 26 + dy, 3, 3, p.pants)
        c.rect(9, 29 + dy, 4, 2, p.boots)
        c.rect(5, 26 + dy, 2, 3, p.pants_s)
        c.rect(3, 28 + dy, 3, 2, p.boots)
    else:
        c.rect(5, 26 + dy, 3, 3, p.pants_s)
        c.rect(4, 29 + dy, 4, 2, p.boots)
        c.rect(8, 26 + dy, 2, 2, p.pants)
        c.rect(9, 28 + dy, 3, 2, p.boots)


def outline(c):
    marks = []
    for y in range(c.h):
        for x in range(c.w):
            if c.get(x, y) is not None:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                v = c.get(x + dx, y + dy)
                if v is not None and v != OUTLINE:
                    marks.append((x, y))
                    break
    for x, y in marks:
        c.set(x, y, OUTLINE)


def legends(p):
    hair = {"h": p.hair, "H": p.hair_s, "L": p.hair_l, "a": p.accent, "A": p.accent_s, "w": WHITE, "o": OUTLINE}
    hat = {"a": p.accent, "A": p.accent_s, "w": WHITE, "o": OUTLINE}
    return hair, hat


def glasses_front(c, dy=0):
    for x0 in (4, 9):
        c.rect(x0, 13 + dy, 3, 1, OUTLINE)
        c.set(x0, 14 + dy, OUTLINE)
        c.set(x0 + 2, 14 + dy, OUTLINE)
        c.set(x0 + 1, 14 + dy, WHITE)
        c.rect(x0, 15 + dy, 3, 1, OUTLINE)
        c.set(x0 + 1, 15 + dy, OUTLINE)
    c.rect(7, 14 + dy, 2, 1, OUTLINE)


def glasses_side(c, dy=0):
    c.rect(9, 13 + dy, 3, 1, OUTLINE)
    c.set(9, 14 + dy, OUTLINE)
    c.set(11, 14 + dy, OUTLINE)
    c.rect(9, 15 + dy, 3, 1, OUTLINE)
    c.rect(5, 14 + dy, 4, 1, OUTLINE)


def draw_head(c, p, view, style, extras, dy=0, mouth=False):
    hair_leg, hat_leg = legends(p)
    hats = [e for e in extras if e in HATS]
    if view == "down":
        head_front(c, p, dy)
        face_front(c, p, dy, mouth)
    elif view == "up":
        head_back(c, p, dy)
    else:
        head_side(c, p, dy)
    mask = HAIR_MASKS[style][view]
    if hats and hats[0] in ("beanie", "santa", "cap") and style == "spiky":
        mask = HAIR_MASKS["short"][view]
    apply_mask(c, mask, hair_leg, dy=dy)
    if "glasses" in extras:
        if view == "down":
            glasses_front(c, dy)
        elif view == "side":
            glasses_side(c, dy)
    for h in hats:
        apply_mask(c, HATS[h][view], hat_leg, dy=dy)


def frame(p, style, extras, kind):
    c = Canvas(16, 32)
    if kind in ("down", "down_a", "down_b"):
        step = {"down": 0, "down_a": -1, "down_b": 1}[kind]
        bob = 0 if step == 0 else 1
        legs_front(c, p, 0, step)
        torso_front(c, p, bob, arm_swing=step)
        draw_head(c, p, "down", style, extras, bob)
    elif kind in ("up", "up_a", "up_b"):
        step = {"up": 0, "up_a": -1, "up_b": 1}[kind]
        bob = 0 if step == 0 else 1
        legs_front(c, p, 0, step)
        torso_back(c, p, bob, arm_swing=step)
        draw_head(c, p, "up", style, extras, bob)
    elif kind in ("side", "side_a", "side_b"):
        step = {"side": 0, "side_a": 1, "side_b": -1}[kind]
        bob = 0 if step == 0 else 1
        legs_side(c, p, 0, step)
        torso_side(c, p, bob, arm=-step)
        draw_head(c, p, "side", style, extras, bob)
    elif kind == "sit":
        # Sitting on a sled, facing the viewer: legs stick out towards us.
        dy = 3
        c.rect(4, 26 + dy - 1, 8, 2, p.pants)
        c.rect(4, 27 + dy - 1, 8, 1, p.pants_s)
        c.rect(3, 28 + dy - 1, 3, 2, p.boots)
        c.rect(10, 28 + dy - 1, 3, 2, p.boots)
        torso_front(c, p, dy, arms=False)
        c.rect(2, 22 + dy, 2, 3, p.coat)
        c.rect(12, 22 + dy, 2, 3, p.coat_s)
        c.set(2, 25 + dy, p.skin)
        c.set(13, 25 + dy, p.skin)
        draw_head(c, p, "down", style, extras, dy)
    elif kind == "side_sit":
        dy = 2
        torso_side(c, p, dy, arm=1)
        c.rect(6, 25 + dy, 6, 2, p.pants)
        c.rect(6, 26 + dy, 6, 1, p.pants_s)
        c.rect(10, 27 + dy, 2, 1, p.pants)
        c.rect(10, 28 + dy, 3, 2, p.boots)
        draw_head(c, p, "side", style, extras, dy)
    elif kind == "cheer":
        legs_front(c, p, 0, 0)
        torso_front(c, p, 0, arms=False)
        c.rect(2, 13, 1, 7, p.coat)
        c.rect(13, 13, 1, 7, p.coat_s)
        c.rect(2, 11, 1, 2, p.skin)
        c.rect(13, 11, 1, 2, p.skin)
        draw_head(c, p, "down", style, extras, 0, mouth=True)
    outline(c)
    return c


KINDS = ["down", "down_a", "down_b", "up", "up_a", "up_b", "side", "side_a", "side_b", "sit", "side_sit", "cheer"]


def sheet(entry):
    name, skin, hair, style, coat, pants, accent, extras = entry
    p = Pal(skin, hair, coat, pants, accent)
    s = Canvas(16, 32 * len(KINDS))
    for i, k in enumerate(KINDS):
        s.blit(frame(p, style, extras, k), 0, 32 * i)
    return s


def main():
    os.makedirs(OUT, exist_ok=True)
    preview = Canvas(16 * len(KINDS) + len(KINDS), 33 * len(CHARACTERS))
    for ci, entry in enumerate(CHARACTERS):
        s = sheet(entry)
        n = len(s.colors())
        if n > 15:
            sys.exit(f"{entry[0]}: {n} colours, max 15")
        fname = f"chr_{entry[0].lower()}"
        s.save_bmp(os.path.join(OUT, fname + ".bmp"), bpp=4)
        write_json(os.path.join(OUT, fname + ".json"), '{\n    "type": "sprite",\n    "height": 32\n}')
        for i in range(len(KINDS)):
            preview.blit(s.crop(0, 32 * i, 16, 32) if hasattr(s, "crop") else _crop(s, i), i * 17, ci * 33)
    if len(sys.argv) > 1:
        save_png(sys.argv[1], preview, scale=4)
    print(f"Wrote {len(CHARACTERS)} character sheets to {os.path.normpath(OUT)}")


def _crop(s, i):
    c = Canvas(16, 32)
    for y in range(32):
        for x in range(16):
            c.set(x, y, s.get(x, 32 * i + y))
    return c


if __name__ == "__main__":
    main()
