"""Generates the art for the controls pages (see include/sb_controls.h) in gba/graphics/ui/.

    python3 tools/art/make_controls.py [preview_dir]

  controls_frame    regular BG (256x256, the top-left 240x160 is the screen): the dotted backdrop, the title
                    bar, a Miyoo Mini Plus with every button greyed out, its screen left TRANSPARENT (the game
                    shows a close-up of itself through it), and the panel on the right for the labels.
  controls_buttons  32x32 sprites, drawn over the frame:
                      0 A lit, 1 A pressed, 2 B lit, 3 B pressed,
                      4 D-pad lit, 5 D-pad lit + left pressed, 6 D-pad lit + right pressed,
                      7 Start lit, 8 Start pressed,
                      9 A icon, 10 A icon glowing, 11 B icon, 12 B icon glowing,
                      13 D-pad icon, 14 D-pad icon glowing, 15 Start icon, 16 Start icon glowing,
                      17 a divider line (32 px wide, use 4 side by side)
The layout numbers (DEVICE_*, SCREEN_*, CTRL_*) must match include/sb_controls.h.
"""
import os
import sys

from canvas import Canvas, rgb, write_json, save_png

OUT = os.path.join(os.path.dirname(__file__), "..", "..", "graphics", "ui")

O = rgb("#282030")
BG = rgb("#5078b8")
BG_DOT = rgb("#6088c8")
PANEL = rgb("#f8f0e0")
PANEL_EDGE = rgb("#c8b8a0")
BODY = rgb("#e8e8ec")
BODY_SHADE = rgb("#c0c0c8")
SCREEN = rgb("#202028")
DPAD = rgb("#303038")
DIM = rgb("#b0b0b8")
DIM_DARK = rgb("#909098")
GLOW = rgb("#f8f080")
PRESS = rgb("#f8f8f8")
A_FILL, A_DARK = rgb("#c83030"), rgb("#902020")
B_FILL, B_DARK = rgb("#e8c030"), rgb("#b08818")

# ---- layout (screen pixels). Keep in sync with include/sb_controls.h
DEVICE_X, DEVICE_Y, DEVICE_W, DEVICE_H = 5, 20, 88, 138
SCREEN_X, SCREEN_Y, SCREEN_W, SCREEN_H = 13, 28, 72, 56
CORNER_R = 18
CTRL = {"DPAD": (27, 118), "X": (69, 106), "A": (80, 117), "B": (69, 128), "Y": (58, 117),
        "SELECT": (35, 144), "START": (48, 144)}
PANEL_X0, PANEL_Y0, PANEL_X1, PANEL_Y1 = 100, 22, 235, 156


def rounded_rect(c, x, y, w, h, fill, edge):
    c.rect(x + 2, y, w - 4, h, fill)
    c.rect(x, y + 2, w, h - 4, fill)
    c.rect(x + 1, y + 1, w - 2, h - 2, fill)
    c.hline(x + 2, x + w - 3, y - 1, edge)
    c.hline(x + 2, x + w - 3, y + h, edge)
    c.vline(x - 1, y + 2, y + h - 3, edge)
    c.vline(x + w, y + 2, y + h - 3, edge)
    for (px, py) in ((x, y + 1), (x + 1, y), (x + w - 2, y), (x + w - 1, y + 1),
                     (x, y + h - 2), (x + 1, y + h - 1), (x + w - 2, y + h - 1), (x + w - 1, y + h - 2)):
        c.set(px, py, edge)


def circle(c, cx, cy, r, fill, edge):
    c.ellipse(cx, cy, r + 1, r + 1, edge)
    c.ellipse(cx, cy, r, r, fill)


def dpad(c, x, y, fill, glow=False, press=0):
    if glow:
        c.rect(x - 5, y - 15, 11, 31, GLOW)
        c.rect(x - 15, y - 5, 31, 11, GLOW)
    c.rect(x - 4, y - 14, 9, 29, fill)
    c.rect(x - 14, y - 4, 29, 9, fill)
    if press:
        ax = x + press * 10
        c.rect(ax - 3, y - 3, 7, 7, PRESS)
    c.set(x, y, DIM)


