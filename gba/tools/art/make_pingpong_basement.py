"""Generates the Ping Pong training room art in gba/graphics/pingpong/ (Charly's basement).

    python3 tools/art/make_pingpong_basement.py [preview_dir]

Backgrounds (256x256, only the top-left 240x160 is on screen; created at (8, 48) like the hall):
  pingpong_basement  pink walls, soffit, whiteboard, red knot, pictures, pillar, treadmill and
                     the carpet                                          (BG priority 3)
  pingpong_btable    the dark "Silver Extreme" table, its net, and the ball robot behind it:
                     catch net with a V of poles, ball tray, black column (BG priority 2)
Sprites:
  pingpong_robot     32x32, 5 frames: the robot's head aimed far left, left, middle, right,
                     far right (it turns to show where the next ball goes)

The table uses the same geometry as tools/art/make_pingpong.py (TABLE_*), so the game's
physics fit both rooms.
"""
import os
import sys

from canvas import Canvas, rgb, write_json, save_png
import make_pingpong as pp

X0, X1, Y0, Y1 = pp.TABLE_X0, pp.TABLE_X1, pp.TABLE_Y0, pp.TABLE_Y1
NET_Y, FACE_H, LEG_H = pp.NET_Y, pp.FACE_H, pp.LEG_H
OUTLINE = pp.OUTLINE

# The robot's head: its centre on screen (pixels). Must match ROBOT_HEAD_* in src/games/pingpong.cpp.
HEAD_X, HEAD_Y = (X0 + X1) // 2, 28

# ---------------------------------------------------------------- palette
SOFFIT = rgb("#f8e8e0")
SOFFIT_SHADE = rgb("#e0c8c0")
WALL = rgb("#f0b8a8")
WALL_SHADE = rgb("#e0a898")
WALL_DARK = rgb("#c88878")
BASEBOARD = rgb("#f8e0d0")
LAMP = rgb("#f8f8e0")

CARPET = rgb("#d8c8a8")
CARPET_DOT = rgb("#c8b898")
CARPET_LIGHT = rgb("#e8d8b8")
CARPET_SHADOW = rgb("#b0a080")

KNOT = rgb("#d02828")
KNOT_DARK = rgb("#901818")
FRAME = rgb("#585048")
PIC = rgb("#90a8a0")
PIC2 = rgb("#b8c8b0")
BOARD = rgb("#f8f8f8")
BOARD_FRAME = rgb("#a8a8b0")
MARKER = rgb("#4060c0")

TOP = rgb("#303c3c")
TOP_SHADE = rgb("#283030")
TOP_LIGHT = rgb("#405050")
LINE = rgb("#f8f8f8")
SILVER = rgb("#c0c8d0")
SILVER_DARK = rgb("#808890")
LEG = rgb("#202028")
NETC = rgb("#202028")

ROBO_NET = rgb("#282020")
ROBO_POLE = rgb("#101010")
ROBO_BODY = rgb("#202830")
ROBO_BLUE = rgb("#3058b0")
ROBO_LIGHT = rgb("#6890e0")
ORANGE = rgb("#f89830")
ORANGE_DARK = rgb("#c86818")
ORANGE_LIGHT = rgb("#f8c878")
TUBE = rgb("#a8b8c8")
LABEL = rgb("#c8d840")
LABEL_DARK = rgb("#889020")
FACE = rgb("#101014")

TREAD = rgb("#383840")
TREAD_LIGHT = rgb("#707078")
TREAD_BELT = rgb("#202024")
SCREEN = rgb("#40a8e0")


