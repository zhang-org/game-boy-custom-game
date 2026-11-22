// Ping Pong Pals - table tennis seen from behind your end of the table.
//
// Controls (Mario Tennis style):
//   D-pad   move
//   A       topspin  (fast, low)
//   B       slice    (slower, low bounce)
//   A+B     lob      (high and slow)
//   Hold LEFT/RIGHT as you hit to aim. UP = deep, DOWN = short.
//   Press A/B EARLY (before the ball reaches you) to charge a power shot.
//   Serve: A to toss, then A or B to hit. Hit at the top of the toss = NICE SERVE.
// Real rules: the serve bounces on your side first, every return must bounce once
// on the other side. First to 7 wins.

#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>
#include <string.h>
#include <rand.h>
#include "../common/gfx.h"
#include "../common/characters.h"
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
#define WIN_SCORE    7
#define CPU_SPEED    20      // 1/16 units per frame
#define PLAYER_SPEED 40

#define NEAR 0               // you
#define FAR  1               // the CPU

// ---------- Screen mapping (pseudo 3D) ----------
#define FAR_Y   44           // screen y of the far table edge
#define NEAR_Y  116          // screen y of the near table edge
#define NET_Y   80
#define NET_TOP 76

// ---------- Tiles ----------
#define TBL_MAX        150   // BG tiles 0..149 = generated table art
#define SPR_NEAR_FRONT 160
#define SPR_NEAR_BACK  164
#define SPR_FAR_FRONT  168
#define SPR_FAR_FLAP   172
#define SPR_BODY       176
#define SPR_PADDLE     180
#define SPR_BALL       182
#define SPR_SHADOW     184
#define T_FONT         200

// ---------- OAM slots (lower = drawn on top) ----------
#define OAM_BALL   0
#define OAM_NEAR   1         // paddle, head L/R, body L/R
#define OAM_FAR    6
#define OAM_SHADOW 11

// ---------- Sprite palettes ----------
#define PAL_BALL   4
#define PAL_PADDLE 5
#define PAL_SHIRT  6         // 6 = you, 7 = CPU

enum { SHOT_TOP, SHOT_SLICE, SHOT_LOB, SHOT_SERVE };
enum { ST_TITLE, ST_SERVE, ST_RALLY, ST_POINT, ST_OVER };
enum { EV_NONE, EV_BOUNCE, EV_NET, EV_FLOOR };

typedef struct { int16_t x, z, h, vx, vz, vh, serve_vh; uint8_t spin; } ball_t;
typedef struct { int16_t x, z; uint8_t who, swing, queued, shot, charge; } player_t;

static const palette_color_t bg_pal[] = {
    RGB(17,10,6), RGB(4,11,23), RGB(2,4,10), RGB(31,31,31),     // floor, table, dark, white
};
static const palette_color_t ball_pal[]   = { 0, RGB(31,31,31), RGB(22,22,25), RGB(3,4,9) };
static const palette_color_t paddle_pal[] = { 0, RGB(29,5,5),   RGB(24,16,8),  RGB(6,3,2) };
static const palette_color_t shirt_col[2] = { RGB(6,14,31), RGB(30,8,8) };

// How each shot behaves when it bounces (x/16): height kept, forward speed kept
static const uint8_t bounce_e[4]    = { 11, 7, 12, 11 };
static const uint8_t bounce_kick[4] = { 17, 11, 14, 16 };

// Shot flight time in frames (bigger = slower ball)
#define T_TOP    52
#define T_SLICE  62
#define T_LOB    80
#define T_SERVE  40          // serve second hop; a NICE SERVE is much faster

// CPU difficulty: each of your shots gets a score (0..100+) from
//   power   = how hard you hit it (charge / nice serve)       -> pressure * CPU_POWER_W
//   stretch = how far the CPU must run vs. the time it has    -> % of its reach, above CPU_STRETCH_FREE
// Score <  CPU_DEFEND : normal return
// Score >= CPU_DEFEND : slow defensive return to the middle
// Score >= CPU_RISKY  : may also make a mistake; chance = (score - CPU_RISKY) * CPU_ERR_SLOPE %
#define CPU_POWER_W      5
#define CPU_STRETCH_FREE 35
#define CPU_DEFEND       20
#define CPU_RISKY        45
#define CPU_ERR_SLOPE    3
enum { MISS_NONE, MISS_WIDE, MISS_LONG };

