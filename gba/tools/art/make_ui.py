"""Generates the font and the UI box tiles in gba/graphics/ui/.

Font: 94 glyphs (ASCII 33..126; Butano draws the space itself), each 8x16, stacked vertically
-> font.bmp (8x1504).
Glyph pixels use palette index 1, the drop shadow index 2. Recolour text at runtime by
swapping in one of the font_pal_*.bmp palettes.
Some ASCII characters we never type are drawn as icons instead:
  @ heart   { star   } music note   | present   ~ snowflake
  > right arrow (menu cursor)   < left arrow   ^ up arrow   _ down arrow
The widths (in pixels) are written to include/sb_font_widths.h.
"""
import os
from canvas import Canvas, rgb, write_json

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
OUT = os.path.join(ROOT, "graphics", "ui")

G = {
    "A": ".###.|#...#|#...#|#####|#...#|#...#|#...#",
    "B": "####.|#...#|#...#|####.|#...#|#...#|####.",
    "C": ".###.|#...#|#....|#....|#....|#...#|.###.",
    "D": "###..|#..#.|#...#|#...#|#...#|#..#.|###..",
    "E": "#####|#....|#....|####.|#....|#....|#####",
    "F": "#####|#....|#....|####.|#....|#....|#....",
    "G": ".###.|#...#|#....|#.###|#...#|#...#|.####",
    "H": "#...#|#...#|#...#|#####|#...#|#...#|#...#",
    "I": "###|.#.|.#.|.#.|.#.|.#.|###",
    "J": "..###|...#.|...#.|...#.|#..#.|#..#.|.##..",
    "K": "#...#|#..#.|#.#..|##...|#.#..|#..#.|#...#",
    "L": "#....|#....|#....|#....|#....|#....|#####",
    "M": "#...#|##.##|#.#.#|#.#.#|#...#|#...#|#...#",
    "N": "#...#|##..#|#.#.#|#.#.#|#..##|#...#|#...#",
    "O": ".###.|#...#|#...#|#...#|#...#|#...#|.###.",
    "P": "####.|#...#|#...#|####.|#....|#....|#....",
    "Q": ".###.|#...#|#...#|#...#|#.#.#|#..#.|.##.#",
    "R": "####.|#...#|#...#|####.|#.#..|#..#.|#...#",
    "S": ".####|#....|#....|.###.|....#|....#|####.",
    "T": "#####|..#..|..#..|..#..|..#..|..#..|..#..",
    "U": "#...#|#...#|#...#|#...#|#...#|#...#|.###.",
    "V": "#...#|#...#|#...#|#...#|#...#|.#.#.|..#..",
    "W": "#...#|#...#|#...#|#.#.#|#.#.#|##.##|#...#",
    "X": "#...#|#...#|.#.#.|..#..|.#.#.|#...#|#...#",
    "Y": "#...#|#...#|.#.#.|..#..|..#..|..#..|..#..",
    "Z": "#####|....#|...#.|..#..|.#...|#....|#####",
    "0": ".###.|#...#|#..##|#.#.#|##..#|#...#|.###.",
    "1": ".#.|##.|.#.|.#.|.#.|.#.|###",
    "2": ".###.|#...#|....#|...#.|..#..|.#...|#####",
    "3": "####.|....#|....#|.###.|....#|....#|####.",
    "4": "...#.|..##.|.#.#.|#..#.|#####|...#.|...#.",
    "5": "#####|#....|####.|....#|....#|#...#|.###.",
    "6": "..##.|.#...|#....|####.|#...#|#...#|.###.",
    "7": "#####|....#|...#.|..#..|.#...|.#...|.#...",
    "8": ".###.|#...#|#...#|.###.|#...#|#...#|.###.",
    "9": ".###.|#...#|#...#|.####|....#|...#.|.##..",
    "a": ".....|.....|.###.|....#|.####|#...#|.####",
    "b": "#....|#....|####.|#...#|#...#|#...#|####.",
    "c": ".....|.....|.###.|#....|#....|#....|.###.",
    "d": "....#|....#|.####|#...#|#...#|#...#|.####",
    "e": ".....|.....|.###.|#...#|#####|#....|.###.",
    "f": "..##|.#..|####|.#..|.#..|.#..|.#..",
    "g": ".....|.....|.####|#...#|#...#|#...#|.####|....#|.###.",
    "h": "#....|#....|####.|#...#|#...#|#...#|#...#",
    "i": "#|.|#|#|#|#|#",
    "j": "..#|...|..#|..#|..#|..#|..#|#.#|.#.",
    "k": "#...|#...|#..#|#.#.|##..|#.#.|#..#",
    "l": "##.|.#.|.#.|.#.|.#.|.#.|..#",
    "m": ".....|.....|##.#.|#.#.#|#.#.#|#.#.#|#.#.#",
    "n": ".....|.....|####.|#...#|#...#|#...#|#...#",
    "o": ".....|.....|.###.|#...#|#...#|#...#|.###.",
    "p": ".....|.....|####.|#...#|#...#|#...#|####.|#....|#....",
    "q": ".....|.....|.####|#...#|#...#|#...#|.####|....#|....#",
    "r": "....|....|#.##|##..|#...|#...|#...",
    "s": ".....|.....|.####|#....|.###.|....#|####.",
    "t": ".#..|.#..|####|.#..|.#..|.#..|..##",
    "u": ".....|.....|#...#|#...#|#...#|#...#|.####",
    "v": ".....|.....|#...#|#...#|#...#|.#.#.|..#..",
    "w": ".....|.....|#...#|#.#.#|#.#.#|#.#.#|.#.#.",
    "x": ".....|.....|#...#|.#.#.|..#..|.#.#.|#...#",
    "y": ".....|.....|#...#|#...#|#...#|#...#|.####|....#|.###.",
    "z": ".....|.....|#####|...#.|..#..|.#...|#####",
    "!": "#|#|#|#|#|.|#",
    '"': "#.#|#.#",
    "#": ".....|.#.#.|#####|.#.#.|#####|.#.#.",
    "$": "..#..|.####|#.#..|.###.|..#.#|####.|..#..",
    "%": "##..#|##.#.|..#..|.#...|#.##.|...##",
    "&": ".##..|#..#.|.##..|.#...|#.#.#|#..#.|.##.#",
    "'": "#|#",
    "(": ".#|#.|#.|#.|#.|#.|.#",
    ")": "#.|.#|.#|.#|.#|.#|#.",
    "*": "...|#.#|.#.|#.#",
    "+": ".....|..#..|..#..|#####|..#..|..#..",
    ",": "..|..|..|..|..|.#|.#|#.",
    "-": "....|....|....|####",
    ".": ".|.|.|.|.|.|#",
    "/": "....#|...#.|...#.|..#..|.#...|.#...|#....",
    ":": ".|.|#|.|.|#",
    ";": "..|..|.#|..|..|.#|#.",
    "=": "....|....|####|....|####",
    "?": ".###.|#...#|....#|...#.|..#..|.....|..#..",
    "[": "##|#.|#.|#.|#.|#.|##",
    "\\": "#....|.#...|.#...|..#..|...#.|...#.|....#",
    "]": "##|.#|.#|.#|.#|.#|##",
    "`": "#.|.#",
    # Icons
    "@": ".##.##.|#######|#######|.#####.|..###..|...#...",
    "{": "...#...|...#...|#######|.#####.|..###..|.##.##.|.#...#.",
    "}": "...##.|...#.#|...#..|...#..|.###..|####..|.##...",
    "|": "..#.#..|...#...|#######|#..#..#|#..#..#|#..#..#|#######",
    "~": "...#...|.#.#.#.|..###..|#######|..###..|.#.#.#.|...#...",
    ">": "#...|##..|###.|####|###.|##..|#...",
    "<": "...#|..##|.###|####|.###|..##|...#",
    "^": ".......|.......|...#...|..###..|.#####.|#######",
    "_": "#######|.#####.|..###..|...#...",
}