def draw_room(c):
    # carpet with a soft repeating speckle (cheap in tiles)
    c.rect(0, 0, 256, 256, CARPET)
    for y in range(40, 160):
        for x in range(0, 240):
            k = (x * 7 + y * 13) % 16
            if k == 0:
                c.set(x, y, CARPET_DOT)
            elif k == 9:
                c.set(x, y, CARPET_LIGHT)
    # back wall
    c.rect(0, 0, 240, 40, WALL)
    for x in range(0, 240, 2):
        c.set(x, 39, WALL_SHADE)
    # ceiling soffit with a recessed light
    c.rect(0, 0, 240, 6, SOFFIT)
    c.hline(0, 239, 6, SOFFIT_SHADE)
    c.hline(0, 239, 7, WALL_SHADE)
    c.rect(150, 1, 10, 3, LAMP)
    c.hline(151, 158, 4, SOFFIT_SHADE)
    # baseboard
    c.rect(0, 37, 240, 3, BASEBOARD)
    c.hline(0, 239, 40, WALL_DARK)
    # whiteboard (left)
    c.rect(2, 14, 26, 17, BOARD_FRAME)
    c.rect(3, 15, 24, 15, BOARD)
    c.hline(6, 14, 18, MARKER)
    c.hline(6, 20, 21, MARKER)
    c.hline(6, 11, 24, KNOT)
    c.rect(4, 31, 22, 1, BOARD_FRAME)
    # red Chinese knot, hanging above the robot net
    kx = 120
    c.vline(kx, 7, 10, KNOT_DARK)
    for d in range(5):
        c.hline(kx - d, kx + d, 11 + d, KNOT)
        c.hline(kx - d, kx + d, 19 - d, KNOT)
    for (dx, dy) in ((-2, 13), (2, 13), (0, 15), (-2, 17), (2, 17)):
        c.set(kx + dx, dy, KNOT_DARK)
    c.ellipse(kx - 6, 15, 2, 2, KNOT)
    c.ellipse(kx + 7, 15, 2, 2, KNOT)
    c.vline(kx, 20, 22, KNOT_DARK)
    # three framed pictures (right)
    for i, px in enumerate((172, 184, 196)):
        c.rect(px, 13, 9, 11, FRAME)
        c.rect(px + 1, 14, 7, 9, PIC if i != 1 else PIC2)
        c.hline(px + 2, px + 6, 19, PIC2 if i != 1 else PIC)
    # pillar (left), pink column from ceiling to the carpet
    px0 = 31
    c.rect(px0, 0, 14, 96, WALL)
    c.vline(px0, 0, 95, WALL_DARK)
    c.vline(px0 + 13, 0, 95, WALL_DARK)
    c.rect(px0 + 10, 0, 3, 96, WALL_SHADE)
    c.hline(px0, px0 + 13, 96, WALL_DARK)
    for y in range(97, 100):
        c.hline(px0 + 1, px0 + 14, y, CARPET_SHADOW)
    # treadmill (right, against the wall)
    tx, ty = 196, 42
    c.rect(tx, ty + 6, 34, 30, TREAD)
    c.rect(tx + 4, ty + 9, 26, 25, TREAD_BELT)
    for y in range(ty + 11, ty + 34, 4):
        c.hline(tx + 5, tx + 28, y, TREAD)
    c.rect(tx + 2, ty - 6, 30, 12, TREAD)
    c.rect(tx + 6, ty - 4, 22, 5, TREAD_LIGHT)
    c.rect(tx + 12, ty - 3, 10, 3, SCREEN)
    c.vline(tx + 2, ty - 6, ty + 8, TREAD_LIGHT)
    c.vline(tx + 31, ty - 6, ty + 8, TREAD_LIGHT)
    c.hline(tx, tx + 34, ty + 36, CARPET_SHADOW)
    # table shadow on the carpet
    for y in range(Y1 + FACE_H + 1, Y1 + FACE_H + LEG_H + 3):
        c.hline(X0 + 2, X1 - 2, y, CARPET_SHADOW)


