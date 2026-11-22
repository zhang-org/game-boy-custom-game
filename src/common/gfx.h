// Shared helpers: turn text pixel-art into Game Boy tiles, load the font.
#ifndef GFX_H
#define GFX_H
#include <gb/gb.h>
#include <stdint.h>
#include "font.h"

// 8 chars ('.', '0'-'3') -> one 2bpp tile row (2 bytes)
static void encode_row(const char *s, uint8_t *out) {
    uint8_t lo = 0, hi = 0, i, v;
    for (i = 0; i < 8; i++) {
        v = (s[i] >= '0' && s[i] <= '3') ? (uint8_t)(s[i] - '0') : 0;
        lo = (lo << 1) | (v & 1);
        hi = (hi << 1) | (v >> 1);
    }
    out[0] = lo;
    out[1] = hi;
}

// 16x16 art -> 4 sprite tiles in 8x16-mode order: top-left, bottom-left, top-right, bottom-right
static void load_sprite16(const char * const *art, uint8_t first) {
    uint8_t buf[16], t, r, cx, cy;
    for (t = 0; t < 4; t++) {
        cx = (t >> 1) * 8;
        cy = (t & 1) * 8;
        for (r = 0; r < 8; r++) encode_row(art[cy + r] + cx, buf + r * 2);
        set_sprite_data(first + t, 1, buf);
    }
}

// 8x16 art (16 rows of 8) -> 2 sprite tiles
static void load_sprite8x16(const char * const *art, uint8_t first) {
    uint8_t buf[32], r;
    for (r = 0; r < 16; r++) encode_row(art[r], buf + r * 2);
    set_sprite_data(first, 2, buf);
}

// Font -> background tiles starting at `first`
static void load_font(uint8_t first) {
    uint8_t buf[16], t, r, b;
    for (t = 0; t < FONT_GLYPHS; t++) {
        for (r = 0; r < 8; r++) {
            b = (r < 7) ? (uint8_t)(font5x7[t * 7 + r] << 2) : 0;
            buf[r * 2] = b;
            buf[r * 2 + 1] = b;
        }
        set_bkg_data(first + t, 1, buf);
    }
}

static uint8_t glyph(char c, uint8_t font_first, uint8_t blank) {
    if (c >= '0' && c <= '9') return font_first + (c - '0');
    if (c >= 'A' && c <= 'Z') return font_first + 10 + (c - 'A');
    if (c == '<') return font_first + 36;
    if (c == '>') return font_first + 37;
    return blank;
}

#endif
