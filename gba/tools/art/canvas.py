"""Tiny pixel canvas + indexed BMP writer (standard library only).

The art generators in this folder draw the starting artwork with code and save it as
indexed BMP files, the format Butano's asset converter (grit) expects. The BMP files in
gba/graphics/ are the real source of truth: you can redraw any of them in Aseprite,
Usenti or similar, as long as you keep the same size and save as an indexed BMP
(16 colours for sprites, up to 256 for backgrounds, first palette colour = transparent).
"""
import struct

TRANSPARENT = None


def rgb(hexstr):
    """'#rrggbb' -> (r, g, b), snapped to the GBA's 5-bit-per-channel colours."""
    h = hexstr.lstrip("#")
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    return (r & 0xF8, g & 0xF8, b & 0xF8)


class Canvas:
    def __init__(self, w, h, fill=TRANSPARENT):
        self.w, self.h = w, h
        self.px = [fill] * (w * h)

    def get(self, x, y):
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.px[y * self.w + x]
        return TRANSPARENT

    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y * self.w + x] = c

    def rect(self, x, y, w, h, c):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set(xx, yy, c)

    def hline(self, x0, x1, y, c):
        for x in range(min(x0, x1), max(x0, x1) + 1):
            self.set(x, y, c)

    def vline(self, x, y0, y1, c):
        for y in range(min(y0, y1), max(y0, y1) + 1):
            self.set(x, y, c)

    def ellipse(self, cx, cy, rx, ry, c, outline=None):
        """Filled ellipse; optional 1px outline colour."""
        for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
            for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
                dx, dy = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
                if dx * dx + dy * dy <= 1.0:
                    self.set(x, y, c)
        if outline is not None:
            self.outline_color(c, outline)

    def outline_color(self, inside, outline):
        """Draw a 1px outline around every pixel of colour `inside` (on transparent/other pixels)."""
        marks = []
        for y in range(self.h):
            for x in range(self.w):
                if self.get(x, y) == inside:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    if self.get(x + dx, y + dy) == inside:
                        marks.append((x, y))
                        break
        for x, y in marks:
            self.set(x, y, outline)

    def ascii(self, x0, y0, rows, legend):
        """Paint ASCII art. Characters missing from the legend (and '.') are skipped."""
        for y, row in enumerate(rows):
            for x, ch in enumerate(row):
                if ch in legend and legend[ch] is not None:
                    self.set(x0 + x, y0 + y, legend[ch])

    def blit(self, src, dx, dy, flip_x=False):
        for y in range(src.h):
            for x in range(src.w):
                c = src.px[y * src.w + (src.w - 1 - x if flip_x else x)]
                if c is not TRANSPARENT:
                    self.set(dx + x, dy + y, c)

    def colors(self):
        seen = []
        for c in self.px:
            if c is not TRANSPARENT and c not in seen:
                seen.append(c)
        return seen

    def save_bmp(self, path, bpp=4, transparent=(255, 0, 255), palette=None):
        """Save as an indexed BMP. Index 0 is the transparent colour (shown as `transparent`).

        bpp=4 allows 15 colours + transparent, bpp=8 allows 255 + transparent.
        Pass `palette` (list of colours) to force a palette order.
        """
        pal = [transparent] + (list(palette) if palette else [c for c in self.colors()])
        if palette:
            for c in self.colors():
                if c not in pal:
                    raise ValueError(f"{path}: colour {c} not in the given palette")
        limit = 16 if bpp == 4 else 256
        if len(pal) > limit:
            raise ValueError(f"{path}: {len(pal) - 1} colours, but {bpp}bpp allows {limit - 1}")
        pal += [(0, 0, 0)] * (limit - len(pal))
        index = {c: i for i, c in reversed(list(enumerate(pal)))}
        index[TRANSPARENT] = 0
        row_bytes = (self.w * bpp + 7) // 8
        stride = (row_bytes + 3) & ~3
        pixels = bytearray()
        for y in range(self.h - 1, -1, -1):
            row = bytearray(stride)
            for x in range(self.w):
                i = index[self.px[y * self.w + x]]
                if bpp == 8:
                    row[x] = i
                else:
                    row[x // 2] |= (i << 4) if x % 2 == 0 else i
            pixels += row
        pal_bytes = b"".join(struct.pack("<BBBB", c[2], c[1], c[0], 0) for c in pal)
        offset = 14 + 40 + len(pal_bytes)
        header = struct.pack("<2sIHHI", b"BM", offset + len(pixels), 0, 0, offset)
        info = struct.pack("<IiiHHIIiiII", 40, self.w, self.h, 1, bpp, 0, len(pixels), 2835, 2835,
                           limit, 0)
        with open(path, "wb") as f:
            f.write(header + info + pal_bytes + pixels)


def write_json(path, text):
    with open(path, "w", newline="\n") as f:     # same line endings on Windows and Mac
        f.write(text.strip() + "\n")


def save_png(path, canvas, scale=1, background=(96, 96, 112)):
    """Preview helper: writes an RGB PNG (transparent pixels shown as `background`)."""
    import zlib
    w, h = canvas.w * scale, canvas.h * scale
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            c = canvas.px[(y // scale) * canvas.w + (x // scale)]
            raw.extend(bytes(background if c is None else c))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)
