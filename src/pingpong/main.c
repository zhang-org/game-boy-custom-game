// Ping Pong Pals (Game Boy Color version, for the ModRetro Chromatic).
// A cut-down port of the GBA game in gba/src/games/pingpong.cpp: same rules and the same skills,
// simpler graphics. Table tennis seen from behind your end of the table.
//
//   Serve:  HOLD A (topspin, fast) or B (backspin, slow, low bounce). A marker climbs the bar on the left;
//           LET GO in the red box for a perfect serve (green = strong, gold = ok). Too slow = try again.
//   Rally:  move left/right (and up/down) to the ball: you swing by yourself.
//   Smash:  HOLD A (forehand, aims left) or B (backhand, aims right) to build power (you move slower),
//           LET GO as the ball reaches you. Full power + good timing = PERFECT (freeze, flash, shake).
//           Hold too long and you only block it back weakly.
//   The CPU turns to the side it will hit to (its paddle swings out): forehand = to its right.
// Rules: the serve bounces on your side first; first to 5 points, win by 2. Three difficulty levels.
// Best result per level is saved on the cartridge (battery save: see lcc_flags.txt).

#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>
#include <string.h>
#include <rand.h>
#include "../common/gfx.h"
#include "family.h"           // the 10 family members (made by make_family.py)
#include "art.h"
#include "song.h"

// ---------- World (game units; ball & players stored in 1/16 units) ----------
#define TABLE_LEN    176     // z: 0 = your end, 176 = far end
#define TABLE_HALF_W 64      // x: -64 .. 64
#define NET_Z        88
#define NET_H        6
#define GRAV         2       // 1/16 units per frame^2
#define NEAR_Z_HOME  (-14)
#define FAR_Z        190
#define WIN_SCORE    5       // first to 5 ...
#define WIN_BY       2       // ... with a 2-point lead
#define PLAYER_SPEED 40
#define CHARGE_SPEED 14      // you move this slowly while charging a smash

#define NEAR 0               // you
#define FAR  1               // the CPU

// ---------- Difficulty ----------
typedef struct {
    const char *name;
    uint8_t cpu_speed;       // 1/16 units per frame
    uint8_t reaction;        // frames before it starts running
    uint8_t defend, risky, err_slope;   // CPU pressure thresholds (see cpu_predict)
    uint8_t tell;            // it shows forehand/backhand this many frames before hitting
    uint8_t charge_frames;   // frames to fill your smash meter
    int8_t  sweet_lo, sweet_hi;  // PERFECT: the ball is this far in front of you (units) when you let go
    uint8_t perfect_return;  // % chance it still blocks back a PERFECT smash ...
    uint8_t close_return;    // ... extra % when the smash goes close to where it stands
    uint8_t serve_speed;     // serve marker speed (1/16 px per frame)
    uint8_t zp, zs, zo;      // serve zones: within this many px of the middle = perfect, strong, ok
    uint8_t perfect_floater; // % chance a perfect serve comes back as an easy floater
    uint8_t strong_floater;
} level_t;

static const level_t levels[3] = {
    //  name      spd react def risky slope tell chg  lo  hi  ret close  srv zp zs zo  pf  sf
    { "EASY",     16, 14,   18, 50,   2,    30,  30,  3,  20,  5,  20,   10,  2, 8, 14, 100, 40 },
    { "MEDIUM",   20, 12,   20, 45,   3,    22,  24,  6,  16,  30, 45,   13,  1, 7, 12, 100, 30 },
    { "HARD",     24, 9,    24, 40,   4,    16,  24,  8,  14,  45, 40,   17,  1, 5, 10, 85,  20 },
};
#define PERFECT_CHARGE 75    // % of the smash meter needed for a PERFECT (the gold part)

// ---------- Screen mapping (pseudo 3D) ----------
#define FAR_Y   44           // screen y of the far table edge
#define NEAR_Y  116          // screen y of the near table edge
#define NET_Y   80
#define NET_TOP 76

// ---------- Tiles ----------
#define TBL_MAX        150   // BG tiles 0..149 = generated table art
#define T_METER        150   // BG tiles 150..157: the serve bar (redrawn every serve)
#define SPR_NEAR_FRONT 160
#define SPR_NEAR_BACK  164
#define SPR_FAR_FRONT  168
#define SPR_FAR_FLAP   172
#define SPR_BODY       176
#define SPR_PADDLE     180
#define SPR_BALL       182
#define SPR_SHADOW     184
#define SPR_MARKER     186
#define SPR_CHARGE     188   // 4 tiles: the smash meter (redrawn as it fills)
#define T_FONT         200

// ---------- OAM slots (lower = drawn on top) ----------
#define OAM_BALL   0
#define OAM_NEAR   1         // paddle, head L/R, body L/R
#define OAM_FAR    6
#define OAM_SHADOW 11
#define OAM_MARKER 12
#define OAM_CHARGE 13        // 13, 14
#define OAM_TRAIL  15        // 15, 16

// ---------- Palettes ----------
#define PAL_METER  2         // sprite palette: smash meter
#define PAL_BALL   4
#define PAL_PADDLE 5
#define PAL_SHIRT  6         // 6 = you, 7 = CPU
#define BG_PAL_BAR 1         // background palette: the serve bar

// ---------- Serve bar ----------
#define BAR_ROW     7        // the bar fills tile rows 7..14 of column 0 (64 px)
#define BAR_TOP_Y   (BAR_ROW * 8)
#define BAR_TRACK   58       // the marker climbs this many px (from the bottom)
#define ZONE_MIN    26       // the red box sits somewhere between here ...
#define ZONE_MAX    50       // ... and here (px up the bar)
#define TOSS_H      30       // the toss goes this high and comes back to paddle height in the red box
#define PADDLE_H    12

enum { SHOT_TOP, SHOT_SLICE, SHOT_LOB, SHOT_SERVE };
enum { ST_TITLE, ST_SERVE, ST_RALLY, ST_POINT, ST_OVER };
enum { EV_NONE, EV_BOUNCE, EV_NET, EV_FLOOR };
enum { Q_WEAK, Q_OK, Q_STRONG, Q_PERFECT };

typedef struct { int16_t x, z, h, vx, vz, vh, serve_vh; uint8_t spin, serve_spin; } ball_t;
typedef struct { int16_t x, z; uint8_t who, swing; } player_t;

static const palette_color_t bg_pal[] = {
    RGB(17,10,6), RGB(4,11,23), RGB(2,4,10), RGB(31,31,31),     // floor, table, dark, white
};
static const palette_color_t bar_pal[]    = { RGB(3,4,9), RGB(31,6,6), RGB(8,26,8), RGB(31,25,4) };
static const palette_color_t white_pal[]  = { RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31) };
static const palette_color_t ball_pal[]   = { 0, RGB(31,31,31), RGB(22,22,25), RGB(3,4,9) };
static const palette_color_t paddle_pal[] = { 0, RGB(29,5,5),   RGB(24,16,8),  RGB(6,3,2) };
static const palette_color_t meter_pal[]  = { 0, RGB(3,4,9),    RGB(8,26,8),   RGB(31,25,4) };
static const palette_color_t shirt_col[2] = { RGB(6,14,31), RGB(30,8,8) };

