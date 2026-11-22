"""Combines harness screenshots into one image so you can look at many at once.

    python3 tools/harness/contact_sheet.py OUT.png shot1.png shot2.png ...   (2x scale, 2 columns)
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "art"))
from canvas import Canvas, save_png  # noqa: E402


def read_png(path):
    d = open(path, "rb").read()
    pos, idat = 8, b""
    while pos < len(d):
        n, = struct.unpack(">I", d[pos:pos + 4])
        kind, body = d[pos + 4:pos + 8], d[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, _bd, ct = struct.unpack(">IIBB", body[:10])
        elif kind == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    ch = {2: 3, 6: 4}[ct]
    stride = w * ch
    prev = bytearray(stride)
    out, p = [], 0
    for _ in range(h):
        f, line = raw[p], bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - ch] if i >= ch else 0
            b, c = prev[i], (prev[i - ch] if i >= ch else 0)
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out.append([tuple(line[x * ch:x * ch + 3]) for x in range(w)])
        prev = line
    return w, h, out


def main():
    dest, files = sys.argv[1], sys.argv[2:]
    cols = 2 if len(files) > 1 else 1
    W, H = 240, 160
    sheet = Canvas((W + 4) * cols, (H + 4) * ((len(files) + cols - 1) // cols), (255, 255, 255))
    for i, f in enumerate(files):
        w, h, px = read_png(f)
        for y in range(h):
            for x in range(w):
                sheet.set((i % cols) * (W + 4) + x, (i // cols) * (H + 4) + y, px[y][x])
    save_png(dest, sheet, scale=2)


if __name__ == "__main__":
    main()