GLYPH_TOP = 3  # first glyph row inside the 16px cell


def glyph_width(ch):
    if ch == " ":
        return 4
    rows = G[ch].split("|")
    w = max(len(r) for r in rows)
    return min(8, w + 2)  # glyph + shadow column + 1px gap


def make_font():
    font = Canvas(8, 16 * 94)
    ink, shadow = 1, 2
    widths = []
    for i in range(95):
        ch = chr(32 + i)
        widths.append(glyph_width(ch))
        if ch == " ":
            continue
        rows = G[ch].split("|")
        cell = Canvas(8, 16)
        for y, row in enumerate(rows):
            for x, c in enumerate(row):
                if c == "#":
                    cell.set(x, GLYPH_TOP + y, ink)
        # Drop shadow to the right, below and diagonally (like the text in Pokemon-era RPGs).
        for y in range(16):
            for x in range(8):
                if cell.get(x, y) is None and (
                        cell.get(x - 1, y) == ink or cell.get(x, y - 1) == ink or cell.get(x - 1, y - 1) == ink):
                    cell.set(x, y, shadow)
        for y in range(16):
            for x in range(8):
                v = cell.get(x, y)
                if v is not None:
                    font.set(x, (i - 1) * 16 + y, v)
    return font, widths


# Text colour palettes: (ink, shadow).
TEXT_PALETTES = {
    "dark": (rgb("#383838"), rgb("#d0d0c8")),     # default: dark text for white boxes
    "white": (rgb("#f8f8f8"), rgb("#282838")),    # HUD text over game scenes
    "yellow": (rgb("#f8e030"), rgb("#683800")),   # highlights / new record
    "red": (rgb("#e83030"), rgb("#f8c0c0")),      # warnings, on white boxes
    "blue": (rgb("#2858c8"), rgb("#c0d0f0")),     # names, on white boxes
    "green": (rgb("#208838"), rgb("#b8e8b0")),    # good things, on white boxes
}