def draw_table(c):
    x0, x1, y0, y1 = X0, X1, Y0, Y1
    for lx in (x0 + 4, x1 - 6, (x0 + x1) // 2 - 1):
        c.rect(lx, y1 + FACE_H, 3, LEG_H + 1, LEG)
    c.rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, TOP)
    c.rect(x0, y0, x1 - x0 + 1, 3, TOP_SHADE)
    c.rect(x0, y1 - 3, x1 - x0 + 1, 2, TOP_LIGHT)
    c.hline(x0 + 1, x1 - 1, y0 + 1, LINE)
    c.hline(x0 + 1, x1 - 1, y1 - 1, LINE)
    c.vline(x0 + 1, y0 + 1, y1 - 1, LINE)
    c.vline(x1 - 1, y0 + 1, y1 - 1, LINE)
    c.vline((x0 + x1) // 2, y0 + 1, y1 - 1, LINE)
    c.hline(x0, x1, y0, OUTLINE)
    c.vline(x0, y0, y1 + FACE_H - 1, OUTLINE)
    c.vline(x1, y0, y1 + FACE_H - 1, OUTLINE)
    # the silver "Silver Extreme" edge band
    c.rect(x0 + 1, y1 + 1, x1 - x0 - 1, FACE_H - 1, SILVER)
    c.hline(x0 + 1, x1 - 1, y1 + FACE_H - 1, SILVER_DARK)
    c.hline(x0, x1, y1, LINE)
    c.hline(x0, x1, y1 + FACE_H, OUTLINE)
    # net
    c.hline(x0 + 1, x1 - 1, NET_Y + 1, TOP_SHADE)
    for y in range(NET_Y - 6, NET_Y + 1):
        for x in range(x0 - 2, x1 + 3):
            if (x + y) % 2 == 0 or y == NET_Y:
                c.set(x, y, NETC)
    c.hline(x0 - 2, x1 + 2, NET_Y - 7, LINE)
    c.hline(x0 - 2, x1 + 2, NET_Y - 8, OUTLINE)
    for px in (x0 - 4, x1 + 3):
        c.rect(px, NET_Y - 8, 2, 10, SILVER)
        c.vline(px - 1, NET_Y - 9, NET_Y + 2, OUTLINE)
        c.vline(px + 2, NET_Y - 9, NET_Y + 2, OUTLINE)


def draw_robot(c):
    """Catch net over the far end of the table, ball tray, shooter tower."""
    nx0, nx1, ntop, nbot = X0 - 6, X1 + 6, 24, Y0 + 3
    # mesh (every other pixel, so the wall shows through)
    for y in range(ntop, nbot):
        for x in range(nx0, nx1 + 1):
            if (x % 4 == 0) or (y % 4 == 0):
                c.set(x, y, ROBO_NET)
    # side wings running along the table's long edges
    for y in range(nbot, Y0 + 16):
        for x in list(range(X0 - 6, X0 - 1)) + list(range(X1 + 2, X1 + 7)):
            if (x % 4 == 0) or (y % 4 == 0):
                c.set(x, y, ROBO_NET)
    # frame poles: top bar, corner poles, braces from the top centre to the corners
    c.hline(nx0, nx1, ntop, ROBO_POLE)
    c.hline(nx0, nx1, ntop + 1, ROBO_POLE)
    for px in (nx0, nx1):
        c.vline(px, ntop, Y0 + 16, ROBO_POLE)
    mid = (X0 + X1) // 2
    span = mid - 6 - nx0          # the braces end exactly on the corner poles
    for i in range(0, span + 1):
        # a V: from low in the middle (by the robot) up to the top corners
        y = int(Y0 - 4 - i / span * (Y0 - 6 - ntop))
        y2 = int(Y0 - 4 - (i + 1) / span * (Y0 - 6 - ntop))
        c.vline(mid - 6 - i, min(y, y2 + 1), y + 1, ROBO_POLE)
        c.vline(mid + 6 + i, min(y, y2 + 1), y + 1, ROBO_POLE)
    # ball tray along the bottom with orange balls
    c.rect(nx0 + 1, Y0 - 3, nx1 - nx0 - 1, 6, ROBO_BODY)
    for bx in range(nx0 + 3, nx1 - 2, 4):
        if bx % 8 != 5:
            c.rect(bx, Y0 - 2, 3, 3, ORANGE)
            c.set(bx + 2, Y0, ORANGE_DARK)
    # shooter column: black, with a clear window of orange balls and a yellow-green label
    c.rect(mid - 5, 30, 11, Y0 - 28, OUTLINE)
    c.rect(mid - 4, 30, 9, Y0 - 28, ROBO_BODY)
    c.rect(mid - 2, 36, 5, Y0 - 38, TUBE)
    for by in range(37, Y0 - 3, 4):
        c.rect(mid - 1, by, 3, 3, ORANGE)
    c.vline(mid + 3, 36, Y0 - 3, LABEL)
    c.vline(mid - 3, 36, Y0 - 3, LABEL_DARK)
    # base clamp on the table edge
    c.rect(mid - 8, Y0 - 2, 17, 5, OUTLINE)
    c.rect(mid - 7, Y0 - 1, 15, 3, ROBO_BODY)


HEAD_ANGLES = (-2, -1, 0, 1, 2)


def draw_head(c, cx, cy, aim):
    """The shooter head (a sprite in the game), Newgy style: a round black head with a
    blue label on top and a round mouth the ball shoots out of. The head turns towards
    where the next ball goes: aim -2..2 = far left .. far right."""
    # head body
    c.ellipse(cx, cy, 9, 8, OUTLINE)
    c.ellipse(cx, cy, 8, 7, ROBO_BODY)
    # blue label across the top, sliding with the turn
    lx = cx + aim
    c.rect(lx - 6, cy - 7, 13, 4, ROBO_BLUE)
    c.hline(lx - 5, lx + 5, cy - 7, ROBO_LIGHT)
    c.rect(lx - 1, cy - 6, 3, 2, LINE)
    # the mouth: a ring with an orange ball inside, moved towards the aim
    mx, my = cx + aim * 2, cy + 2
    c.ellipse(mx, my, 4, 4, FACE)
    c.ellipse(mx, my, 3, 3, ROBO_BODY)
    c.ellipse(mx, my, 2, 2, ORANGE)
    c.set(mx - 1, my - 1, ORANGE_LIGHT)
    # a shine on the side turned away
    sx = cx - 6 if aim >= 0 else cx + 5
    c.vline(sx, cy - 2, cy + 3, TREAD_LIGHT)


def draw_basement():
    c = Canvas(256, 256)
    draw_room(c)
    return c


def draw_btable():
    c = Canvas(256, 256)
    draw_robot(c)
    draw_table(c)
    return c


def make_robot():
    c = Canvas(32, 32 * len(HEAD_ANGLES))
    for i, aim in enumerate(HEAD_ANGLES):
        draw_head(c, 16, i * 32 + 15, aim)
    return c


def main():
    room = draw_basement()
    table = draw_btable()
    pp.save_bg("pingpong_basement", room)
    pp.save_bg("pingpong_btable", table)
    robot = make_robot()
    pp.save_sprite("pingpong_robot", robot, 32)
    print("basement: %d colours, %d unique tiles" % (len(room.colors()), pp.count_tiles(room)))
    print("btable: %d colours, %d unique tiles" % (len(table.colors()), pp.count_tiles(table)))
    print("robot: %d colours" % len(robot.colors()))

    if len(sys.argv) > 1:
        d = sys.argv[1]
        os.makedirs(d, exist_ok=True)
        combo = Canvas(240, 160)
        combo.blit(room, 0, 0)
        combo.blit(table, 0, 0)
        frame = Canvas(32, 32)
        frame.blit(robot, 0, -64)
        combo.blit(frame, HEAD_X - 16, HEAD_Y - 15)
        save_png(os.path.join(d, "pp_basement.png"), combo, scale=3)
        save_png(os.path.join(d, "pp_robot.png"), robot, scale=6)
    print("Wrote the basement art to", os.path.normpath(pp.OUT))


if __name__ == "__main__":
    main()