static ball_t   ball;
static player_t pl[2];
static uint8_t  is_cgb, state, keys, prev_keys, pressed, timer, tossing, ball_live;
static uint8_t  server, last_hitter, serve_phase, server_bounced, recv_bounces, pt_winner;
static uint8_t  title_row;
static uint8_t  score[2];
static int16_t  cpu_tx;
static uint8_t  cpu_delay, cpu_mistake, cpu_weak, pressure;
static int16_t  sx, sy;                 // result of project()

static uint8_t  table_map[360];
static uint8_t  tstore[TBL_MAX * 16];
static uint8_t  thash[TBL_MAX];
static uint8_t  edge_l[NEAR_Y + 1], edge_r[NEAR_Y + 1];
static char     back_rows[16][17];
static const char *back_ptr[16];

// ---------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------
static void sfx_bounce(void) { NR21_REG = 0xBC; NR22_REG = 0x81; NR23_REG = 0x80; NR24_REG = 0xC7; }
// (channel 2 = sound effects; channels 1, 3, 4 = music)
static void sfx_paddle(void) { NR21_REG = 0xB8; NR22_REG = 0xC1; NR23_REG = 0x00; NR24_REG = 0xC6; }
static void sfx_point(void)  { NR21_REG = 0x80; NR22_REG = 0xF3; NR23_REG = 0x00; NR24_REG = 0x87; }
static void sfx_net(void)    { NR41_REG = 0x30; NR42_REG = 0x91; NR43_REG = 0x70; NR44_REG = 0xC0; }

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
                b->spin = SHOT_TOP;
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

// ---------------------------------------------------------------------------
// CPU brain
// ---------------------------------------------------------------------------
static void cpu_predict(void) {
    ball_t b = ball;
    uint8_t i, ev;
    for (i = 0; i < 200; i++) {
        ev = step_ball(&b);
        if (b.vz > 0 && b.z >= ((FAR_Z - 6) << 4)) break;
        if (ev == EV_FLOOR || ev == EV_NET) break;
    }
    int16_t need, reach, frames, stretch, score;

    // Reaction time is fixed, a bit longer after a power shot
    cpu_delay = 12 + (pressure >> 1);
    cpu_tx = b.x + ((int16_t)(rand() % 9) - 4) * 16;        // tiny aim wobble

    // How stretched is the CPU? distance to run vs. how far it can run in time
    need = (b.x - pl[FAR].x) >> 4;
    if (need < 0) need = -need;
    frames = (int16_t)i - cpu_delay;
    if (frames < 0) frames = 0;
    reach = ((frames * CPU_SPEED) >> 4) + 28;               // + paddle reach
    stretch = (need * 100) / reach;                         // 100+ = can't get there

    score = pressure * CPU_POWER_W;
    if (stretch > CPU_STRETCH_FREE) score += stretch - CPU_STRETCH_FREE;

    cpu_mistake = MISS_NONE;
    cpu_weak = 0;
    if (score >= CPU_DEFEND) {
        cpu_weak = 10 + ((score - CPU_DEFEND) >> 1);       // weaker, slower return
        if (cpu_weak > 30) cpu_weak = 30;
    }
    if (score >= CPU_RISKY && rand() % 100 < (score - CPU_RISKY) * CPU_ERR_SLOPE)
        cpu_mistake = (rand() & 1) ? MISS_WIDE : MISS_LONG;
}

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------
static void award(uint8_t s) {
    score[s]++;
    pt_winner = s;
    state = ST_POINT;
    timer = 0;
    pl[NEAR].queued = pl[FAR].queued = 0;
    sfx_point();
    draw_score();
    show_msg(16, "POINT ", characters[pl[s].who].name);
}