def save_font():
    font, widths = make_font()
    ink, shadow = TEXT_PALETTES["dark"]
    # Replace the index markers with the real colours.
    font.px = [None if v is None else (ink if v == 1 else shadow) for v in font.px]
    font.save_bmp(os.path.join(OUT, "font.bmp"), bpp=4, palette=[ink, shadow])
    write_json(os.path.join(OUT, "font.json"), '{\n    "type": "sprite",\n    "height": 16\n}')
    for name, (ink, shadow) in TEXT_PALETTES.items():
        c = Canvas(8, 8)
        c.set(0, 0, ink)
        c.set(1, 0, shadow)
        c.save_bmp(os.path.join(OUT, f"font_pal_{name}.bmp"), bpp=4, palette=[ink, shadow])
        write_json(os.path.join(OUT, f"font_pal_{name}.json"),
                   '{\n    "type": "sprite_palette",\n    "bpp_mode": "bpp_4"\n}')
    lines = ["// Generated by tools/art/make_ui.py. Pixel width of each font character (ASCII 32..126).",
             "#ifndef SB_FONT_WIDTHS_H", "#define SB_FONT_WIDTHS_H", "", "#include <stdint.h>", "",
             "namespace sb", "{", "    constexpr int8_t font_widths[] = {"]
    for i, w in enumerate(widths):
        ch = chr(32 + i)
        lines.append(f"        {w},  // {ch!r}")
    lines += ["    };", "}", "", "#endif", ""]
    with open(os.path.join(ROOT, "include", "sb_font_widths.h"), "w") as f:
        f.write("\n".join(lines))


# ---------------------------------------------------------------- UI box tiles
BOX_PAL = [rgb("#f8f8f8"), rgb("#384878"), rgb("#88b0e8"), rgb("#e05050"), rgb("#f8f0c8")]
# indices:   1 fill       2 border dark   3 border light  4 accent red    5 cream fill


def box_tiles():
    """Tiles (8x8 each, stacked vertically):
    0 empty, 1 top-left, 2 top, 3 top-right, 4 left, 5 fill, 6 right, 7 bottom-left, 8 bottom, 9 bottom-right,
    10 cream fill, 11 red holly-berry corner decoration."""
    W, D, L = BOX_PAL[0], BOX_PAL[1], BOX_PAL[2]
    t = Canvas(8, 8 * 12)

    def tile(i, rows, legend):
        t.ascii(0, i * 8, rows, legend)

    leg = {"w": W, "d": D, "l": L}
    tl = ["..dddddd", ".dllllll", "dlwwwwww", "dlwwwwww", "dlwwwwww", "dlwwwwww", "dlwwwwww", "dlwwwwww"]
    top = ["dddddddd", "llllllll"] + ["wwwwwwww"] * 6
    left = ["dlwwwwww"] * 8

    def mirror_x(rows):
        return [r[::-1] for r in rows]

    def mirror_y(rows):
        return rows[::-1]

    tile(1, tl, leg)
    tile(2, top, leg)
    tile(3, mirror_x(tl), leg)
    tile(4, left, leg)
    tile(5, ["wwwwwwww"] * 8, leg)
    tile(6, mirror_x(left), leg)
    tile(7, mirror_y(tl), leg)
    tile(8, mirror_y(top), leg)
    tile(9, mirror_y(mirror_x(tl)), leg)
    tile(10, ["cccccccc"] * 8, {"c": BOX_PAL[4]})
    tile(11, ["..dddddd", ".dllllll", "dlwrrwww", "dlrrrrgw", "dlwrrggw", "dlwwgggw", "dlwwwwww", "dlwwwwww"],
         {"w": W, "d": D, "l": L, "r": BOX_PAL[3], "g": rgb("#30a050")})
    return t


