"""Generates the little animated things on the town map (sprites, not part of the map picture):

  world_prints.bmp   snow footprints (8x8, 4 frames: tall/wide x fresh/faded)
  world_horse.bmp    the horse in the paddock (32x32, 3 frames: head up, eating, chewing)
  world_reindeer.bmp the reindeer outside the Roost (32x32, 3 frames: looking, eating, ear flick)
  world_fx.bmp       an apple and a heart (16x16, 2 frames)

Run:  python3 tools/art/make_world_sprites.py
Then redraw them in any pixel editor if you like (indexed BMP, 16 colours, first colour = transparent).
"""
import os
from canvas import Canvas, rgb, write_json

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
OUT = os.path.join(ROOT, "graphics", "world")

# ONE shared 15-colour palette for all four sheets below. The GBA only has 16 sprite palettes in total (the
# ten characters, the snowflakes and the text colours already use most of them), and sheets with exactly the
# same palette share one slot. So: keep every colour in this list, and don't add new ones.
OUTL = rgb("#283048")
BODY, BODY_D, BODY_L = rgb("#b88060"), rgb("#8a5a3a"), rgb("#d8a880")      # the horse (BODY_L also lights the deer)
MANE, WHITE, BLACK = rgb("#583820"), rgb("#f8f8f8"), rgb("#303038")
DEER, DEER_D, ANTLER = rgb("#a86840"), rgb("#7a4828"), rgb("#e8d0a0")
RED, GREEN, PINK, PINK_L = rgb("#e83838"), rgb("#50a850"), rgb("#f060a0"), rgb("#ffb0d0")
SNOW_D = rgb("#a0b4dc")
PALETTE = [OUTL, BODY, BODY_D, BODY_L, MANE, WHITE, BLACK, DEER, DEER_D, ANTLER, RED, GREEN, PINK, PINK_L, SNOW_D]
# aliases (the same colours under the names the drawing code below uses)
DEER_L, NOSE, GRASS, LEAF, SNOW_S, RED_D = BODY_L, RED, GREEN, GREEN, WHITE, OUTL


