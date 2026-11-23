"""Generates the art for the opening scene (the family group photo by the big tree):

  graphics/ui/title_scene.bmp   the picture behind the family: dusk sky, snowy hills, the big tree (256x256, the
                                screen shows the 240x160 part at x=8, y=48)
  graphics/ui/title_frame.bmp   a white photo border that appears when the picture is taken
  graphics/ui/title_camera.bmp  the camera on its tripod (16x32, 2 frames: waiting / flash)

The GAME'S NAME is not drawn here: see tools/art/game_title.txt and make_ui.py.

Run:  python3 tools/art/make_title.py
"""
import os
import sys
from canvas import Canvas, rgb, write_json, save_png

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
OUT = os.path.join(ROOT, "graphics", "ui")
W, H = 240, 160          # what the screen shows
OX, OY = 8, 48           # where that sits inside the 256x256 background image

OUTL = rgb("#282048")
SKY = [rgb(h) for h in ("#141a48", "#1c2460", "#283074", "#3c3c88", "#584c96", "#8a5c9c", "#c87a9a", "#f0a090")]
STAR = rgb("#f8f0d0")
MOUNT, MOUNT_L = rgb("#3a4488"), rgb("#5460a0")
HILL, HILL_L, HILL_D = rgb("#c8d4f0"), rgb("#e4ecfc"), rgb("#a8b8e0")
SNOW, SNOW_S, SNOW_D, SNOW_DD = rgb("#f0f4fc"), rgb("#dce4f8"), rgb("#c0d0ee"), rgb("#a4b8e0")
PINE, PINE_L = rgb("#1c4a48"), rgb("#2c6860")
TREE_D, TREE_M, TREE_L = rgb("#185838"), rgb("#28784a"), rgb("#48a060")
TRUNK = rgb("#704028")
GOLD, RED, BLUE, PINK, PURPLE, WIN = (rgb("#f8d040"), rgb("#e84040"), rgb("#4890f0"), rgb("#f080b8"),
                                      rgb("#a070e0"), rgb("#f8e078"))
HOUSE, HOUSE_D, ROOF = rgb("#605078"), rgb("#483c64"), rgb("#8a90b8")
WOOD, WOOD_D, BLACK, GREY, GREY_D, GREY_L = (rgb("#a06838"), rgb("#6a4020"), rgb("#303038"), rgb("#8890a0"),
                                              rgb("#585c70"), rgb("#c0c8d8"))


def dither_band(c, y0, y1, col_a, col_b):
    """Fills rows y0..y1 with col_a and dithers the last rows towards col_b."""
    for y in range(y0, y1 + 1):
        for x in range(W):
            c.set(x, y, col_a)
    for k, y in enumerate(range(y1 - 2, y1 + 1)):
        for x in range(W):
            if (x + y) % 2 == 0 and k >= 1 or (x % 4 == 0 and (y % 2 == 0) and k == 0):
                c.set(x, y, col_b)