def save_box():
    pal = BOX_PAL + [rgb("#30a050")]
    t = box_tiles()
    t.save_bmp(os.path.join(OUT, "box_tiles.bmp"), bpp=4, palette=pal)
    write_json(os.path.join(OUT, "box_tiles.json"), '{\n    "type": "regular_bg_tiles",\n    "bpp_mode": "bpp_4"\n}')
    p = Canvas(8, 8)
    p.rect(0, 0, 8, 8, pal[0])
    p.save_bmp(os.path.join(OUT, "box_palette.bmp"), bpp=4, palette=pal)
    write_json(os.path.join(OUT, "box_palette.json"), '{\n    "type": "bg_palette",\n    "bpp_mode": "bpp_4"\n}')


# ---------------------------------------------------------------- snowflakes
def save_snow():
    W, B = rgb("#f8f8f8"), rgb("#b8d0f0")
    c = Canvas(8, 24)
    c.ascii(0, 0, ["........", "...w....", ".w.w.w..", "..wbw...", "wwbwbww.", "..wbw...", ".w.w.w..",
                   "...w...."], {"w": W, "b": B})
    c.ascii(0, 8, ["........", "........", "...ww...", "..wwww..", "..wwwb..", "...wb...", "........",
                   "........"], {"w": W, "b": B})
    c.ascii(0, 16, ["........", "........", "........", "...w....", "..wwb...", "...b....", "........",
                    "........"], {"w": W, "b": B})
    c.save_bmp(os.path.join(OUT, "snow.bmp"), bpp=4)
    write_json(os.path.join(OUT, "snow.json"), '{\n    "type": "sprite",\n    "height": 8\n}')


# ---------------------------------------------------------------- title logo
def save_logo():
    """'Snowberry' in big letters (the font at 3x) with snow on top, as a 256x256 background."""
    red, dark, white, snow_s = rgb("#e03838"), rgb("#702030"), rgb("#f8f8f8"), rgb("#c8d8f0")
    green, gold = rgb("#30a050"), rgb("#f8d040")
    c = Canvas(256, 256)
    # THE GAME'S NAME lives in tools/art/game_title.txt (one line). Change it there and re-run this script.
    # (The name inside the ROM file itself is ROMTITLE in the Makefile, max 12 capital letters.)
    with open(os.path.join(os.path.dirname(__file__), "game_title.txt")) as f:
        text = f.read().strip()
    scale = 3
    x = 0
    widths = [glyph_width(ch) - 1 for ch in text]
    total = sum(w * scale for w in widths)
    x = (256 - total) // 2
    y0 = 20
    for ch, w in zip(text, widths):
        rows = G[ch].split("|")
        for gy, row in enumerate(rows):
            for gx, px in enumerate(row):
                if px == "#":
                    c.rect(x + gx * scale, y0 + gy * scale, scale, scale, red)
        x += w * scale
    # Outline (dark), then white rim outside it.
    c.outline_color(red, dark)
    for _ in range(1):
        marks = [(xx, yy) for yy in range(c.h) for xx in range(c.w)
                 if c.get(xx, yy) is None and any(c.get(xx + dx, yy + dy) == dark
                                                   for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))]
        for xx, yy in marks:
            c.set(xx, yy, white)
    # Shine stripe on the letters and snow caps on top edges.
    for yy in range(c.h):
        for xx in range(c.w):
            if c.get(xx, yy) == red and c.get(xx, yy - 1) in (dark,) and c.get(xx, yy - 2) in (white, None):
                c.set(xx, yy - 1, white)
                c.set(xx, yy, snow_s if (xx // 2) % 3 == 0 else white)
    # Little holly sprig over the 'o'.
    hx = (256 - total) // 2 + (widths[0] + widths[1]) * scale + 4
    for dx, dy, col in ((0, 0, green), (1, 0, green), (2, 1, green), (-1, 1, green), (3, 1, green), (1, 2, gold),
                        (0, 3, red), (2, 3, red)):
        pass
    c.save_bmp(os.path.join(OUT, "title_logo.bmp"), bpp=4)
    write_json(os.path.join(OUT, "title_logo.json"), '{\n    "type": "regular_bg"\n}')


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    save_font()
    save_box()
    save_snow()
    save_logo()
    print("UI art written to", os.path.normpath(OUT))