def pill(c, sx, sy, fill, glow=False):
    """A slanted Select / Start pill, rising to the right."""
    if glow:
        for i in range(9):
            c.rect(sx - 4 + i - 1, sy + 2 - i // 2 - 2, 3, 5, GLOW)
    for i in range(9):
        c.rect(sx - 4 + i, sy + 2 - i // 2 - 1, 1, 3, fill)


def face_button(c, bx, by, fill, dark, state):
    """state: None (greyed out), 'lit' or 'pressed'."""
    if state is None:
        circle(c, bx, by, 6, DIM, DIM_DARK)
    elif state == "lit":
        c.ellipse(bx, by, 8, 8, GLOW)
        circle(c, bx, by, 6, fill, O)
        c.set(bx - 2, by - 3, PRESS)
    else:
        c.ellipse(bx, by + 1, 8, 8, PRESS)
        circle(c, bx, by + 1, 6, dark, O)


def draw_frame():
    c = Canvas(256, 256, fill=BG)
    for y in range(0, 160, 4):
        for x in range((y // 4) % 2 * 4, 240, 8):
            c.set(x, y, BG_DOT)
    # title bar
    c.rect(0, 0, 256, 17, PANEL)
    c.hline(0, 255, 17, O)
    # the Miyoo
    rounded_rect(c, DEVICE_X, DEVICE_Y, DEVICE_W, DEVICE_H, BODY, O)
    c.vline(DEVICE_X + DEVICE_W - 1, DEVICE_Y + 3, DEVICE_Y + DEVICE_H - 4, BODY_SHADE)
    ccx, ccy = DEVICE_X + DEVICE_W - CORNER_R, DEVICE_Y + DEVICE_H - CORNER_R
    for y in range(ccy, DEVICE_Y + DEVICE_H + 2):
        for x in range(ccx, DEVICE_X + DEVICE_W + 2):
            d = ((x - ccx) ** 2 + (y - ccy) ** 2) ** 0.5
            if d > CORNER_R + 0.5:
                c.set(x, y, BG_DOT if (y % 4 == 0 and (x - (y // 4) % 2 * 4) % 8 == 0) else BG)
            elif d > CORNER_R - 0.6:
                c.set(x, y, O)
    c.rect(SCREEN_X - 4, SCREEN_Y - 4, SCREEN_W + 8, SCREEN_H + 8, SCREEN)
    for y in range(SCREEN_Y, SCREEN_Y + SCREEN_H):        # the hole the game shows through
        for x in range(SCREEN_X, SCREEN_X + SCREEN_W):
            c.set(x, y, None)
    circle(c, DEVICE_X + 44, DEVICE_Y + 80, 2, DIM, DIM_DARK)          # menu button
    dpad(c, *CTRL["DPAD"], DIM_DARK)
    for name in ("X", "A", "B", "Y"):
        face_button(c, *CTRL[name], None, None, None)
    for name in ("SELECT", "START"):
        pill(c, *CTRL[name], DIM_DARK)
    for i in range(4):                                                  # speaker, down the big corner
        x1 = DEVICE_X + 80 - i * 3
        c.hline(x1 - 6, x1, DEVICE_Y + 118 + i * 4, DIM_DARK)
    # the labels panel
    c.rect(PANEL_X0, PANEL_Y0, PANEL_X1 - PANEL_X0 + 1, PANEL_Y1 - PANEL_Y0 + 1, PANEL)
    c.hline(PANEL_X0, PANEL_X1, PANEL_Y0 - 1, O)
    c.hline(PANEL_X0, PANEL_X1, PANEL_Y1 + 1, O)
    c.vline(PANEL_X0 - 1, PANEL_Y0, PANEL_Y1, O)
    c.vline(PANEL_X1 + 1, PANEL_Y0, PANEL_Y1, O)
    return c


def make_buttons():
    frames = []

    def new():
        f = Canvas(32, 32)
        frames.append(f)
        return f

    face_button(new(), 16, 16, A_FILL, A_DARK, "lit")
    face_button(new(), 16, 16, A_FILL, A_DARK, "pressed")
    face_button(new(), 16, 16, B_FILL, B_DARK, "lit")
    face_button(new(), 16, 16, B_FILL, B_DARK, "pressed")
    dpad(new(), 16, 16, DPAD, glow=True)
    dpad(new(), 16, 16, DPAD, glow=True, press=-1)
    dpad(new(), 16, 16, DPAD, glow=True, press=1)
    pill(new(), 16, 16, DPAD, glow=True)
    f = new()
    pill(f, 16, 16, PRESS, glow=True)
    # label icons (centred), plain and glowing
    for fill, dark in ((A_FILL, A_DARK), (B_FILL, B_DARK)):
        for glow in (False, True):
            f = new()
            if glow:
                f.rect(9, 8, 14, 17, GLOW)
            circle(f, 16, 16, 4, fill, dark)
            f.set(15, 14, PRESS)
    for glow in (False, True):
        f = new()
        if glow:
            f.rect(7, 7, 19, 19, GLOW)
        f.rect(14, 9, 5, 15, DPAD)
        f.rect(9, 14, 15, 5, DPAD)
        f.set(16, 16, DIM)
    for glow in (False, True):
        f = new()
        if glow:
            f.rect(10, 12, 13, 9, GLOW)
        f.rect(12, 14, 9, 4, DPAD)
    f = new()
    f.hline(0, 31, 16, PANEL_EDGE)
    sheet = Canvas(32, 32 * len(frames))
    for i, f in enumerate(frames):
        sheet.blit(f, 0, i * 32)
    return sheet


def main():
    os.makedirs(OUT, exist_ok=True)
    frame = draw_frame()
    frame.save_bmp(os.path.join(OUT, "controls_frame.bmp"), bpp=8)
    write_json(os.path.join(OUT, "controls_frame.json"),
               '{\n    "type": "regular_bg",\n    "bpp_mode": "bpp_4_auto"\n}')
    buttons = make_buttons()
    buttons.save_bmp(os.path.join(OUT, "controls_buttons.bmp"))
    write_json(os.path.join(OUT, "controls_buttons.json"), '{\n    "type": "sprite",\n    "height": 32\n}')
    print("frame: %d colours; buttons: %d colours" % (len(frame.colors()), len(buttons.colors())))
    if len(sys.argv) > 1:
        d = sys.argv[1]
        os.makedirs(d, exist_ok=True)
        save_png(os.path.join(d, "controls_frame.png"), frame, scale=2, background=(255, 0, 255))
        save_png(os.path.join(d, "controls_buttons.png"), buttons, scale=3)


if __name__ == "__main__":
    main()
