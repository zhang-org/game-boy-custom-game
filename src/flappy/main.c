// Flappy Friend - a tiny Game Boy Color game (GBDK-2020)
// Flap with A / B / Up. Fly through the gaps. Don't hit the pipes or ground.

#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>
#include <rand.h>
#include "../common/gfx.h"
#include "../common/characters.h"
#include "art.h"

// ---- Tuning (positions/speeds in 1/16 pixel units) ----
#define GRAVITY        3
#define FLAP_VEL     (-44)
#define MAX_FALL      56
#define GAP_ROWS       6     // gap height in tiles (6 tiles = 48 px)
#define PIPE_EVERY    10     // a pipe every 10 tile columns (80 px)
#define FIRST_PIPE_COL 30    // first pipe at x = 240 px

#define PLAYER_X      40     // player's fixed screen x
#define PLAYFIELD_H  128     // the window (ground + HUD) covers the bottom 16 px
#define GROUND_Y     (PLAYFIELD_H - 16)

// ---- Tile indices ----
enum { T_SKY = 0, T_PIPE_L, T_PIPE_R, T_CAP_L, T_CAP_R, T_GRASS, T_DIRT, T_CLOUD_L, T_CLOUD_R };
#define T_FONT    16   // 0-9 then A-Z
#define SPR_BASE  64   // player sprite tiles (kept clear of BG tiles)

// ---- Colour palettes (CGB / Chromatic) ----
#define PAL_SKY    0
#define PAL_GROUND 1
#define PAL_CLOUD  2
static const palette_color_t bg_pals[] = {
    RGB(19,26,31), RGB(18,30,12), RGB(6,21,6),   RGB(2,8,4),     // sky + pipes
    RGB(20,13,6),  RGB(16,29,9),  RGB(5,19,5),   RGB(31,31,31),  // ground + HUD text
    RGB(19,26,31), RGB(31,31,31), RGB(24,27,31), RGB(2,8,4),     // clouds
};

enum { ST_TITLE, ST_PLAY, ST_OVER };

static uint8_t  state, is_cgb, anim, keys, prev_keys, pressed;
static uint8_t  who;             // selected character (index into characters[])
static uint16_t cam_x, next_col, score, best, timer;
static int16_t  py, vy;          // player y / velocity in 1/16 px
static uint8_t  gaps[8];         // gap top row for recent pipes (ring buffer)

// ---------------------------------------------------------------------------
// Graphics loading: turn the text art from art.h into Game Boy 2bpp tiles
// ---------------------------------------------------------------------------
static void load_graphics(void) {
    uint8_t buf[16], t, r;
    for (t = 0; t < BG_TILE_COUNT; t++) {
        for (r = 0; r < 8; r++) encode_row(bg_art[t * 8 + r], buf + r * 2);
        set_bkg_data(t, 1, buf);
    }
    load_font(T_FONT);
    for (t = 0; t < NUM_CHARACTERS; t++) {
        load_sprite16(characters[t].idle, SPR_BASE + t * 8);
        load_sprite16(characters[t].flap, SPR_BASE + t * 8 + 4);
    }
}

// ---------------------------------------------------------------------------
// Text + HUD
// ---------------------------------------------------------------------------
// on_win = 1 draws on the window (HUD), else on the background at camera-relative x
static void put_text(uint8_t on_win, uint8_t x, uint8_t y, const char *s, uint8_t blank, uint8_t pal) {
    uint8_t t, mx;
    while (*s) {
        t = glyph(*s, T_FONT, blank);
        mx = on_win ? x : (uint8_t)(((cam_x >> 3) + x) & 31);
        if (on_win) set_win_tiles(mx, y, 1, 1, &t); else set_bkg_tiles(mx, y, 1, 1, &t);
        if (is_cgb) {
            VBK_REG = 1;
            if (on_win) set_win_tiles(mx, y, 1, 1, &pal); else set_bkg_tiles(mx, y, 1, 1, &pal);
            VBK_REG = 0;
        }
        x++;
        s++;
    }
}

static void fmt3(uint16_t n, char *out) {
    if (n > 999) n = 999;
    out[0] = n >= 100 ? '0' + n / 100 : ' ';
    out[1] = n >= 10 ? '0' + (n / 10) % 10 : ' ';
    out[2] = '0' + n % 10;
    out[3] = 0;
}

static void update_hud(void) {
    char buf[4];
    fmt3(score, buf);
    put_text(1, 7, 1, buf, T_DIRT, PAL_GROUND);
    fmt3(best, buf);
    put_text(1, 17, 1, buf, T_DIRT, PAL_GROUND);
}