// How each shot behaves when it bounces (x/16): height kept, forward speed kept
static const uint8_t bounce_e[4]    = { 11, 7, 12, 11 };
static const uint8_t bounce_kick[4] = { 17, 11, 14, 16 };

// Shot flight time in frames (bigger = slower ball)
#define T_TOP    52
#define T_SLICE  62
#define T_LOB    80
#define T_SERVE  42

// CPU: each of your shots gets a score from its power and how far the CPU must run.
//   score <  defend: normal return ... >= defend: a slow, high floater (smash it!) ... >= risky: may miss.
#define CPU_POWER_W      5
#define CPU_STRETCH_FREE 35
enum { MISS_NONE, MISS_WIDE, MISS_LONG };

static ball_t   ball;
static player_t pl[2];
static uint8_t  is_cgb, state, keys, prev_keys, pressed, released, timer, tossing, ball_live;
static uint8_t  server, last_hitter, serve_phase, server_bounced, recv_bounces, pt_winner;
static uint8_t  title_row, level_choice;
static uint8_t  score[2];
static int16_t  cpu_tx;
static uint8_t  cpu_delay, cpu_mistake, cpu_weak, pressure;
static int8_t   cpu_side;                // +1 forehand (to its right on screen), -1 backhand
static int16_t  sx, sy;                  // result of project()
static const level_t *lv;

// Serve bar
static uint8_t  aiming, wait_release, serve_button, serve_quality;
static int16_t  mark, zone;              // 1/16 px from the bottom of the bar
// Smash
static uint8_t  charging, charge_button, charge, charge_full, last_perfect;
// Effects
static uint8_t  freeze, flash, shake, trail;
static int16_t  trail_x[8], trail_y[8];
static uint8_t  hint_timer, note_timer;

static uint8_t  table_map[360];
static uint8_t  tstore[TBL_MAX * 16];
static uint8_t  thash[TBL_MAX];
static uint8_t  edge_l[NEAR_Y + 1], edge_r[NEAR_Y + 1];
static char     back_rows[16][17];
static const char *back_ptr[16];

// ---------------------------------------------------------------------------
// Saved records: the best result per difficulty (battery-backed cartridge RAM)
// ---------------------------------------------------------------------------
#define SAVE_MAGIC 0x5A
typedef struct { uint8_t magic, version; uint16_t best[3]; uint8_t who[3]; uint8_t sum; } save_t;
static save_t save;

static uint8_t save_sum(void) {
    uint8_t s = 0x37, i;
    const uint8_t *p = (const uint8_t *)&save;
    for (i = 0; i < sizeof(save_t) - 1; i++) s = (uint8_t)((s << 1) | (s >> 7)) ^ p[i];
    return s;
}

static void save_load(void) {
    ENABLE_RAM;
    SWITCH_RAM(0);
    memcpy(&save, (const void *)0xA000, sizeof(save_t));
    DISABLE_RAM;
    if (save.magic != SAVE_MAGIC || save.version != 1 || save.sum != save_sum()) {
        memset(&save, 0, sizeof(save_t));
        save.magic = SAVE_MAGIC;
        save.version = 1;
    }
}

static void save_write(void) {
    save.sum = save_sum();
    ENABLE_RAM;
    SWITCH_RAM(0);
    memcpy((void *)0xA000, &save, sizeof(save_t));
    DISABLE_RAM;
}

// A result as one number that sorts correctly: any win beats any loss, then the bigger margin.
static uint16_t result_value(uint8_t my, uint8_t cpu) {
    if (my > cpu) return 20000 + (uint16_t)(99 - cpu) * 100 + my;
    return 1 + (uint16_t)my * 100 + (99 - cpu);
}

static void result_points(uint16_t v, uint8_t *my, uint8_t *cpu) {
    if (v >= 20000) { v -= 20000; *cpu = 99 - v / 100; *my = v % 100; }
    else { v -= 1; *my = v / 100; *cpu = 99 - v % 100; }
}

// ---------------------------------------------------------------------------
// Sound (channel 2 = sound effects; channels 1, 3, 4 = music)
// ---------------------------------------------------------------------------
static void sfx_bounce(void) { NR21_REG = 0xBC; NR22_REG = 0x81; NR23_REG = 0x80; NR24_REG = 0xC7; }
static void sfx_paddle(void) { NR21_REG = 0xB8; NR22_REG = 0xC1; NR23_REG = 0x00; NR24_REG = 0xC6; }
static void sfx_point(void)  { NR21_REG = 0x80; NR22_REG = 0xF3; NR23_REG = 0x00; NR24_REG = 0x87; }
static void sfx_net(void)    { NR41_REG = 0x30; NR42_REG = 0x91; NR43_REG = 0x70; NR44_REG = 0xC0; }
static void sfx_smash(void)  { NR41_REG = 0x30; NR42_REG = 0xC2; NR43_REG = 0x42; NR44_REG = 0xC0; sfx_paddle(); }
static void sfx_perfect(void){ NR41_REG = 0x20; NR42_REG = 0xF3; NR43_REG = 0x31; NR44_REG = 0xC0;
                               NR21_REG = 0x80; NR22_REG = 0xF2; NR23_REG = 0xC0; NR24_REG = 0x87; }
static void sfx_ready(void)  { NR21_REG = 0x40; NR22_REG = 0x71; NR23_REG = 0xD0; NR24_REG = 0xC7; }
static void sfx_miss(void)   { NR21_REG = 0x80; NR22_REG = 0x63; NR23_REG = 0x20; NR24_REG = 0x85; }

// ---------------------------------------------------------------------------
// 3D -> screen.  x, z, h in whole units.
// ---------------------------------------------------------------------------
static void project(int16_t x, int16_t z, int16_t h) {
    int16_t hw = 72 - (z * 5) / 22;           // half table width in pixels at depth z
    sx = 80 + (x * hw) / 64;
    sy = (NEAR_Y - (z * 9) / 22) - (h * hw) / 72;
}

// ---------------------------------------------------------------------------
// Table background: computed pixel by pixel at boot, then de-duplicated into tiles
// ---------------------------------------------------------------------------
static uint8_t table_pixel(uint8_t px, uint8_t py) {
    uint8_t l, r;
    if (py >= FAR_Y && py <= NEAR_Y) {
        l = edge_l[py];
        r = edge_r[py];
        if (py >= NET_TOP && py <= NET_Y && px + 2 >= l && px <= r + 2) {
            if (py == NET_TOP) return 3;                  // white tape
            if (px < l || px > r) return 2;               // posts
            return ((px + py) & 1) ? 2 : 1;               // mesh
        }
        if (px < l || px > r) return 0;
        if (px == l || px == r || py == FAR_Y || py == NEAR_Y || px == 80) return 3;
        return 1;
    }
    l = edge_l[NEAR_Y];
    r = edge_r[NEAR_Y];
    if (py > NEAR_Y && py <= NEAR_Y + 4) return (px >= l && px <= r) ? 2 : 0;   // table front
    if (py > NEAR_Y + 4 && py <= NEAR_Y + 22 &&
        ((px >= l + 10 && px <= l + 12) || (px + 12 >= r && px + 10 <= r))) return 2;  // legs
    return 0;
}