static void rules_event(uint8_t ev) {
    uint8_t side, other = !last_hitter;
    if (ev == EV_BOUNCE) {
        sfx_bounce();
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

static void hit_ball(uint8_t s, uint8_t shot, int16_t tx, int16_t depth, uint8_t power) {
    uint8_t T = shot == SHOT_TOP ? T_TOP : (shot == SHOT_SLICE ? T_SLICE : T_LOB);
    T -= power;
    if (s == NEAR) pressure = power;
    else { T += cpu_weak; cpu_weak = 0; }   // CPU's weak return under pressure
    ball.spin = shot;
    aim_ball(tx, s == NEAR ? NET_Z + depth : NET_Z - depth, T);
    last_hitter = s;
    recv_bounces = 0;
    serve_phase = 0;
    pl[s].swing = 12;
    pl[s].queued = 0;
    pl[s].charge = 0;
    sfx_paddle();
    if (s == NEAR) cpu_predict();
}

// ---------------------------------------------------------------------------
// Serving
// ---------------------------------------------------------------------------
static void hold_ball(void) {
    player_t *p = &pl[server];
    ball.x = p->x + (server == NEAR ? (10 << 4) : -(10 << 4));
    ball.z = p->z + (server == NEAR ? (6 << 4) : -(6 << 4));
    ball.h = 12 << 4;
    ball.vx = ball.vz = ball.vh = ball.serve_vh = 0;
}

// Serve: first bounce on the server's side (b1z), second on the receiver's side (tx, b2z)
static void serve_ball(uint8_t s, int16_t tx, int16_t b2z, uint8_t power) {
    int16_t b1z = s == NEAR ? 40 : TABLE_LEN - 40;
    uint8_t T2 = T_SERVE - power;
    int16_t vz = ((b2z - b1z) << 4) / T2;
    int16_t t1 = ((b1z << 4) - ball.z) / vz;
    if (t1 < 4) t1 = 4;
    ball.vz = vz;
    ball.vx = ((tx << 4) - ball.x) / (t1 + T2);
    ball.vh = (t1 + 1) - ball.h / t1;
    ball.serve_vh = T2 + 1;
    ball.spin = SHOT_SERVE;
    last_hitter = s;
    serve_phase = 1;
    server_bounced = 0;
    recv_bounces = 0;
    pl[s].swing = 12;
    sfx_paddle();
    state = ST_RALLY;
    if (s == NEAR) {
        pressure = power >= 16 ? 14 : power / 2;   // a NICE SERVE rattles the CPU
        cpu_predict();
    }
}

static void setup_serve(void) {
    server = (((score[0] + score[1]) >> 1) & 1) ? FAR : NEAR;   // swap every 2 points
    state = ST_SERVE;
    tossing = 0;
    timer = 0;
    ball_live = 1;
    pl[NEAR].z = NEAR_Z_HOME << 4;
    pl[FAR].z = FAR_Z << 4;
    pl[FAR].x = 0;
    pl[NEAR].queued = pl[FAR].queued = 0;
    if (server == NEAR && (pl[NEAR].x > (50 << 4) || pl[NEAR].x < -(50 << 4))) pl[NEAR].x = 0;
    hold_ball();
    draw_score();
    restore_row(17);
    if (server == NEAR) show_msg(16, "A TOSS  A HIT", 0);
    else restore_row(16);
}

// ---------------------------------------------------------------------------
// Per-frame updates
// ---------------------------------------------------------------------------
static void move_near(void) {
    player_t *p = &pl[NEAR];
    int16_t spd = p->queued ? 14 : PLAYER_SPEED;
    if (state == ST_SERVE && server == NEAR && tossing) return;   // stand still while serving
    if (keys & J_LEFT)  p->x -= spd;
    if (keys & J_RIGHT) p->x += spd;
    if (state != ST_SERVE) {
        if (keys & J_UP)   p->z += spd >> 1;
        if (keys & J_DOWN) p->z -= spd >> 1;
    }
    if (p->x < -(110 << 4)) p->x = -(110 << 4);
    if (p->x >  (110 << 4)) p->x =  (110 << 4);
    if (p->z < -(36 << 4))  p->z = -(36 << 4);
    if (p->z > -(4 << 4))   p->z = -(4 << 4);
}

static void update_near_rally(void) {
    player_t *p = &pl[NEAR];
    uint8_t s;
    int16_t tx, depth;

    if (last_hitter != FAR) return;              // ball isn't coming to you
    if (pressed & (J_A | J_B)) {
        s = (pressed & J_A) ? SHOT_TOP : SHOT_SLICE;
        if ((pressed & (J_A | J_B)) == (J_A | J_B)) s = SHOT_LOB;
        if (!p->queued) { p->queued = 1; p->charge = 0; p->shot = s; }
        else if (s != p->shot) p->shot = SHOT_LOB;   // pressed the other button too
    }
    if (p->queued && p->charge < 42) p->charge++;
    // Auto-swing: if the ball is in reach you always hit it.
    // Pressing early picks the shot (A/B/A+B) and charges a power shot.
    if (can_hit(NEAR) && in_zone(NEAR)) {
        tx = (keys & J_LEFT) ? -44 : (keys & J_RIGHT) ? 44 : (int16_t)(rand() % 33) - 16;
        depth = (keys & J_UP) ? 74 : (keys & J_DOWN) ? 32 : 56;
        if (p->queued) hit_ball(NEAR, p->shot, tx, depth, p->charge / 3);
        else           hit_ball(NEAR, SHOT_TOP, tx, depth, 0);
    }
}

static void update_far(void) {
    player_t *p = &pl[FAR];
    int16_t target = (state == ST_RALLY && last_hitter == NEAR) ? cpu_tx : 0;
    int16_t d, tx, depth;
    uint8_t r, shot;

    if (cpu_delay) cpu_delay--;
    else if (!(state == ST_SERVE && server == FAR)) {
        d = target - p->x;
        if (d > CPU_SPEED) d = CPU_SPEED;
        if (d < -CPU_SPEED) d = -CPU_SPEED;
        p->x += d;
    }

    if (can_hit(FAR) && in_zone(FAR)) {
        if (cpu_weak) {
            // Under pressure: a slow, safe ball back to the middle
            shot = SHOT_SLICE;
            tx = (int16_t)(rand() % 25) - 12;
            depth = 45;
        } else {
            r = rand() % 10;
            shot = r < 6 ? SHOT_TOP : (r < 9 ? SHOT_SLICE : SHOT_LOB);
            tx = 16 + rand() % 30;
            if (pl[NEAR].x > 0) tx = -tx;            // aim away from you...
            if ((rand() & 3) == 0) tx = -tx;         // ...usually
            depth = 30 + rand() % 46;
        }
        if (cpu_mistake == MISS_WIDE) tx = (tx < 0) ? -80 : 80;   // off the side
        if (cpu_mistake == MISS_LONG) depth = 104;                // past your end
        hit_ball(FAR, shot, tx, depth, cpu_weak ? 0 : rand() % 6);
    }
}

static int16_t iabs(int16_t v) { return v < 0 ? -v : v; }

static void update_serve(void) {
    int16_t tx, b2z, av;
    uint8_t power;

    if (server == NEAR) {
        if (!tossing) {
            hold_ball();
            if (pressed & J_A) { tossing = 1; ball.vh = 40; }
        } else {
            ball.vh -= GRAV;
            ball.h += ball.vh;
            if (pressed & (J_A | J_B)) {
                av = iabs(ball.vh);
                power = av <= 6 ? 16 : (av <= 16 ? 6 : 0);   // 16 = NICE SERVE
                tx = (keys & J_LEFT) ? -40 : (keys & J_RIGHT) ? 40 : (int16_t)(rand() % 33) - 16;
                b2z = (keys & J_UP) ? 160 : (keys & J_DOWN) ? 120 : 142;
                serve_ball(NEAR, tx, b2z, power);
                if (power == 16) show_msg(16, "NICE SERVE", 0); else restore_row(16);
            } else if (ball.vh < 0 && ball.h < (12 << 4)) {
                tossing = 0;                     // missed the toss: try again
            }
        }
    } else {
        timer++;
        if (!tossing) {
            hold_ball();
            if (timer > 50) { tossing = 1; ball.vh = 40; }
        } else {
            ball.vh -= GRAV;
            ball.h += ball.vh;
            if (ball.vh <= 2) {
                tx = 10 + rand() % 30;
                if (rand() & 1) tx = -tx;
                b2z = TABLE_LEN - (120 + rand() % 40);
                serve_ball(FAR, tx, b2z, rand() % 6);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
static void hide(uint8_t n) { move_sprite(n, 0, 0); }

static void draw_player(uint8_t s) {
    player_t *p = &pl[s];
    uint8_t base = s == NEAR ? OAM_NEAR : OAM_FAR;
    uint8_t head = s == NEAR ? SPR_NEAR_BACK : (p->swing ? SPR_FAR_FLAP : SPR_FAR_FRONT);
    uint8_t fore;
    int16_t px, py;

    project(p->x >> 4, p->z >> 4, 0);

    set_sprite_tile(base + 1, head);
    set_sprite_tile(base + 2, head + 2);
    set_sprite_prop(base + 1, s);
    set_sprite_prop(base + 2, s);
    move_sprite(base + 1, (uint8_t)sx, (uint8_t)(sy - 13));
    move_sprite(base + 2, (uint8_t)(sx + 8), (uint8_t)(sy - 13));

    set_sprite_tile(base + 3, SPR_BODY);
    set_sprite_tile(base + 4, SPR_BODY + 2);
    set_sprite_prop(base + 3, PAL_SHIRT + s);
    set_sprite_prop(base + 4, PAL_SHIRT + s);
    move_sprite(base + 3, (uint8_t)sx, (uint8_t)(sy + 2));
    move_sprite(base + 4, (uint8_t)(sx + 8), (uint8_t)(sy + 2));

    // Paddle on whichever side the ball is
    fore = ball.x >= p->x;
    px = fore ? sx + 5 : sx - 13;
    py = sy - 20;
    if (p->swing > 5) { py -= 4; px += fore ? -3 : 3; }
    else if (p->queued) py += 3;                 // pulled back while charging
    set_sprite_tile(base, SPR_PADDLE);
    set_sprite_prop(base, PAL_PADDLE | (fore ? 0 : S_FLIPX));
    move_sprite(base, (uint8_t)(px + 8), (uint8_t)(py + 16));
    if (p->swing) p->swing--;
}

static void draw_ball(void) {
    if (!ball_live) { hide(OAM_BALL); hide(OAM_SHADOW); return; }
    project(ball.x >> 4, ball.z >> 4, ball.h >> 4);
    move_sprite(OAM_BALL, (uint8_t)(sx + 4), (uint8_t)(sy + 14));
    if (over_table(&ball)) {
        project(ball.x >> 4, ball.z >> 4, 0);
        move_sprite(OAM_SHADOW, (uint8_t)(sx + 4), (uint8_t)(sy + 15));
    } else hide(OAM_SHADOW);
}

// ---------------------------------------------------------------------------
// Title screen: pick you and your opponent
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

static void draw_title_names(void) {
    char row[21];
    uint8_t blank[20];
    memset(row, ' ', 20);
    row[20] = 0;
    place_name(row, 0, pl[NEAR].who, title_row == 0);
    place_name(row, 10, pl[FAR].who, title_row == 1);
    memset(blank, 0, 20);
    set_bkg_tiles(0, 11, 20, 1, blank);
    put_text(0, 11, row);
}

static void draw_title_faces(void) {
    uint8_t i;
    for (i = 0; i < 12; i++) hide(i);
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
    clear_bg();
    put_text(3, 2, "PING PONG PALS");
    put_text(2, 6, "YOU");
    put_text(16, 6, "VS");
    draw_title_names();
    put_text(4, 15, "PRESS START");
    load_characters();
    draw_title_faces();
}

static void update_title(void) {
    player_t *p;
    if (pressed & (J_UP | J_DOWN)) { title_row ^= 1; draw_title_names(); }
    if (pressed & (J_LEFT | J_RIGHT)) {
        p = &pl[title_row];
        if (pressed & J_RIGHT) p->who = (p->who + 1) % NUM_CHARACTERS;
        else                   p->who = (p->who + NUM_CHARACTERS - 1) % NUM_CHARACTERS;
        load_characters();
        draw_title_names();
        sfx_bounce();
    }
    if (pressed & (J_START | J_A)) {
        initrand(sys_time);
        score[0] = score[1] = 0;
        pl[NEAR].x = 0;
        set_bkg_tiles(0, 0, 20, 18, table_map);
        setup_serve();
    }
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
        set_sprite_palette(PAL_BALL, 1, ball_pal);
        set_sprite_palette(PAL_PADDLE, 1, paddle_pal);
    }

    build_table();
    load_font(T_FONT);
    load_sprite16(body_art, SPR_BODY);
    load_sprite8x16(paddle_art, SPR_PADDLE);
    load_sprite8x16(ball_art, SPR_BALL);
    load_sprite8x16(shadow_art, SPR_SHADOW);
    set_sprite_tile(OAM_BALL, SPR_BALL);
    set_sprite_prop(OAM_BALL, PAL_BALL);
    set_sprite_tile(OAM_SHADOW, SPR_SHADOW);
    set_sprite_prop(OAM_SHADOW, PAL_BALL);
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
        prev_keys = keys;

        music_tick();
        if (pressed & J_SELECT) music_toggle();

        if (state == ST_TITLE) { update_title(); continue; }

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
                if (score[pt_winner] >= WIN_SCORE) {
                    state = ST_OVER;
                    show_msg(16, characters[pl[pt_winner].who].name, " WINS");
                    show_msg(17, "PRESS START", 0);
                } else setup_serve();
            }
        } else if (state == ST_OVER) {
            if (pressed & (J_START | J_A)) { go_title(); continue; }
        }

        draw_ball();
        draw_player(NEAR);
        draw_player(FAR);
    }
}