static void init_window(void) {
    uint8_t row[20], i;
    for (i = 0; i < 20; i++) row[i] = T_GRASS;
    set_win_tiles(0, 0, 20, 1, row);
    for (i = 0; i < 20; i++) row[i] = T_DIRT;
    set_win_tiles(0, 1, 20, 1, row);
    if (is_cgb) {
        for (i = 0; i < 20; i++) row[i] = PAL_GROUND;
        VBK_REG = 1;
        set_win_tiles(0, 0, 20, 1, row);
        set_win_tiles(0, 1, 20, 1, row);
        VBK_REG = 0;
    }
    put_text(1, 1, 1, "SCORE", T_DIRT, PAL_GROUND);
    put_text(1, 12, 1, "BEST", T_DIRT, PAL_GROUND);
    move_win(7, PLAYFIELD_H);
}

// ---------------------------------------------------------------------------
// Sound effects (raw APU registers)
// ---------------------------------------------------------------------------
static void sfx_flap(void)  { NR10_REG = 0x16; NR11_REG = 0xB0; NR12_REG = 0x72; NR13_REG = 0x00; NR14_REG = 0xC6; }
static void sfx_point(void) { NR21_REG = 0xA0; NR22_REG = 0xA2; NR23_REG = 0xC0; NR24_REG = 0xC7; }
static void sfx_hit(void)   { NR41_REG = 0x00; NR42_REG = 0xF3; NR43_REG = 0x61; NR44_REG = 0xC0; }

// ---------------------------------------------------------------------------
// World: an endless strip of tile columns streamed into the 32-wide BG map
// ---------------------------------------------------------------------------
static void draw_column(uint16_t wc) {
    uint8_t tiles[16], attrs[16], r, g, phase, crow;
    uint8_t is_pipe;
    phase = wc % PIPE_EVERY;
    is_pipe = (wc >= FIRST_PIPE_COL) && phase < 2;

    for (r = 0; r < 16; r++) { tiles[r] = T_SKY; attrs[r] = PAL_SKY; }

    if (is_pipe) {
        if (phase == 0) gaps[(wc / PIPE_EVERY) & 7] = 2 + rand() % 7;   // rows 2..8
        g = gaps[(wc / PIPE_EVERY) & 7];
        for (r = 0; r < 16; r++) {
            if (r >= g && r < g + GAP_ROWS) continue;
            if (r == g - 1 || r == g + GAP_ROWS) tiles[r] = phase ? T_CAP_R : T_CAP_L;
            else                                 tiles[r] = phase ? T_PIPE_R : T_PIPE_L;
        }
    } else if (phase == 5 || phase == 6) {
        crow = 1 + (wc / PIPE_EVERY) % 4;
        tiles[crow] = (phase == 5) ? T_CLOUD_L : T_CLOUD_R;
        attrs[crow] = PAL_CLOUD;
    }

    set_bkg_tiles(wc & 31, 0, 1, 16, tiles);
    if (is_cgb) {
        VBK_REG = 1;
        set_bkg_tiles(wc & 31, 0, 1, 16, attrs);
        VBK_REG = 0;
    }
}

static void stream_columns(void) {
    while (next_col <= (cam_x >> 3) + 21) {
        draw_column(next_col);
        next_col++;
    }
}

static uint8_t solid_at(uint16_t wx, uint8_t y) {
    uint16_t col = wx >> 3;
    uint8_t row, g;
    if (col < FIRST_PIPE_COL || (col % PIPE_EVERY) >= 2) return 0;
    g = gaps[(col / PIPE_EVERY) & 7];
    row = y >> 3;
    return row < g || row >= g + GAP_ROWS;
}

static void reset_world(void) {
    cam_x = 0;
    next_col = 0;
    stream_columns();          // draws columns 0..21 (no pipes there yet)
    score = 0;
    py = 56 << 4;
    vy = 0;
    anim = 0;
    update_hud();
}

// ---------------------------------------------------------------------------
// States
// ---------------------------------------------------------------------------
// "   <    GRANDMA   >  " on row 10 of the title screen
static void draw_picker(void) {
    char row[21];
    const char *name = characters[who].name;
    uint8_t i, len = 0;
    for (i = 0; i < 20; i++) row[i] = ' ';
    row[20] = 0;
    while (name[len]) len++;
    for (i = 0; i < len; i++) row[(20 - len) / 2 + i] = name[i];
    row[3] = '<';
    row[16] = '>';
    put_text(0, 0, 10, row, T_SKY, PAL_SKY);
}

