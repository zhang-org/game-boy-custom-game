"""Prints harness commands that walk from the start square to a place's doorstep on the town map.

    python3 tools/harness/route.py 4        # doorstep '4' (Gift Tower); 1..6 as in sb_games.h
Assumes a fresh walk from the start cell 'P' (the first map visit). One cell = 8 frames.
"""
import re
import sys
from collections import deque

layout = re.findall(r'"([^"]{32})"', open(__file__.replace("tools/harness/route.py", "include/sb_world_layout.h")).read())


def route(target):
    start = next((x, y) for y, row in enumerate(layout) for x, c in enumerate(row) if c == "P")
    goal = next((x, y) for y, row in enumerate(layout) for x, c in enumerate(row) if c == target)
    prev = {start: None}
    q = deque([start])
    while q:
        cur = q.popleft()
        if cur == goal:
            break
        for dx, dy, name in ((0, -1, "UP"), (0, 1, "DOWN"), (-1, 0, "LEFT"), (1, 0, "RIGHT")):
            nx, ny = cur[0] + dx, cur[1] + dy
            c = layout[ny][nx]
            if (nx, ny) not in prev and c not in "T~#oBN":
                prev[(nx, ny)] = (cur, name)
                q.append((nx, ny))
    steps = []
    cur = goal
    while prev[cur]:
        cur, name = prev[cur]
        steps.append(name)
    steps.reverse()
    out = []
    for name in steps:
        if out and out[-1][0] == name:
            out[-1][1] += 1
        else:
            out.append([name, 1])
    return [f"hold {n} {k * 8}" for n, k in out]


if __name__ == "__main__":
    print("\n".join(route(sys.argv[1])))
