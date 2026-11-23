"""Generates the Snowball Fight art in gba/graphics/snowball/.

    python3 tools/art/make_snowball.py [PREVIEW_DIR]   (PREVIEW_DIR: also write scaled PNG previews there)

The scene is three backgrounds (each 256x256, the screen is pixels 8..247 x 48..207 of it):
  snowball_yard    the back: dusk sky, hills, the family's houses, the snowy yard        (BG priority 3)
  snowball_cover   the cover the family hides behind: snow drifts (far), snow-brick walls (middle),
                   big drifts with a snowman (near). Family members are sprites drawn BETWEEN the yard
                   and this layer, so they disappear behind it.                          (BG priority 2)
  snowball_fort    your own big snow fort across the bottom of the screen               (BG priority 1)
Sprites (all share ONE palette, FX_PAL, so they use a single sprite palette slot):
  snowball_ball8   8x8    0 a far snowball, 1 a bit nearer
  snowball_ball16  16x16  0, 1 an incoming snowball getting closer
  snowball_ball32  32x32  0, 1 an incoming snowball about to hit you
  snowball_splat   16x16  0-2 a snowball bursting
  snowball_face    64x64  0 snow splatted on the screen when you're hit, 1 sliding off
  snowball_aim     16x16  0 the aiming cross, 1 aiming at someone (red)
  snowball_tell    8x16   0, 1 the "!" over someone about to throw
  snowball_stars   16x16  0, 1 dizzy stars after a hit
  snowball_icons   8x8    0 heart, 1 empty heart, 2 snowball (ammo), 3 empty ammo spot

The cover spots (keep in sync with SPOTS in src/games/snowball.cpp): x centre and top y in screen pixels.
"""
import math
import os
import sys

from canvas import Canvas, rgb, write_json, save_png

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
OUT = os.path.join(ROOT, "graphics", "snowball")

OUTLINE = rgb("#283048")
WHITE = rgb("#f8f8f8")
SNOW, SNOW_S, SNOW_D, SNOW_DD = rgb("#f0f4fc"), rgb("#d0dcf0"), rgb("#a8bce0"), rgb("#8098c8")
SKY = [rgb(h) for h in ("#283868", "#344880", "#405898", "#5070b0", "#6888c0", "#88a0d0", "#a8b8e0")]
HILL, HILL_S = rgb("#b8c8e8"), rgb("#98acd8")
PINE, PINE_S = rgb("#2c6850"), rgb("#1c4838")
WALL, WALL_S, ROOF, ROOF_S, WIN = rgb("#e8d8c0"), rgb("#c0a888"), rgb("#c84848"), rgb("#902830"), rgb("#f8d860")
CARROT, COAL, SCARF = rgb("#f08030"), rgb("#303038"), rgb("#d83840")
RED, RED_S, GOLD, GOLD_S = rgb("#e83838"), rgb("#a02028"), rgb("#f8d040"), rgb("#c89020")
ICE = rgb("#88c0f0")

SCREEN_X, SCREEN_Y = 8, 48        # where the 240x160 screen sits in the 256x256 backgrounds

# Cover spots: (kind, x centre, top y) in screen pixels. Family members pop up from behind them.
SPOTS = [("far", 44, 52), ("far", 120, 50), ("far", 196, 52),
         ("mid", 74, 76), ("mid", 166, 76),
         ("near", 26, 96), ("near", 214, 96)]
FORT_TOP = 118                     # top of your fort wall's battlements (screen y)

FX_PAL = [OUTLINE, WHITE, SNOW, SNOW_S, SNOW_D, SNOW_DD, ICE, RED, RED_S, GOLD, GOLD_S, rgb("#f8a0a0")]


def outline(c, col=OUTLINE):
    marks = []
    for y in range(c.h):
        for x in range(c.w):
            if c.get(x, y) is not None:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if c.get(x + dx, y + dy) is not None and c.get(x + dx, y + dy) != col:
                    marks.append((x, y))
                    break
    for x, y in marks:
        c.set(x, y, col)