static void go_title(void) {
    reset_world();
    put_text(0, 3, 3, "FLAPPY FRIEND", T_SKY, PAL_SKY);
    draw_picker();
    put_text(0, 6, 13, "PRESS A", T_SKY, PAL_SKY);
    state = ST_TITLE;
    timer = 0;
}

static void start_game(void) {
    initrand(sys_time);        // seed from how long the title was shown
    reset_world();             // also wipes the title text
    vy = FLAP_VEL;
    anim = 8;
    sfx_flap();
    state = ST_PLAY;
}

static void die(void) {
    sfx_hit();
    if (score > best) best = score;
    update_hud();
    put_text(0, 5, 4, "GAME OVER", T_SKY, PAL_SKY);
    state = ST_OVER;
    timer = 0;
}

static void apply_gravity(void) {
    vy += GRAVITY;
    if (vy > MAX_FALL) vy = MAX_FALL;
    py += vy;
    if (py < 0) { py = 0; vy = 0; }
}

static void update_play(void) {
    uint8_t y;
    uint16_t x0, x1;

    if (pressed & (J_A | J_B | J_UP)) {
        vy = FLAP_VEL;
        anim = 8;
        sfx_flap();
    }
    apply_gravity();

    cam_x++;
    stream_columns();

    // Passed a pipe?
    if (cam_x + 24 >= FIRST_PIPE_COL * 8 && (cam_x + 24) % (PIPE_EVERY * 8) == 0) {
        score++;
        sfx_point();
        update_hud();
    }

    // Collision (hitbox is the middle 10x10 of the 16x16 sprite)
    y = (uint8_t)(py >> 4);
    if (y + 13 >= PLAYFIELD_H) { die(); return; }
    x0 = cam_x + PLAYER_X + 3;
    x1 = cam_x + PLAYER_X + 12;
    if (solid_at(x0, y + 3) || solid_at(x1, y + 3) ||
        solid_at(x0, y + 12) || solid_at(x1, y + 12)) {
        die();
    }
}

static void update_over(void) {
    if (py < (GROUND_Y << 4)) {
        apply_gravity();
        if (py > (GROUND_Y << 4)) py = GROUND_Y << 4;
    }
    timer++;
    if (timer == 40) put_text(0, 6, 12, "PRESS A", T_SKY, PAL_SKY);
    if (timer > 40 && (pressed & (J_A | J_START))) go_title();
}

static void update_title(void) {
    uint8_t t = (timer >> 2) & 15;
    timer++;
    py = (52 + (t < 8 ? t : 15 - t)) << 4;   // gentle bob
    if (pressed & (J_LEFT | J_RIGHT)) {
        if (pressed & J_RIGHT) who = (who + 1) % NUM_CHARACTERS;
        else                   who = (who + NUM_CHARACTERS - 1) % NUM_CHARACTERS;
        draw_picker();
        sfx_point();
    }
    if (pressed & (J_A | J_START)) start_game();
}

static void draw_player(void) {
    uint8_t y = (uint8_t)(py >> 4);
    uint8_t base = SPR_BASE + who * 8 + (anim ? 4 : 0);
    if (anim) anim--;
    set_sprite_tile(0, base);
    set_sprite_tile(1, base + 2);
    set_sprite_prop(0, who);     // CGB palette = character number
    set_sprite_prop(1, who);
    move_sprite(0, PLAYER_X + 8, y + 16);
    move_sprite(1, PLAYER_X + 16, y + 16);
}

// ---------------------------------------------------------------------------
void main(void) {
    DISPLAY_OFF;
    is_cgb = (_cpu == CGB_TYPE);

    NR52_REG = 0x80;   // sound on
    NR50_REG = 0x77;
    NR51_REG = 0xFF;

    BGP_REG = 0xE4;
    OBP0_REG = 0xE4;
    if (is_cgb) {
        set_bkg_palette(0, 3, bg_pals);
        for (who = 0; who < NUM_CHARACTERS; who++)
            set_sprite_palette(who, 1, characters[who].pal);
        who = 0;
    }

    load_graphics();
    SPRITES_8x16;
    init_window();
    go_title();

    SHOW_BKG;
    SHOW_WIN;
    SHOW_SPRITES;
    DISPLAY_ON;

    while (1) {
        vsync();
        SCX_REG = (uint8_t)cam_x;

        keys = joypad();
        pressed = keys & ~prev_keys;
        prev_keys = keys;

        if (state == ST_TITLE)     update_title();
        else if (state == ST_PLAY) update_play();
        else                       update_over();

        draw_player();
    }
}