def scene():
    c = Canvas(W, H)
    # sky: bands with dithered edges
    bands = [0, 12, 24, 36, 48, 58, 68, 78]
    for i, y0 in enumerate(bands):
        y1 = bands[i + 1] - 1 if i + 1 < len(bands) else 84
        dither_band(c, y0, y1, SKY[i], SKY[min(i + 1, len(SKY) - 1)])
    # stars (a tiny pseudo-random scatter)
    seed = 12345
    for i in range(48):
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        x = (seed >> 8) % W
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        y = (seed >> 8) % 50
        c.set(x, y, STAR)
        if i % 8 == 0:
            c.set(x + 1, y, STAR); c.set(x - 1, y, STAR); c.set(x, y - 1, STAR); c.set(x, y + 1, STAR)
    # far mountains (smooth rolling silhouette)
    import math
    for x in range(W):
        h = int(20 + 8 * math.sin(x / 19.0 + 0.5) + 5 * math.sin(x / 7.0 + 2.0) + 3 * math.sin(x / 3.1))
        for y in range(84 - h, 90):
            c.set(x, y, MOUNT if (x + y) % 9 else MOUNT_L)
    # rolling snowy hills
    for x in range(W):
        y_top = 82 + int(5 * (1 + math.sin(x / 21.0)) + 3 * (1 + math.sin(x / 9.0 + 1)))
        for y in range(y_top, 100):
            c.set(x, y, HILL_L if y < y_top + 2 else HILL if (x + y) % 11 else HILL_D)
    # distant cottages with warm windows
    for hx, hy in ((18, 86), (176, 84), (206, 89)):
        c.rect(hx, hy, 18, 12, HOUSE)
        c.rect(hx, hy + 9, 18, 3, HOUSE_D)
        for i in range(8):
            c.hline(hx - 2 + i // 2, hx + 19 - i // 2, hy - 1 - i, ROOF if i % 3 else rgb("#a8aed0"))
        c.rect(hx + 3, hy + 3, 4, 4, WIN)
        c.rect(hx + 11, hy + 3, 4, 4, WIN)
        c.rect(hx + 13, hy - 8, 3, 5, HOUSE_D)
    # pine tree line
    for tx in range(4, W, 11):
        ty = 92 + ((tx * 5) % 7)
        for i in range(9):
            half = 1 + i // 2
            c.hline(tx - half, tx + half, ty - 9 + i, PINE if i % 3 else PINE_L)
    # snowy ground
    for y in range(96, H):
        t = (y - 96) / (H - 96)
        for x in range(W):
            if t < 0.35:
                col = SNOW
            elif t < 0.65:
                col = SNOW if (x + y) % 3 else SNOW_S
            else:
                col = SNOW_S if (x + y) % 4 else SNOW_D
            c.set(x, y, col)
    for i in range(40):                       # little drifts
        x, y = (i * 47 + 9) % W, 100 + (i * 23) % 56
        c.hline(x, x + 3 + i % 4, y, SNOW_D)
    # the big Christmas tree behind the family
    cx, base = 120, 104
    top_y = 50
    height = base - top_y
    for i in range(height):
        half = 2 + i * 27 // height
        if i % 17 > 14:
            half -= 3
        col = TREE_M if (i // 3) % 3 else TREE_D
        c.hline(cx - half, cx + half, top_y + i, col)
        c.set(cx - half - 1, top_y + i, OUTL)
        c.set(cx + half + 1, top_y + i, OUTL)
        if i % 17 == 16:
            c.hline(cx - half, cx + half, top_y + i, TREE_D)
    c.rect(cx - 4, base, 8, 6, TRUNK)
    # garland, ornaments and lights
    colors = [RED, GOLD, BLUE, PINK, SNOW, PURPLE]
    k = 0
    for i in range(10, height - 4, 7):
        half = 2 + i * 27 // height
        for x in range(cx - half + 3, cx + half - 2, 6):
            c.set(x + (i // 7) % 3, top_y + i, colors[k % 6])
            c.set(x + (i // 7) % 3 + 1, top_y + i, colors[k % 6])
            k += 1
    for i in range(14, height, 9):
        half = 2 + i * 27 // height
        for x in range(cx - half + 1, cx + half, 3):
            c.set(x, top_y + i + (x % 3), WIN)
    # big star on top
    c.ascii(cx - 6, top_y - 11, [
        ".....o.....", "....oGo....", "...oGGGo...", "ooooGGGoooo", ".oGGGwGGGo.", "..oGGGGGo..", "...oGGGo...",
        "..oGGoGGo..", "..ooo.ooo..",
    ], {"o": OUTL, "G": GOLD, "w": rgb("#ffffff")})
    # presents under the tree
    for bx, by, w, h, col, rib in ((cx - 42, 98, 10, 8, RED, GOLD), (cx - 30, 100, 8, 6, BLUE, SNOW),
                                   (cx + 30, 99, 11, 8, GOLD, RED), (cx + 43, 101, 8, 6, PINK, BLUE)):
        c.rect(bx, by, w, h, OUTL)
        c.rect(bx + 1, by + 1, w - 2, h - 2, col)
        c.vline(bx + w // 2, by + 1, by + h - 2, rib)
        c.hline(bx + 1, bx + w - 2, by + h // 2, rib)
    # lamp posts at both sides with a string of lights between them and the tree
    for lx in (14, 226):
        c.rect(lx - 1, 52, 3, 54, BLACK)
        c.rect(lx - 4, 46, 9, 7, WIN)
        c.hline(lx - 5, lx + 5, 45, OUTL)
        c.rect(lx - 4, 106, 9, 2, GREY_D)
    for i in range(W - 28):
        x = 14 + i
        sag = int(8 * (1 - ((x - 120) / 106.0) ** 2))
        y = 50 + sag
        if i % 7 == 0:
            c.set(x, y + 1, colors[(i // 7) % 6])
            c.set(x, y + 2, colors[(i // 7) % 6])
        c.set(x, y, OUTL)
    # footprints leading to the photo spot
    for i in range(8):
        c.set(60 + i * 7, 148 - (i % 2) * 2, SNOW_DD)
        c.set(61 + i * 7, 148 - (i % 2) * 2, SNOW_DD)
    return c


def frame():
    c = Canvas(W, H)
    paper, paper_d, paper_l = rgb("#f8f4e8"), rgb("#d8d0bc"), rgb("#ffffff")
    b = 5
    for y in range(H):
        for x in range(W):
            if x < b or x >= W - b or y < b or y >= H - b - 3:
                c.set(x, y, paper)
    for x in range(W):
        c.set(x, 0, paper_l)
    c.hline(b - 1, W - b, b - 1, paper_d)
    c.hline(b - 1, W - b, H - b - 4, paper_l)
    c.vline(b - 1, b - 1, H - b - 4, paper_d)
    c.vline(W - b, b - 1, H - b - 4, paper_l)
    # corner decorations (holly)
    for (hx, hy) in ((8, 8), (W - 14, 8), (8, H - 17), (W - 14, H - 17)):
        c.ascii(hx, hy, ["gg.gg", "ggggg", ".grg.", ".rrr."][0:3], {"g": rgb("#40a050"), "r": rgb("#e03838")})
        c.set(hx + 2, hy + 1, rgb("#e03838"))
    out = Canvas(256, 256)
    out.blit(c, OX, OY)
    return out


def camera():
    """16x32 frames: 0 waiting (red light), 1 flash."""
    frames = []
    for flash in (False, True):
        c = Canvas(16, 32)
        # tripod legs
        for (x0, x1) in ((8, 3), (8, 8), (8, 13)):
            steps = 14
            for i in range(steps + 1):
                c.set(x0 + round((x1 - x0) * i / steps), 17 + i, GREY_D)
        c.rect(7, 14, 3, 4, GREY_D)
        # camera body
        c.rect(2, 4, 12, 10, OUTL)
        c.rect(3, 5, 10, 8, GREY)
        c.rect(3, 5, 10, 2, GREY_L)
        c.rect(3, 11, 10, 2, GREY_D)
        c.rect(5, 2, 4, 3, OUTL)
        c.rect(6, 3, 2, 1, GREY_L)
        # lens
        c.ellipse(8, 9, 3.2, 3.2, BLACK, OUTL)
        c.set(7, 8, rgb("#80a0e0"))
        # light
        c.set(12, 6, WIN if flash else RED)
        if flash:
            for (x, y) in ((8, 9), (3, 4), (13, 4), (2, 12), (14, 12), (8, 1), (0, 8), (15, 8)):
                c.set(x, y, rgb("#ffffff"))
            for dx, dy in ((0, 1), (1, 0), (-1, 0), (0, -1)):
                c.set(8 + dx, 9 + dy, rgb("#ffffff"))
        frames.append(c)
    sheet = Canvas(16, 64)
    for i, f in enumerate(frames):
        sheet.blit(f, 0, i * 32)
    return sheet


def main():
    os.makedirs(OUT, exist_ok=True)
    s = scene()
    big = Canvas(256, 256)
    big.px = [rgb("#141a48")] * (256 * 256)
    big.blit(s, OX, OY)
    colours = len(big.colors())
    print(f"title scene: {colours} colours")
    big.save_bmp(os.path.join(OUT, "title_scene.bmp"), bpp=8)
    write_json(os.path.join(OUT, "title_scene.json"), '{\n    "type": "regular_bg",\n    "bpp_mode": "bpp_8"\n}')
    fr = frame()
    fr.save_bmp(os.path.join(OUT, "title_frame.bmp"), bpp=4)
    write_json(os.path.join(OUT, "title_frame.json"), '{\n    "type": "regular_bg"\n}')
    camera().save_bmp(os.path.join(OUT, "title_camera.bmp"), bpp=4)
    write_json(os.path.join(OUT, "title_camera.json"), '{\n    "type": "sprite",\n    "height": 32\n}')
    if len(sys.argv) > 1:
        save_png(sys.argv[1], big, scale=2)


if __name__ == "__main__":
    main()