def disc(c, cx, cy, r, col):
    for y in range(int(cy - r) - 1, int(cy + r) + 2):
        for x in range(int(cx - r) - 1, int(cx + r) + 2):
            if (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= r * r:
                c.set(x, y, col)


def shaded_ball(c, cx, cy, r):
    """A snowball: white with a blue shadow at the bottom right."""
    for y in range(int(cy - r) - 1, int(cy + r) + 2):
        for x in range(int(cx - r) - 1, int(cx + r) + 2):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            if dx * dx + dy * dy <= r * r:
                shade = (dx + dy) / max(r, 1)
                c.set(x, y, WHITE if shade < 0.2 else SNOW_S if shade < 0.9 else SNOW_D)


# ============================================================================ backgrounds
def make_yard():
    c = Canvas(256, 256, SKY[-1])
    s = Canvas(240, 160)
    # Sky bands (dusk, darker at the top) with a checker dither between them.
    for y in range(0, 52):
        band = min(len(SKY) - 1, y // 8)
        col = SKY[band]
        if y % 8 >= 6 and band + 1 < len(SKY) and (y + 0) % 2 == 0:
            col = SKY[band + 1]
        for x in range(240):
            s.set(x, y, col if not (y % 8 == 7 and x % 2) else SKY[min(band + 1, len(SKY) - 1)])
    for (sx, sy) in ((20, 6), (60, 14), (100, 4), (150, 10), (190, 3), (226, 16), (130, 22), (40, 28)):
        s.set(sx, sy, WHITE)
    disc(s, 200, 14, 6, rgb("#f8f0c8"))            # the moon
    disc(s, 203, 12, 5, SKY[1])
    # Far hills.
    for x in range(240):
        h = 40 + int(6 * math.sin(x / 23) + 4 * math.sin(x / 9 + 1))
        for y in range(h, 60):
            s.set(x, y, HILL if y > h + 1 else WHITE)
    # The family's houses on the far side of the yard.
    for hx, roof in ((10, ROOF), (96, rgb("#4870c0")), (182, rgb("#40a060"))):
        s.rect(hx, 34, 40, 16, WALL)
        s.rect(hx, 46, 40, 4, WALL_S)
        for i in range(10):
            s.hline(hx - 3 + i, hx + 42 - i, 33 - i, roof if i % 3 else ROOF_S if roof == ROOF else roof)
        s.hline(hx - 3, hx + 42, 34, SNOW)
        s.hline(hx + 4, hx + 35, 24, SNOW)
        for wx in (hx + 5, hx + 27):
            s.rect(wx, 37, 7, 6, WIN)
        s.rect(hx + 17, 39, 6, 11, rgb("#704830"))
    # Pines between the houses.
    for px, h in ((62, 24), (76, 18), (148, 22), (164, 16), (228, 20)):
        for i in range(h):
            half = i * 5 // 12
            s.hline(px - half, px + half, 50 - h + i, PINE if i % 5 else PINE_S)
            if i % 5 == 0:
                s.hline(px - half, px + half, 50 - h + i, SNOW)
    # The yard: snow with soft furrows getting wider apart towards you.
    for y in range(50, 160):
        for x in range(240):
            depth = y - 50
            col = SNOW_S if depth < 14 and (x + y) % 2 else SNOW
            if depth % (4 + depth // 10) == 0:
                col = SNOW_D if depth < 20 else SNOW_S
            s.set(x, y, col)
    for (fx, fy) in ((30, 70), (90, 64), (140, 88), (200, 70), (60, 104), (180, 110), (120, 66)):
        s.hline(fx, fx + 6, fy, SNOW_D)              # footprints and dents
        s.hline(fx + 10, fx + 15, fy + 2, SNOW_D)
    c.blit(s, SCREEN_X, SCREEN_Y)
    return c, s


def drift(c, cx, top, w, h):
    """A rounded snow drift `w` wide whose top is at `top`, `h` tall: a soft mound, lit from the top left,
    with a blue shadow on its right and along the ground."""
    half = w / 2
    for x in range(int(cx - half), int(cx + half)):
        u = (x + 0.5 - cx) / half                       # -1 .. 1
        bump = 0.12 * math.sin(u * 5.0 + cx)            # a slightly lumpy top
        height = h * max(0.0, (1 - u * u)) ** 0.45 * (1 + bump)
        y_top = int(top + h - height)
        for y in range(max(top, y_top), top + h):
            depth = y - y_top
            if depth < 2 and u < 0.3:
                col = WHITE
            elif u > 0.45 or y > top + h - 3:
                col = SNOW_D if u > 0.75 else SNOW_S
            else:
                col = SNOW
            c.set(x, y, col)
    for x in range(int(cx - half) + 2, int(cx + half) + 4):        # shadow on the ground
        if c.get(x, top + h) is not None:
            c.set(x, top + h, SNOW_D)


def brick_wall(c, cx, top, w, h):
    """A wall of packed snow blocks."""
    x0 = cx - w // 2
    for y in range(top, top + h):
        row = (y - top) // 6
        for x in range(x0, x0 + w):
            edge = (y - top) % 6 == 5 or (x - x0 + (row % 2) * 6) % 12 == 0
            c.set(x, y, SNOW_D if edge else WHITE if (y - top) % 6 == 0 else SNOW)
    for x in range(x0, x0 + w):                 # snow piled on top
        c.set(x, top - 1, WHITE)
        if (x * 7) % 5 < 3:
            c.set(x, top - 2, WHITE)


def snowman(c, cx, base):
    disc(c, cx, base - 7, 7.5, WHITE)
    disc(c, cx, base - 19, 5.5, WHITE)
    disc(c, cx, base - 28, 4.5, WHITE)
    for (dx, dy) in ((4, -5), (5, -8), (3, -2)):
        c.set(cx + dx, base + dy, SNOW_S)
    c.hline(cx - 4, cx + 4, base - 24, SCARF)
    c.set(cx + 3, base - 23, SCARF)
    c.set(cx + 3, base - 22, SCARF)
    c.set(cx - 2, base - 30, COAL)
    c.set(cx + 2, base - 30, COAL)
    c.hline(cx, cx + 4, base - 28, CARROT)
    for dy in (-20, -17, -14):
        c.set(cx, base + dy, COAL)


# Below every cover there is a "skirt": a copy of the yard's own pixels, on the cover layer. It looks just like
# the yard, but it hides the legs of someone crouching behind the cover (the family are 32 px tall sprites).
SKIRT_TO = 34                     # the skirt reaches this far below the cover's top


def cover_width(kind):
    return {"far": 38, "mid": 50, "near": 46}[kind]


def sled(c, x, y):
    """A red sled leaning in the snow."""
    for i in range(14):
        c.set(x + i, y - i // 3, RED)
        c.set(x + i, y - i // 3 + 1, RED_S)
    c.hline(x - 1, x + 15, y + 3, rgb("#c89020"))
    c.set(x - 2, y + 2, rgb("#c89020"))


def shovel(c, x, y):
    c.vline(x, y - 16, y, rgb("#906038"))
    c.rect(x - 3, y, 7, 6, rgb("#5878a8"))
    c.rect(x - 2, y - 17, 5, 2, rgb("#906038"))


def make_cover(yard_screen):
    c = Canvas(256, 256)
    s = Canvas(240, 160)
    # 1. The skirts: copies of the yard below every cover.
    for kind, x, top in SPOTS:
        w = cover_width(kind)
        for y in range(top, min(160, top + SKIRT_TO)):
            for xx in range(x - w // 2, x + w // 2):
                if 0 <= xx < 240:
                    s.set(xx, y, yard_screen.get(xx, y))
    # 2. The covers themselves, outlined, on top.
    covers = Canvas(240, 160)
    for kind, x, top in SPOTS:
        if kind == "far":
            drift(covers, x, top, 38, 20)
        elif kind == "mid":
            brick_wall(covers, x, top, 50, 24)
        else:
            drift(covers, x, top, 46, 24)
    snowman(covers, 120, 110)                     # just a decoration in the middle of the yard
    outline(covers)
    s.blit(covers, 0, 0)
    sled(s, 54, 110)
    shovel(s, 232, 116)
    c.blit(s, SCREEN_X, SCREEN_Y)
    return c


def make_fort():
    c = Canvas(256, 256)
    s = Canvas(240, 160)
    wall_top = FORT_TOP + 8
    for y in range(wall_top, 160):
        row = (y - wall_top) // 7
        for x in range(240):
            edge = (y - wall_top) % 7 == 6 or (x + (row % 2) * 8) % 16 == 0
            s.set(x, y, SNOW_D if edge else WHITE if (y - wall_top) % 7 == 0 else SNOW)
    for x0 in range(0, 240, 20):                  # battlements, with a low gap in the middle for you
        if 104 <= x0 <= 120:
            continue
        for y in range(FORT_TOP, wall_top):
            for x in range(x0 + 2, x0 + 16):
                s.set(x, y, WHITE if y == FORT_TOP else SNOW if x < x0 + 13 else SNOW_S)
    for x in range(102, 138):                     # your low spot: a dip with snowball piles either side
        s.set(x, wall_top, WHITE)
    for (bx, by) in ((86, wall_top - 3), (92, wall_top - 3), (89, wall_top - 7),
                     (146, wall_top - 3), (152, wall_top - 3), (149, wall_top - 7)):
        shaded_ball(s, bx, by, 3)
    outline(s)
    c.blit(s, SCREEN_X, SCREEN_Y)
    return c


# ============================================================================ sprites
def frames(w, h, makers):
    sheet = Canvas(w, h * len(makers))
    for i, m in enumerate(makers):
        f = Canvas(w, h)
        m(f)
        sheet.blit(f, 0, h * i)
    return sheet


def ball_maker(size, r):
    def m(c):
        shaded_ball(c, size / 2, size / 2, r)
        outline(c)
    return m


def splat_maker(step):
    def m(c):
        r = 2 + step * 2
        for i in range(8):
            a = i * math.pi / 4 + step * 0.3
            d = r + 1 + (i % 2) * 2
            disc(c, 8 + math.cos(a) * d, 8 + math.sin(a) * d, 1.5 - step * 0.3, WHITE if i % 2 else SNOW_S)
        if step < 2:
            disc(c, 8, 8, r - step, WHITE)
        outline(c, SNOW_DD)
    return m


def face_maker(drip):
    def m(c):
        for i in range(12):
            a = i * math.pi / 6
            d = 18 + (i % 3) * 5
            disc(c, 32 + math.cos(a) * d * 0.9, 30 + math.sin(a) * d * 0.7, 5 + (i % 2) * 2, WHITE)
        disc(c, 32, 30, 19, WHITE)
        for (x, y, r) in ((24, 24, 4), (40, 34, 5), (30, 38, 3)):
            disc(c, x, y, r, SNOW_S)
        if drip:
            for x in (18, 30, 44):
                for y in range(40, 58 - (x % 7)):
                    c.set(x, y, SNOW)
                    c.set(x + 1, y, SNOW_S)
        outline(c, SNOW_D)
    return m


def aim_maker(hot):
    def m(c):
        col = RED if hot else WHITE
        shade = RED_S if hot else SNOW_D
        for i in range(16):
            dx, dy = i - 7.5, 0
        for y in range(16):
            for x in range(16):
                r = ((x - 7.5) ** 2 + (y - 7.5) ** 2) ** 0.5
                if 4.5 <= r <= 6.2:
                    c.set(x, y, col)
        for k in range(3):
            c.set(7, k, col)
            c.set(8, k, shade)
            c.set(7, 15 - k, col)
            c.set(8, 15 - k, shade)
            c.set(k, 7, col)
            c.set(k, 8, shade)
            c.set(15 - k, 7, col)
            c.set(15 - k, 8, shade)
        c.set(7, 7, col)
        c.set(8, 8, col)
        outline(c)
    return m


def tell_maker(bright):
    def m(c):
        col = GOLD if bright else RED
        c.rect(3, 1, 2, 9, col)
        c.rect(3, 12, 2, 2, col)
        outline(c)
    return m


def stars_maker(step):
    def m(c):
        for i in range(3):
            a = i * 2 * math.pi / 3 + step * 0.8
            x, y = 8 + math.cos(a) * 6, 8 + math.sin(a) * 2.5
            c.set(int(x), int(y), GOLD)
            c.set(int(x) + 1, int(y), GOLD)
            c.set(int(x), int(y) - 1, GOLD_S if step else GOLD)
            c.set(int(x), int(y) + 1, GOLD)
        outline(c)
    return m


def icon_maker(kind):
    def m(c):
        if kind in (0, 1):
            heart = [".hh.hh.", "hhhhhhh", "hhhhhhh", ".hhhhh.", "..hhh..", "...h..."]
            col = RED if kind == 0 else SNOW_D
            c.ascii(0, 1, heart, {"h": col})
            if kind == 0:
                c.set(1, 2, rgb("#f8a0a0"))
        elif kind == 2:
            shaded_ball(c, 3.5, 4, 3)
        else:
            disc(c, 3.5, 5, 2, SNOW_D)
        outline(c)
    return m


SPRITES = {
    "snowball_ball8": (8, 8, [ball_maker(8, 1.6), ball_maker(8, 2.6)]),
    "snowball_ball16": (16, 16, [ball_maker(16, 4), ball_maker(16, 5.5)]),
    "snowball_ball32": (32, 32, [ball_maker(32, 8.5), ball_maker(32, 12)]),
    "snowball_splat": (16, 16, [splat_maker(0), splat_maker(1), splat_maker(2)]),
    "snowball_face": (64, 64, [face_maker(False), face_maker(True)]),
    "snowball_aim": (16, 16, [aim_maker(False), aim_maker(True)]),
    "snowball_tell": (8, 16, [tell_maker(False), tell_maker(True)]),
    "snowball_stars": (16, 16, [stars_maker(0), stars_maker(1)]),
    "snowball_icons": (8, 8, [icon_maker(0), icon_maker(1), icon_maker(2), icon_maker(3)]),
}


def sprite_json(h):
    return '{\n    "type": "sprite",\n    "height": %d\n}' % h


def main():
    os.makedirs(OUT, exist_ok=True)
    preview = sys.argv[1] if len(sys.argv) > 1 else None
    yard, yard_screen = make_yard()
    bgs = {"snowball_yard": yard, "snowball_cover": make_cover(yard_screen), "snowball_fort": make_fort()}
    for name, bg in bgs.items():
        worst = max(len({bg.get(x, y) for y in range(ty, ty + 8) for x in range(tx, tx + 8)} - {None})
                    for ty in range(0, 256, 8) for tx in range(0, 256, 8))
        if worst > 15:
            sys.exit(f"{name}: a tile has {worst} colours (max 15)")
        print(f"{name}: {len(bg.colors())} colours, at most {worst} in one tile")
        bg.save_bmp(os.path.join(OUT, name + ".bmp"), bpp=8)
        write_json(os.path.join(OUT, name + ".json"), '{\n    "type": "regular_bg",\n    "bpp_mode": "bpp_4_auto"\n}')
    sheets = {}
    for name, (w, h, makers) in SPRITES.items():
        sheet = frames(w, h, makers)
        extra = [col for col in sheet.colors() if col not in FX_PAL]
        if extra:
            sys.exit(f"{name}: colours not in FX_PAL: {extra}")
        sheet.save_bmp(os.path.join(OUT, name + ".bmp"), bpp=4, palette=FX_PAL)
        write_json(os.path.join(OUT, name + ".json"), sprite_json(h))
        sheets[name] = sheet
    if preview:
        os.makedirs(preview, exist_ok=True)
        scene = Canvas(240, 160)
        for name in ("snowball_yard", "snowball_cover", "snowball_fort"):
            bg = bgs[name]
            for y in range(160):
                for x in range(240):
                    col = bg.get(x + SCREEN_X, y + SCREEN_Y)
                    if col is not None:
                        scene.set(x, y, col)
        save_png(os.path.join(preview, "snowball_scene.png"), scene, scale=3)
        w = sum(s.w + 4 for s in sheets.values())
        h = max(s.h for s in sheets.values())
        sp = Canvas(w, h)
        x = 0
        for s in sheets.values():
            sp.blit(s, x, 0)
            x += s.w + 4
        save_png(os.path.join(preview, "snowball_sprites.png"), sp, scale=3, background=SKY[3])
    print(f"Wrote Snowball Fight art to {os.path.normpath(OUT)}")


if __name__ == "__main__":
    main()