static void build_table(void) {
    uint8_t tx, ty, r, c, v, lo, hi, i, n, hsh, px0, py0;
    uint8_t buf[16];
    int16_t hw;

    for (r = FAR_Y; r <= NEAR_Y; r++) {
        hw = 32 + ((int16_t)(r - FAR_Y) * 40) / 72;
        edge_l[r] = 80 - hw;
        edge_r[r] = 79 + hw;
    }

    memset(tstore, 0, 16);          // tile 0 = plain floor
    thash[0] = 0;
    n = 1;

    for (ty = 0; ty < 18; ty++) {
        for (tx = 0; tx < 20; tx++) {
            if (ty < 5) { table_map[ty * 20 + tx] = 0; continue; }
            px0 = tx * 8;
            py0 = ty * 8;
            hsh = 0;
            for (r = 0; r < 8; r++) {
                lo = hi = 0;
                for (c = 0; c < 8; c++) {
                    v = table_pixel(px0 + c, py0 + r);
                    lo = (lo << 1) | (v & 1);
                    hi = (hi << 1) | (v >> 1);
                }
                buf[r * 2] = lo;
                buf[r * 2 + 1] = hi;
                hsh += lo + (hi << 1) + r;
            }
            for (i = 0; i < n; i++)
                if (thash[i] == hsh && memcmp(tstore + i * 16, buf, 16) == 0) break;
            if (i == n) {
                if (n < TBL_MAX) { memcpy(tstore + n * 16, buf, 16); thash[n] = hsh; n++; }
                else i = 0;
            }
            table_map[ty * 20 + tx] = i;
        }
    }
    set_bkg_data(0, n, tstore);
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------
static void put_text(uint8_t x, uint8_t y, const char *s) {
    uint8_t t;
    while (*s) {
        if (*s != ' ') {
            t = glyph(*s, T_FONT, 0);
            set_bkg_tiles(x, y, 1, 1, &t);
        }
        x++;
        s++;
    }
}

static void restore_row(uint8_t y) { set_bkg_tiles(0, y, 20, 1, &table_map[y * 20]); }

static void show_msg(uint8_t y, const char *a, const char *b) {
    char buf[21];
    uint8_t n = 0;
    while (*a && n < 20) buf[n++] = *a++;
    while (b && *b && n < 20) buf[n++] = *b++;
    buf[n] = 0;
    restore_row(y);
    put_text((20 - n) / 2, y, buf);
}

// A message on row 16 that goes away by itself after a while
static void note(const char *s) {
    show_msg(16, s, 0);
    note_timer = 70;
}

// A short note on the bottom row that goes away by itself ("EARLY", "LATE" ...)
static void hint(const char *s) {
    show_msg(17, s, 0);
    hint_timer = 50;
}

static uint8_t name_score(char *buf, uint8_t who, uint8_t sc) {
    const char *nm = characters[who].name;
    uint8_t n = 0;
    while (*nm) buf[n++] = *nm++;
    buf[n++] = ' ';
    if (sc >= 10) buf[n++] = '0' + sc / 10;
    buf[n++] = '0' + sc % 10;
    buf[n] = 0;
    return n;
}

static void draw_score(void) {
    char buf[12];
    uint8_t n;
    restore_row(0);
    name_score(buf, pl[NEAR].who, score[NEAR]);
    put_text(1, 0, buf);
    n = name_score(buf, pl[FAR].who, score[FAR]);
    put_text(19 - n, 0, buf);
}

// "5-2" into buf
static void score_text(char *buf, uint8_t a, uint8_t b) {
    uint8_t n = 0;
    if (a >= 10) buf[n++] = '0' + a / 10;
    buf[n++] = '0' + a % 10;
    buf[n++] = ' ';
    if (b >= 10) buf[n++] = '0' + b / 10;
    buf[n++] = '0' + b % 10;
    buf[n] = 0;
}

// ---------------------------------------------------------------------------
// Characters
// ---------------------------------------------------------------------------
static uint8_t filled(const char * const *art, int8_t r, int8_t c) {
    if (r < 0 || r > 15 || c < 0 || c > 15) return 0;
    return art[r][c] != '.';
}

// Back of the head (for you, seen from behind): skin and face details become hair
static void make_back(const char * const *art) {
    int8_t r, c;
    char ch;
    for (r = 0; r < 16; r++) {
        for (c = 0; c < 16; c++) {
            ch = art[r][c];
            if (ch == '1') ch = '2';
            else if (ch == '3' && filled(art, r - 1, c) && filled(art, r + 1, c) &&
                     filled(art, r, c - 1) && filled(art, r, c + 1)) ch = '2';
            back_rows[r][c] = ch;
        }
        back_rows[r][16] = 0;
        back_ptr[r] = back_rows[r];
    }
}

static void load_characters(void) {
    const character_t *a = &characters[pl[NEAR].who];
    const character_t *b = &characters[pl[FAR].who];
    palette_color_t sp[4];

    load_sprite16(a->idle, SPR_NEAR_FRONT);
    make_back(a->idle);
    load_sprite16(back_ptr, SPR_NEAR_BACK);
    load_sprite16(b->idle, SPR_FAR_FRONT);
    load_sprite16(b->flap, SPR_FAR_FLAP);

    if (is_cgb) {
        set_sprite_palette(0, 1, a->pal);
        set_sprite_palette(1, 1, b->pal);
        sp[0] = 0; sp[3] = RGB(4,4,8);
        sp[1] = shirt_col[0]; sp[2] = a->pal[1];
        set_sprite_palette(PAL_SHIRT, 1, sp);
        sp[1] = shirt_col[1]; sp[2] = b->pal[1];
        set_sprite_palette(PAL_SHIRT + 1, 1, sp);
    }
}

// ---------------------------------------------------------------------------
// Ball physics
// ---------------------------------------------------------------------------
static uint8_t over_table(const ball_t *b) {
    return b->x >= -(TABLE_HALF_W << 4) && b->x <= (TABLE_HALF_W << 4) &&
           b->z >= 0 && b->z <= (TABLE_LEN << 4);
}

static uint8_t step_ball(ball_t *b) {
    int16_t oz = b->z;
    b->vh -= GRAV;
    b->x += b->vx;
    b->z += b->vz;
    b->h += b->vh;

    // Crossed the net plane too low?
    if (((oz < (NET_Z << 4)) != (b->z < (NET_Z << 4))) &&
        b->h < (NET_H << 4) && b->h > -(4 << 4) &&
        b->x > -(68 << 4) && b->x < (68 << 4)) return EV_NET;

    if (b->h <= 0 && b->vh < 0) {
        if (over_table(b) && b->h > -(6 << 4)) {
            b->h = 0;
            if (b->serve_vh) {               // serve: second hop computed in advance
                b->vh = b->serve_vh;
                b->serve_vh = 0;
                b->spin = b->serve_spin;
            } else {
                b->vh = (-b->vh * bounce_e[b->spin]) >> 4;
                b->vz = (b->vz * bounce_kick[b->spin]) >> 4;
            }
            return EV_BOUNCE;
        }
        if (b->h < -(40 << 4)) return EV_FLOOR;
    }
    if (b->z < -(70 << 4) || b->z > (250 << 4) || b->x < -(150 << 4) || b->x > (150 << 4))
        return EV_FLOOR;
    return EV_NONE;
}

// Send the ball to land on (tx, tz) after about T frames, raising the arc if it would hit the net
static void aim_ball(int16_t tx, int16_t tz, uint8_t T) {
    int16_t tn, hn;
    for (;;) {
        ball.vx = ((tx << 4) - ball.x) / T;
        ball.vz = ((tz << 4) - ball.z) / T;
        ball.vh = (T + 1) - ball.h / T;                  // GRAV = 2
        if (ball.vz == 0) break;
        tn = ((NET_Z << 4) - ball.z) / ball.vz;          // frames until the net
        hn = ball.h + ball.vh * tn - tn * (tn + 1);      // height there
        if (hn >= ((NET_H + 3) << 4) || T > 90) break;
        T += 3;
    }
    ball.serve_vh = 0;
}

static int16_t iabs(int16_t v) { return v < 0 ? -v : v; }

// ---------------------------------------------------------------------------
// CPU brain
// ---------------------------------------------------------------------------
static void cpu_predict(void) {
    ball_t b = ball;
    uint8_t i, ev;
    int16_t need, reach, frames, stretch, sc, chance;
    for (i = 0; i < 200; i++) {
        ev = step_ball(&b);
        if (b.vz > 0 && b.z >= ((FAR_Z - 6) << 4)) break;
        if (ev == EV_FLOOR || ev == EV_NET) break;
    }

    cpu_delay = lv->reaction + (pressure >> 1);
    cpu_tx = b.x + ((int16_t)(rand() % 9) - 4) * 16;        // tiny aim wobble

    // How stretched is the CPU? distance to run vs. how far it can run in time
    need = iabs((b.x - pl[FAR].x) >> 4);
    frames = (int16_t)i - cpu_delay;
    if (frames < 0) frames = 0;
    reach = ((frames * lv->cpu_speed) >> 4) + 28;           // + paddle reach
    stretch = (need * 100) / reach;                         // 100+ = can't get there

    sc = pressure * CPU_POWER_W;
    if (stretch > CPU_STRETCH_FREE) sc += stretch - CPU_STRETCH_FREE;

    cpu_mistake = MISS_NONE;
    cpu_weak = 0;
    if (sc >= lv->defend) {
        cpu_weak = 10 + ((sc - lv->defend) >> 1);           // weaker, slower return
        if (cpu_weak > 30) cpu_weak = 30;
    }
    if (sc >= lv->risky && rand() % 100 < (sc - lv->risky) * lv->err_slope)
        cpu_mistake = (rand() & 1) ? MISS_WIDE : MISS_LONG;

    // A PERFECT smash usually wins, but it can be blocked back, mostly when it comes right at the CPU.
    if (last_perfect && cpu_mistake != MISS_NONE && stretch <= 100) {
        chance = lv->perfect_return + (need < 20 ? lv->close_return : 0);
        if (rand() % 100 < chance) { cpu_mistake = MISS_NONE; cpu_weak = 30; }
    }

    // A great serve is rewarded with an easy floater.
    if (serve_phase && cpu_mistake == MISS_NONE) {
        chance = serve_quality == Q_PERFECT ? lv->perfect_floater : serve_quality == Q_STRONG ? lv->strong_floater : 0;
        if (rand() % 100 < chance) cpu_weak = 30;
    }

    // Forehand (ball goes to its right on screen) or backhand: decided now, shown before it hits.
    cpu_side = (rand() & 1) ? 1 : -1;
    if (cpu_tx > (30 << 4)) cpu_side = -1;                   // near the edge only one fits
    if (cpu_tx < -(30 << 4)) cpu_side = 1;
}

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------
static uint8_t deuce(void) { return score[0] >= WIN_SCORE - 1 && score[1] >= WIN_SCORE - 1; }

static uint8_t match_over(void) {
    uint8_t a = score[0], b = score[1];
    return (a >= WIN_SCORE || b >= WIN_SCORE) && iabs((int16_t)a - b) >= WIN_BY;
}

static void stop_charging(void) { charging = 0; charge = 0; charge_full = 0; }

static void award(uint8_t s) {
    score[s]++;
    pt_winner = s;
    state = ST_POINT;
    timer = 0;
    stop_charging();
    sfx_point();
    draw_score();
    note_timer = 0;
    show_msg(16, "POINT ", characters[pl[s].who].name);
}

static void rules_event(uint8_t ev) {
    uint8_t side, other = !last_hitter;
    if (ev == EV_BOUNCE) {
        sfx_bounce();
        trail = 0;
        side = ball.z < (NET_Z << 4) ? NEAR : FAR;
        if (serve_phase) {
            if (side == last_hitter) {
                if (server_bounced) award(other); else server_bounced = 1;
            } else if (!server_bounced) award(other);
            else { serve_phase = 0; recv_bounces = 1; }
        } else if (side == last_hitter) award(other);
        else if (++recv_bounces >= 2) award(last_hitter);
    } else if (ev == EV_NET) {
        ball.vz = -(ball.vz >> 2);
        sfx_net();
        award(other);
    } else if (ev == EV_FLOOR) {
        award(recv_bounces ? last_hitter : other);   // receiver missed / shot went out
    }
}

static uint8_t can_hit(uint8_t s) {
    return state == ST_RALLY && last_hitter != s && recv_bounces >= 1 && !serve_phase;
}

static uint8_t in_zone(uint8_t s) {
    int16_t dz = (ball.z - pl[s].z) >> 4;
    int16_t dx = (ball.x - pl[s].x) >> 4;
    int16_t h  = ball.h >> 4;
    if (s == NEAR) {
        if (ball.vz > 0 || dz < -14 || dz > 24) return 0;
        return dx >= -32 && dx <= 32 && h >= -28 && h <= 48;   // you get a bigger reach
    }
    if (ball.vz < 0 || dz > 12 || dz < -22) return 0;
    return dx >= -28 && dx <= 28 && h >= -24 && h <= 44;
}

static void hit_ball(uint8_t s, uint8_t shot, int16_t tx, int16_t depth, uint8_t T) {
    ball.spin = shot;
    aim_ball(tx, s == NEAR ? NET_Z + depth : NET_Z - depth, T);
    last_hitter = s;
    recv_bounces = 0;
    serve_phase = 0;
    pl[s].swing = 12;
    if (s == NEAR) cpu_predict();
    else {
        // Walking back to the middle: after a hard shot it needs a moment first.
        cpu_delay = pressure > 6 ? (pressure - 6) * 3 : 0;
        pressure = 0;
        last_perfect = 0;
    }
}

// ---------------------------------------------------------------------------
// Serving
// ---------------------------------------------------------------------------
static void hold_ball(void) {
    player_t *p = &pl[server];
    ball.x = p->x + (server == NEAR ? (10 << 4) : -(10 << 4));
    ball.z = p->z + (server == NEAR ? (6 << 4) : -(6 << 4));
    ball.h = PADDLE_H << 4;
    ball.vx = ball.vz = ball.vh = ball.serve_vh = 0;
}

// Serve: first bounce on the server's side (b1z), second on the receiver's side (tx, b2z)
static void serve_ball(uint8_t s, int16_t tx, int16_t b2z, uint8_t power, uint8_t spin) {
    int16_t b1z = s == NEAR ? 40 : TABLE_LEN - 40;
    uint8_t T2 = T_SERVE - power + (spin == SHOT_SLICE ? 8 : 0);
    int16_t vz = ((b2z - b1z) << 4) / T2;
    int16_t t1 = ((b1z << 4) - ball.z) / vz;
    if (t1 < 4) t1 = 4;
    ball.vz = vz;
    ball.vx = ((tx << 4) - ball.x) / (t1 + T2);
    ball.vh = (t1 + 1) - ball.h / t1;
    ball.serve_vh = T2 + 1;
    ball.spin = SHOT_SERVE;
    ball.serve_spin = spin;
    last_hitter = s;
    serve_phase = 1;
    server_bounced = 0;
    recv_bounces = 0;
    pl[s].swing = 12;
    sfx_paddle();
    state = ST_RALLY;
    if (s == NEAR) {
        pressure = power / 2 + (spin == SHOT_SLICE ? 2 : 0);
        cpu_predict();
    }
}

// The serve bar: 8 background tiles in column 0, drawn for the current red box position.
static void draw_bar(void) {
    uint8_t buf[16], t, r, py, col, lo, hi, c;
    int16_t d;
    uint8_t attr[8], map[8];
    for (t = 0; t < 8; t++) {
        for (r = 0; r < 8; r++) {
            py = t * 8 + r;                              // 0 = top of the bar
            d = iabs((int16_t)(63 - py) - (zone >> 4)); // distance from the middle of the red box
            col = d <= lv->zp ? 1 : d <= lv->zs ? 2 : d <= lv->zo ? 3 : 0;
            lo = hi = 0;
            for (c = 0; c < 8; c++) {
                uint8_t v = (c >= 1 && c <= 6) ? col : 0;
                if (c == 0 || c == 7) v = 0;
                lo = (lo << 1) | (v & 1);
                hi = (hi << 1) | (v >> 1);
            }
            buf[r * 2] = lo;
            buf[r * 2 + 1] = hi;
        }
        set_bkg_data(T_METER + t, 1, buf);
        map[t] = T_METER + t;
        attr[t] = BG_PAL_BAR;
    }
    if (is_cgb) {
        VBK_REG = 1;
        set_bkg_tiles(0, BAR_ROW, 1, 8, attr);
        VBK_REG = 0;
    }
    set_bkg_tiles(0, BAR_ROW, 1, 8, map);
}

static void hide(uint8_t n) { move_sprite(n, 0, 0); }

static void hide_bar(void) {
    uint8_t attr[8], r;
    memset(attr, 0, 8);
    if (is_cgb) {
        VBK_REG = 1;
        set_bkg_tiles(0, BAR_ROW, 1, 8, attr);
        VBK_REG = 0;
    }
    for (r = 0; r < 8; r++) set_bkg_tiles(0, BAR_ROW + r, 1, 1, &table_map[(BAR_ROW + r) * 20]);
    hide(OAM_MARKER);
    aiming = 0;
}

static void setup_serve(void) {
    uint8_t total = score[0] + score[1];
    server = deuce() ? ((total & 1) ? FAR : NEAR) : (((total >> 1) & 1) ? FAR : NEAR);
    state = ST_SERVE;
    tossing = 0;
    timer = 0;
    ball_live = 1;
    aiming = 0;
    wait_release = 0;
    stop_charging();
    pl[NEAR].z = NEAR_Z_HOME << 4;
    pl[FAR].z = FAR_Z << 4;
    // The CPU serves from a corner, like the pros.
    pl[FAR].x = server == FAR ? ((rand() & 1) ? (40 << 4) : -(40 << 4)) : 0;
    if (server == NEAR && (pl[NEAR].x > (50 << 4) || pl[NEAR].x < -(50 << 4))) pl[NEAR].x = 0;
    hold_ball();
    draw_score();
    restore_row(17);
    if (server == NEAR) show_msg(16, "HOLD A OR B", 0);
    else restore_row(16);
}

static void update_serve(void) {
    int16_t tx, b2z, miss;
    int32_t f, h;
    uint8_t held, power;

    if (server == NEAR) {
        if (wait_release) {
            if (!(keys & (J_A | J_B))) wait_release = 0;
            hold_ball();
            return;
        }
        if (!aiming) {
            hold_ball();
            if (pressed & (J_A | J_B)) {
                aiming = 1;
                serve_button = (pressed & J_A) ? J_A : J_B;
                mark = 0;
                zone = (int16_t)(ZONE_MIN + rand() % (ZONE_MAX - ZONE_MIN + 1)) << 4;
                draw_bar();
                show_msg(16, "LET GO IN THE RED", 0);
            }
            return;
        }
        // The marker climbs; the toss goes up and falls back to paddle height in the red box.
        mark += lv->serve_speed;
        f = ((int32_t)mark << 8) / zone;                 // 256 = in the middle of the red box
        h = ((int32_t)(TOSS_H << 4) * 4 * f * (256 - f)) >> 16;
        ball.h = (PADDLE_H << 4) + (int16_t)h;
        if (ball.h < -(10 << 4)) ball.h = -(10 << 4);
        move_sprite(OAM_MARKER, 8, (uint8_t)(BAR_TOP_Y + 63 - (mark >> 4) + 15));
        if (mark >= (BAR_TRACK << 4)) {
            hide_bar();
            wait_release = 1;
            sfx_miss();
            show_msg(16, "TOO SLOW TRY AGAIN", 0);
            return;
        }
        held = keys & serve_button;
        if (!held) {
            miss = iabs(mark - zone) >> 4;
            serve_quality = miss <= lv->zp ? Q_PERFECT : miss <= lv->zs ? Q_STRONG : miss <= lv->zo ? Q_OK : Q_WEAK;
            power = serve_quality == Q_PERFECT ? 16 : serve_quality == Q_STRONG ? 10 : serve_quality == Q_OK ? 5 : 0;
            hide_bar();
            tx = (keys & J_LEFT) ? -40 : (keys & J_RIGHT) ? 40 : (int16_t)(rand() % 33) - 16;
            b2z = (keys & J_UP) ? 160 : (keys & J_DOWN) ? 120 : 142;
            serve_ball(NEAR, tx, b2z, power, serve_button == J_A ? SHOT_TOP : SHOT_SLICE);
            if (serve_quality == Q_PERFECT) {
                note("PERFECT SERVE");
                sfx_perfect();
                freeze = 5;
                trail = 24;
            } else if (serve_quality == Q_STRONG) note("STRONG SERVE");
            else if (serve_quality == Q_WEAK) note("WEAK SERVE");
            else restore_row(16);
        }
    } else {
        timer++;
        if (!tossing) {
            hold_ball();
            if (timer > 60) { tossing = 1; ball.vh = 44; }
        } else {
            ball.vh -= GRAV;
            ball.h += ball.vh;
            if (ball.vh < 0 && ball.h <= (PADDLE_H << 4)) {
                // From a corner, usually across the table
                tx = 10 + rand() % 30;
                if (pl[FAR].x > 0) tx = -tx;
                if ((rand() & 3) == 0) tx = -tx;
                b2z = TABLE_LEN - (120 + rand() % 40);
                serve_quality = Q_OK;
                serve_ball(FAR, tx, b2z, rand() % 6, (rand() % 4 == 0) ? SHOT_SLICE : SHOT_TOP);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// The smash meter over your head (2 sprites, redrawn as it fills)
// ---------------------------------------------------------------------------
static void draw_charge_meter(uint8_t pct) {
    uint8_t buf[64], fill, r, c, v, t, lo, hi, x;
    memset(buf, 0, 64);
    fill = (uint8_t)((pct * 14) / 100);
    for (t = 0; t < 2; t++) {                   // two 8x16 sprites side by side
        for (r = 0; r < 4; r++) {
            lo = hi = 0;
            for (c = 0; c < 8; c++) {
                x = t * 8 + c;
                v = 0;
                if (x <= 15) {
                    if (r == 0 || r == 3 || x == 0 || x == 15) v = 1;            // dark frame
                    else if (x - 1 < fill) v = (x - 1 >= 10) ? 3 : 2;           // green, then gold
                }
                lo = (lo << 1) | (v & 1);
                hi = (hi << 1) | (v >> 1);
            }
            buf[t * 32 + r * 2] = lo;
            buf[t * 32 + r * 2 + 1] = hi;
        }
    }
    set_sprite_data(SPR_CHARGE, 4, buf);
}

// ---------------------------------------------------------------------------
// Per-frame updates
// ---------------------------------------------------------------------------
static void move_near(void) {
    player_t *p = &pl[NEAR];
    int16_t spd = charging ? CHARGE_SPEED : PLAYER_SPEED;
    if (state == ST_SERVE && server == NEAR && aiming) return;   // stand still while serving
    if (keys & J_LEFT)  p->x -= spd;
    if (keys & J_RIGHT) p->x += spd;
    if (state != ST_SERVE) {
        if (keys & J_UP)   p->z += spd >> 1;
        if (keys & J_DOWN) p->z -= spd >> 1;
    }
    if (p->x < -(96 << 4)) p->x = -(96 << 4);
    if (p->x >  (96 << 4)) p->x =  (96 << 4);
    if (p->z < -(36 << 4)) p->z = -(36 << 4);
    if (p->z > -(4 << 4))  p->z = -(4 << 4);
}

static void smash(void) {
    int16_t dz = (ball.z - pl[NEAR].z) >> 4;
    uint8_t pct = (uint8_t)(((uint16_t)charge * 100) / lv->charge_frames);
    uint8_t perfect, high, power, T;
    int8_t side = charge_button == J_A ? -1 : 1;     // forehand aims left, backhand right
    int8_t aim = (keys & J_LEFT) ? -1 : (keys & J_RIGHT) ? 1 : 0;
    int16_t tx = side * (aim == side ? 50 : aim == -side ? 16 : 36);

    if (pct > 100) pct = 100;
    perfect = pct >= PERFECT_CHARGE && dz >= lv->sweet_lo && dz <= lv->sweet_hi;
    high = ball.h >= (24 << 4);
    power = 4 + pct / 10 + (perfect ? 6 : 0) + (high ? 2 : 0);
    T = T_TOP - power;
    pressure = power;
    last_perfect = perfect;
    stop_charging();
    hit_ball(NEAR, SHOT_TOP, tx, 64, T);
    trail = 30;

    if (perfect) {
        freeze = 6;
        flash = 4;
        shake = 12;
        sfx_perfect();
        note(high ? "PERFECT SMASH" : "PERFECT SHOT");
    } else {
        sfx_smash();
        note(high ? "SMASH" : "POWER SHOT");
        hint(dz > lv->sweet_hi ? "EARLY" : dz < lv->sweet_lo ? "LATE" : "MORE POWER");
    }
}

static void update_near_rally(void) {
    int16_t tx, dz;
    static uint8_t a_hold, b_hold;

    a_hold = (keys & J_A) ? a_hold + 1 : 0;
    b_hold = (keys & J_B) ? b_hold + 1 : 0;

    if (last_hitter != FAR) { stop_charging(); return; }   // ball isn't coming to you

    if (!charging && (a_hold >= 8 || b_hold >= 8)) {
        charging = 1;
        charge_button = a_hold >= 8 ? J_A : J_B;
        charge = 0;
    }

    if (charging) {
        if (keys & charge_button) {
            if (charge < lv->charge_frames) {
                charge++;
                if (charge == lv->charge_frames) { charge_full = 1; sfx_ready(); }
            }
            // Held too long: the ball is about to pass you, so you just block it back.
            dz = (ball.z - pl[NEAR].z) >> 4;
            if (can_hit(NEAR) && in_zone(NEAR) && dz < -8) {
                stop_charging();
                pressure = 0;
                last_perfect = 0;
                hit_ball(NEAR, SHOT_LOB, (int16_t)(rand() % 33) - 16, 50, T_LOB);
                sfx_paddle();
                hint("TOO LATE");
            }
        } else {
            // Let go: smash if the ball is in reach, otherwise a swing at nothing.
            if (can_hit(NEAR) && in_zone(NEAR)) smash();
            else {
                stop_charging();
                pl[NEAR].swing = 12;
                hint("TOO EARLY");
            }
        }
        return;
    }

    // Auto-swing: if the ball is in reach you always hit it back.
    if (can_hit(NEAR) && in_zone(NEAR)) {
        tx = (keys & J_LEFT) ? -44 : (keys & J_RIGHT) ? 44 : (int16_t)(rand() % 33) - 16;
        pressure = 0;
        last_perfect = 0;
        hit_ball(NEAR, SHOT_TOP, tx, 56, T_TOP);
        sfx_paddle();
    }
}

static void update_far(void) {
    player_t *p = &pl[FAR];
    int16_t target = (state == ST_RALLY && last_hitter == NEAR) ? cpu_tx : 0;
    int16_t d, tx, depth, off;
    uint8_t shot, T;

    if (state == ST_SERVE && server == FAR) return;   // stays in its corner

    if (cpu_delay) cpu_delay--;
    else {
        d = target - p->x;
        if (d > lv->cpu_speed) d = lv->cpu_speed;
        if (d < -lv->cpu_speed) d = -lv->cpu_speed;
        p->x += d;
    }

    if (can_hit(FAR) && in_zone(FAR)) {
        // Its shot goes to the side it showed: right of where it stands for a forehand, left for a backhand.
        off = 14 + rand() % 24;
        tx = (p->x >> 4) + cpu_side * off;
        if (tx > 56) tx = 56;
        if (tx < -56) tx = -56;
        if (cpu_weak) {
            shot = SHOT_LOB;                       // a slow, high floater
            depth = 40;
            T = T_TOP + cpu_weak;
        } else {
            shot = (rand() % 10) < 7 ? SHOT_TOP : SHOT_SLICE;
            depth = 30 + rand() % 46;
            T = (shot == SHOT_TOP ? T_TOP : T_SLICE) - rand() % 6;
        }
        if (cpu_mistake == MISS_WIDE) tx = cpu_side > 0 ? 80 : -80;   // off the side it chose
        if (cpu_mistake == MISS_LONG) depth = 104;                    // past your end
        if (cpu_weak) note(last_perfect ? "BLOCKED SMASH IT" : "FLOATER SMASH IT");
        hit_ball(FAR, shot, tx, depth, T);
        sfx_paddle();
        cpu_weak = 0;
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
static void draw_player(uint8_t s) {
    player_t *p = &pl[s];
    uint8_t base = s == NEAR ? OAM_NEAR : OAM_FAR;
    uint8_t head = s == NEAR ? SPR_NEAR_BACK : (p->swing ? SPR_FAR_FLAP : SPR_FAR_FRONT);
    uint8_t fore, low = 0;
    int16_t px, py, frames_left;

    project(p->x >> 4, p->z >> 4, 0);
    if (s == NEAR && aiming) low = 2;               // bent over, getting ready to serve

    set_sprite_tile(base + 1, head);
    set_sprite_tile(base + 2, head + 2);
    set_sprite_prop(base + 1, s);
    set_sprite_prop(base + 2, s);
    move_sprite(base + 1, (uint8_t)sx, (uint8_t)(sy - 13 + low));
    move_sprite(base + 2, (uint8_t)(sx + 8), (uint8_t)(sy - 13 + low));

    set_sprite_tile(base + 3, SPR_BODY);
    set_sprite_tile(base + 4, SPR_BODY + 2);
    set_sprite_prop(base + 3, PAL_SHIRT + s);
    set_sprite_prop(base + 4, PAL_SHIRT + s);
    move_sprite(base + 3, (uint8_t)sx, (uint8_t)(sy + 2));
    move_sprite(base + 4, (uint8_t)(sx + 8), (uint8_t)(sy + 2));

    // Paddle on whichever side the ball is ...
    fore = ball.x >= p->x;
    px = fore ? sx + 5 : sx - 13;
    py = sy - 20;
    if (s == NEAR && charging) {
        // ... pulled far back on the side you're charging (A forehand = right of you, B backhand = left)
        fore = charge_button == J_A;
        px = fore ? sx + 14 : sx - 22;
        py = sy - 16 + ((charge_full && (sys_time & 4)) ? 1 : 0);
    } else if (s == FAR && state == ST_RALLY && last_hitter == NEAR && ball.vz > 0 && !serve_phase) {
        // ... or, for the CPU, out on the side it is about to hit to (its tell).
        frames_left = ((p->z - ball.z) / (ball.vz ? ball.vz : 1));
        if (frames_left >= 0 && frames_left <= lv->tell) {
            fore = cpu_side > 0;
            px = fore ? sx + 14 : sx - 22;
            py = sy - 24;
        }
    } else if (p->swing > 5) { py -= 4; px += fore ? -3 : 3; }
    else if (s == NEAR && aiming) { px = sx - 12; py = sy - 12; fore = 0; }
    set_sprite_tile(base, SPR_PADDLE);
    set_sprite_prop(base, PAL_PADDLE | (fore ? 0 : S_FLIPX));
    move_sprite(base, (uint8_t)(px + 8), (uint8_t)(py + 16));
    if (p->swing) p->swing--;

    // Smash meter over your head
    if (s == NEAR) {
        if (charging) {
            draw_charge_meter((uint8_t)(((uint16_t)charge * 100) / lv->charge_frames));
            move_sprite(OAM_CHARGE, (uint8_t)sx, (uint8_t)(sy - 21));          // just above your head
            move_sprite(OAM_CHARGE + 1, (uint8_t)(sx + 8), (uint8_t)(sy - 21));
        } else { hide(OAM_CHARGE); hide(OAM_CHARGE + 1); }
    }
}

static void draw_ball(void) {
    uint8_t i;
    if (!ball_live) { hide(OAM_BALL); hide(OAM_SHADOW); hide(OAM_TRAIL); hide(OAM_TRAIL + 1); return; }
    project(ball.x >> 4, ball.z >> 4, ball.h >> 4);
    // Ghost balls behind a smash or a perfect serve
    for (i = 7; i > 0; i--) { trail_x[i] = trail_x[i - 1]; trail_y[i] = trail_y[i - 1]; }
    trail_x[0] = sx + 4;
    trail_y[0] = sy + 14;
    if (trail) {
        trail--;
        move_sprite(OAM_TRAIL, (uint8_t)trail_x[3], (uint8_t)trail_y[3]);
        move_sprite(OAM_TRAIL + 1, (uint8_t)trail_x[7], (uint8_t)trail_y[7]);
    } else { hide(OAM_TRAIL); hide(OAM_TRAIL + 1); }
    move_sprite(OAM_BALL, (uint8_t)(sx + 4), (uint8_t)(sy + 14));
    if (over_table(&ball)) {
        project(ball.x >> 4, ball.z >> 4, 0);
        move_sprite(OAM_SHADOW, (uint8_t)(sx + 4), (uint8_t)(sy + 15));
    } else hide(OAM_SHADOW);
}

// Flash, shake and hint timers
static void update_effects(void) {
    if (flash) {
        flash--;
        if (is_cgb) set_bkg_palette(0, 1, flash ? white_pal : bg_pal);
        else BGP_REG = flash ? 0x00 : 0xE4;
    }
    if (shake) {
        shake--;
        SCX_REG = shake ? ((shake & 2) ? 2 : -2) : 0;
        SCY_REG = shake ? ((shake & 4) ? 1 : -1) : 0;
    }
    if (hint_timer && --hint_timer == 0) restore_row(17);
    if (note_timer && --note_timer == 0 && state == ST_RALLY) restore_row(16);
}

// ---------------------------------------------------------------------------
// Title screen: pick you, your opponent and the difficulty
// ---------------------------------------------------------------------------
static void clear_bg(void) {
    uint8_t row[20], y;
    memset(row, 0, 20);
    for (y = 0; y < 18; y++) set_bkg_tiles(0, y, 20, 1, row);
}

static void place_name(char *row, uint8_t start, uint8_t who, uint8_t arrows) {
    const char *nm = characters[who].name;
    uint8_t n = 0, b;
    while (nm[n]) n++;
    b = start + (10 - n) / 2;
    memcpy(row + b, nm, n);
    if (arrows) { row[b - 1] = '<'; row[b + n] = '>'; }
}

static void draw_title_rows(void) {
    char row[21], buf[8];
    uint8_t blank[20], my, cpu, n;
    const char *nm;
    memset(blank, 0, 20);

    memset(row, ' ', 20);
    row[20] = 0;
    place_name(row, 0, pl[NEAR].who, title_row == 0);
    place_name(row, 10, pl[FAR].who, title_row == 1);
    set_bkg_tiles(0, 11, 20, 1, blank);
    put_text(0, 11, row);

    // Difficulty and its record
    memset(row, ' ', 20);
    nm = levels[level_choice].name;
    n = 0;
    while (nm[n]) n++;
    memcpy(row + (20 - n) / 2, nm, n);
    if (title_row == 2) { row[(20 - n) / 2 - 1] = '<'; row[(20 - n) / 2 + n] = '>'; }
    set_bkg_tiles(0, 13, 20, 1, blank);
    put_text(0, 13, row);

    set_bkg_tiles(0, 14, 20, 1, blank);
    if (save.best[level_choice]) {
        result_points(save.best[level_choice], &my, &cpu);
        score_text(buf, my, cpu);
        memset(row, ' ', 20);
        memcpy(row + 1, "BEST ", 5);
        memcpy(row + 6, buf, strlen(buf));
        nm = characters[save.who[level_choice]].name;
        memcpy(row + 7 + strlen(buf), nm, strlen(nm));
        put_text(0, 14, row);
    } else put_text(5, 14, "NO RECORD");
}

static void draw_title_faces(void) {
    uint8_t i;
    for (i = 0; i < 20; i++) hide(i);
    set_sprite_tile(OAM_NEAR + 1, SPR_NEAR_FRONT);
    set_sprite_tile(OAM_NEAR + 2, SPR_NEAR_FRONT + 2);
    set_sprite_prop(OAM_NEAR + 1, 0);
    set_sprite_prop(OAM_NEAR + 2, 0);
    move_sprite(OAM_NEAR + 1, 32 + 8, 64 + 16);
    move_sprite(OAM_NEAR + 2, 40 + 8, 64 + 16);
    set_sprite_tile(OAM_FAR + 1, SPR_FAR_FRONT);
    set_sprite_tile(OAM_FAR + 2, SPR_FAR_FRONT + 2);
    set_sprite_prop(OAM_FAR + 1, 1);
    set_sprite_prop(OAM_FAR + 2, 1);
    move_sprite(OAM_FAR + 1, 112 + 8, 64 + 16);
    move_sprite(OAM_FAR + 2, 120 + 8, 64 + 16);
}

static void go_title(void) {
    state = ST_TITLE;
    ball_live = 0;
    SCX_REG = SCY_REG = 0;
    clear_bg();
    put_text(3, 2, "PING PONG PALS");
    put_text(2, 6, "YOU");
    put_text(16, 6, "VS");
    draw_title_rows();
    put_text(4, 16, "PRESS START");
    load_characters();
    draw_title_faces();
}

static void update_title(void) {
    player_t *p;
    if (pressed & J_UP)   { title_row = (title_row + 2) % 3; draw_title_rows(); }
    if (pressed & J_DOWN) { title_row = (title_row + 1) % 3; draw_title_rows(); }
    if (pressed & (J_LEFT | J_RIGHT)) {
        if (title_row == 2) {
            level_choice = (pressed & J_RIGHT) ? (level_choice + 1) % 3 : (level_choice + 2) % 3;
        } else {
            p = &pl[title_row];
            if (pressed & J_RIGHT) p->who = (p->who + 1) % NUM_CHARACTERS;
            else                   p->who = (p->who + NUM_CHARACTERS - 1) % NUM_CHARACTERS;
            load_characters();
        }
        draw_title_rows();
        sfx_bounce();
    }
    if (pressed & (J_START | J_A)) {
        initrand(sys_time);
        if (pl[FAR].who == pl[NEAR].who) pl[FAR].who = (pl[FAR].who + 1) % NUM_CHARACTERS;   // never yourself
        lv = &levels[level_choice];
        score[0] = score[1] = 0;
        pl[NEAR].x = 0;
        set_bkg_tiles(0, 0, 20, 18, table_map);
        setup_serve();
    }
}

// The end of a match: who won, and a new record?
static void finish_match(void) {
    char buf[21], sc[8];
    uint8_t won = score[NEAR] > score[FAR], n = 0;
    uint16_t v = result_value(score[NEAR], score[FAR]);
    const char *nm;
    state = ST_OVER;
    if (v > save.best[level_choice]) {
        save.best[level_choice] = v;
        save.who[level_choice] = pl[NEAR].who;
        save_write();
        show_msg(15, "NEW RECORD", 0);
    }
    nm = won ? "YOU WIN " : characters[pl[FAR].who].name;
    while (*nm && n < 12) buf[n++] = *nm++;
    if (!won) { memcpy(buf + n, " WINS ", 6); n += 6; }
    score_text(sc, won ? score[NEAR] : score[FAR], won ? score[FAR] : score[NEAR]);
    memcpy(buf + n, sc, strlen(sc) + 1);
    show_msg(16, buf, 0);
    show_msg(17, "PRESS START", 0);
}

// ---------------------------------------------------------------------------
void main(void) {
    uint8_t ev;

    DISPLAY_OFF;
    is_cgb = (_cpu == CGB_TYPE);
    if (is_cgb) cpu_fast();          // double speed: quicker table generation

    NR52_REG = 0x80;
    NR50_REG = 0x77;
    NR51_REG = 0xFF;
    music_init();
    music_play(&paddle_party);

    BGP_REG = 0xE4;
    OBP0_REG = 0xE4;
    if (is_cgb) {
        set_bkg_palette(0, 1, bg_pal);
        set_bkg_palette(BG_PAL_BAR, 1, bar_pal);
        set_sprite_palette(PAL_BALL, 1, ball_pal);
        set_sprite_palette(PAL_PADDLE, 1, paddle_pal);
        set_sprite_palette(PAL_METER, 1, meter_pal);
    }

    save_load();
    lv = &levels[1];
    level_choice = 1;

    build_table();
    load_font(T_FONT);
    load_sprite16(body_art, SPR_BODY);
    load_sprite8x16(paddle_art, SPR_PADDLE);
    load_sprite8x16(ball_art, SPR_BALL);
    load_sprite8x16(shadow_art, SPR_SHADOW);
    load_sprite8x16(marker_art, SPR_MARKER);
    set_sprite_tile(OAM_BALL, SPR_BALL);
    set_sprite_prop(OAM_BALL, PAL_BALL);
    set_sprite_tile(OAM_SHADOW, SPR_SHADOW);
    set_sprite_prop(OAM_SHADOW, PAL_BALL);
    set_sprite_tile(OAM_MARKER, SPR_MARKER);
    set_sprite_prop(OAM_MARKER, PAL_BALL);
    set_sprite_tile(OAM_CHARGE, SPR_CHARGE);
    set_sprite_tile(OAM_CHARGE + 1, SPR_CHARGE + 2);
    set_sprite_prop(OAM_CHARGE, PAL_METER);
    set_sprite_prop(OAM_CHARGE + 1, PAL_METER);
    set_sprite_tile(OAM_TRAIL, SPR_BALL);
    set_sprite_tile(OAM_TRAIL + 1, SPR_BALL);
    set_sprite_prop(OAM_TRAIL, PAL_BALL);
    set_sprite_prop(OAM_TRAIL + 1, PAL_BALL);
    SPRITES_8x16;

    pl[NEAR].who = 0;
    pl[FAR].who = 1 % NUM_CHARACTERS;
    go_title();

    SHOW_BKG;
    SHOW_SPRITES;
    DISPLAY_ON;

    while (1) {
        vsync();
        keys = joypad();
        pressed = keys & ~prev_keys;
        released = prev_keys & ~keys;
        prev_keys = keys;

        music_tick();
        if (pressed & J_SELECT) music_toggle();

        if (state == ST_TITLE) { update_title(); continue; }

        update_effects();
        if (freeze) { freeze--; continue; }          // hit-stop: everything holds still for a moment

        if (state == ST_SERVE) {
            move_near();
            update_serve();
            update_far();
        } else if (state == ST_RALLY) {
            move_near();
            update_near_rally();
            update_far();
            if (state == ST_RALLY) {
                ev = step_ball(&ball);
                if (ev != EV_NONE) rules_event(ev);
            }
        } else if (state == ST_POINT) {
            move_near();
            if (ball_live && step_ball(&ball) == EV_FLOOR) ball_live = 0;
            if (++timer == 100) {
                if (match_over()) finish_match();
                else setup_serve();
            }
        } else if (state == ST_OVER) {
            if (pressed & (J_START | J_A)) { go_title(); continue; }
        }

        draw_ball();
        draw_player(NEAR);
        draw_player(FAR);
    }
}