def line(c, x0, y0, x1, y1, thick, col):
    steps = max(abs(x1 - x0), abs(y1 - y0), 1)
    for i in range(steps + 1):
        x = round(x0 + (x1 - x0) * i / steps)
        y = round(y0 + (y1 - y0) * i / steps)
        c.rect(x - thick // 2, y - thick // 2, thick, thick, col)


def add_outline(c):
    marks = []
    for y in range(c.h):
        for x in range(c.w):
            if c.get(x, y) is None and any(c.get(x + dx, y + dy) not in (None, OUTL)
                                          for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                marks.append((x, y))
    for x, y in marks:
        c.set(x, y, OUTL)


def sheet(frames):
    w, h = frames[0].w, frames[0].h
    out = Canvas(w, h * len(frames))
    for i, f in enumerate(frames):
        out.blit(f, 0, i * h)
    return out


def save(name, canvas, frame_h):
    canvas.save_bmp(os.path.join(OUT, name + ".bmp"), bpp=4, palette=PALETTE)
    write_json(os.path.join(OUT, name + ".json"), '{\n    "type": "sprite",\n    "height": %d\n}' % frame_h)


# ----------------------------------------------------------------------------- footprints
def prints():
    frames = []
    for fresh in (True, False):
        col = SNOW_D
        tall = Canvas(8, 8)
        tall.rect(3, 1, 3, 6, col)
        tall.rect(2, 2, 5, 4, col)
        tall.set(2, 2, None); tall.set(6, 2, None); tall.set(2, 5, None); tall.set(6, 5, None)
        wide = Canvas(8, 8)
        wide.rect(1, 3, 6, 3, col)
        wide.rect(2, 2, 4, 5, col)
        wide.set(2, 2, None); wide.set(5, 2, None); wide.set(2, 6, None); wide.set(5, 6, None)
        if not fresh:                      # a faded print: only every other pixel is left
            for f in (tall, wide):
                for y in range(8):
                    for x in range(8):
                        if (x + y) % 2:
                            f.set(x, y, None)
        frames += [tall, wide]
    # order: tall fresh, wide fresh, tall faded, wide faded
    save("world_prints", sheet(frames), 8)


# ----------------------------------------------------------------------------- the horse
def horse(frame):
    c = Canvas(32, 32)
    # tail
    line(c, 4, 12, 2, 20, 2, MANE)
    line(c, 3, 14, 3, 22, 2, MANE)
    # back legs and front legs (behind the body slightly darker)
    for x, col in ((7, BODY_D), (10, BODY), (18, BODY_D), (21, BODY)):
        c.rect(x, 20, 3, 9, col)
        c.rect(x, 28, 3, 2, BLACK)
    # body
    c.ellipse(14, 17, 10, 6.5, BODY)
    c.ellipse(14, 19, 8, 3.5, BODY_L)
    c.hline(8, 20, 22, BODY_D)
    # saddle blanket
    c.rect(11, 12, 6, 4, RED)
    c.hline(11, 16, 15, RED_D)
    # neck + head
    if frame == 0:            # head up
        line(c, 21, 14, 25, 8, 5, BODY)
        line(c, 20, 12, 24, 6, 2, MANE)
        c.rect(24, 4, 6, 5, BODY)
        c.rect(28, 7, 3, 4, BODY_L)
        c.rect(24, 2, 2, 3, BODY_D)
        c.set(27, 5, BLACK)
        c.rect(26, 4, 2, 1, WHITE)
    elif frame == 1:          # head down, eating
        line(c, 21, 14, 25, 20, 5, BODY)
        line(c, 20, 13, 23, 19, 2, MANE)
        c.rect(23, 20, 6, 5, BODY)
        c.rect(27, 23, 3, 4, BODY_L)
        c.rect(23, 19, 2, 2, BODY_D)
        c.set(26, 22, BLACK)
        for gx in (26, 28, 30):
            c.rect(gx, 27, 1, 3, GRASS)
    else:                     # head half down, chewing
        line(c, 21, 14, 25, 15, 5, BODY)
        line(c, 20, 12, 24, 13, 2, MANE)
        c.rect(24, 13, 6, 5, BODY)
        c.rect(28, 16, 3, 4, BODY_L)
        c.rect(24, 11, 2, 3, BODY_D)
        c.set(27, 15, BLACK)
        c.rect(28, 20, 1, 3, GRASS)
    add_outline(c)
    return c


# ----------------------------------------------------------------------------- the reindeer
def reindeer(frame):
    c = Canvas(32, 32)
    line(c, 5, 15, 3, 17, 3, WHITE)            # little tail
    for x, col in ((8, DEER_D), (11, DEER), (19, DEER_D), (22, DEER)):
        c.rect(x, 21, 2, 8, col)
        c.rect(x, 28, 2, 2, BLACK)
    c.ellipse(15, 18, 8.5, 5.5, DEER)
    c.ellipse(15, 20, 7, 3, DEER_L)
    c.ellipse(21, 16, 4, 3.5, WHITE)           # fluffy chest
    if frame == 0:            # looking ahead
        line(c, 21, 15, 24, 9, 4, DEER)
        c.rect(23, 6, 6, 5, DEER)
        c.rect(27, 8, 3, 3, DEER_L)
        c.set(30, 9, NOSE); c.set(30, 10, NOSE); c.set(29, 9, NOSE)
        c.set(26, 8, BLACK)
        line(c, 24, 6, 22, 1, 1, ANTLER); line(c, 22, 3, 19, 3, 1, ANTLER); line(c, 23, 5, 20, 5, 1, ANTLER)
        line(c, 26, 6, 27, 1, 1, ANTLER); line(c, 26, 3, 29, 2, 1, ANTLER)
    elif frame == 1:          # eating snow
        line(c, 21, 15, 25, 21, 4, DEER)
        c.rect(24, 21, 6, 5, DEER)
        c.rect(28, 24, 3, 3, DEER_L)
        c.set(30, 25, NOSE); c.set(30, 26, NOSE); c.set(29, 25, NOSE)
        c.set(27, 23, BLACK)
        line(c, 24, 21, 21, 17, 1, ANTLER); line(c, 22, 19, 19, 18, 1, ANTLER)
        line(c, 26, 21, 27, 17, 1, ANTLER); line(c, 27, 19, 30, 18, 1, ANTLER)
        c.hline(23, 31, 28, SNOW_S)
    else:                     # head tilted, ear flick
        line(c, 21, 15, 24, 10, 4, DEER)
        c.rect(23, 7, 6, 5, DEER)
        c.rect(27, 9, 3, 3, DEER_L)
        c.set(30, 10, NOSE); c.set(30, 11, NOSE); c.set(29, 10, NOSE)
        c.set(26, 9, BLACK)
        line(c, 24, 7, 21, 2, 1, ANTLER); line(c, 22, 4, 19, 5, 1, ANTLER); line(c, 22, 5, 20, 7, 1, ANTLER)
        line(c, 26, 7, 28, 2, 1, ANTLER); line(c, 27, 4, 30, 4, 1, ANTLER)
        c.rect(22, 5, 2, 2, DEER_D)             # ear
    add_outline(c)
    return c


# ----------------------------------------------------------------------------- apple and heart
def fx():
    apple = Canvas(16, 16)
    apple.ellipse(8, 9, 5, 5, RED)
    apple.rect(5, 7, 2, 2, PINK_L)
    apple.rect(8, 3, 1, 3, MANE)
    apple.rect(9, 3, 3, 2, LEAF)
    add_outline(apple)
    heart = Canvas(16, 16)
    for (cx, cy) in ((5, 6), (10, 6)):
        heart.ellipse(cx, cy, 3.3, 3.3, PINK)
    for i in range(7):
        heart.hline(2 + i, 13 - i, 8 + i, PINK)
    heart.set(4, 4, PINK_L); heart.set(5, 4, PINK_L)
    add_outline(heart)
    save("world_fx", sheet([apple, heart]), 16)


def main():
    os.makedirs(OUT, exist_ok=True)
    prints()
    save("world_horse", sheet([horse(i) for i in range(3)]), 32)
    save("world_reindeer", sheet([reindeer(i) for i in range(3)]), 32)
    fx()


if __name__ == "__main__":
    main()
