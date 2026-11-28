// Ping Pong Pals: table tennis against a CPU opponent (one of the other characters).
//
// Seen from a 3/4 top-down angle: you stand at the bottom (seen from behind), the CPU at the top.
// The ball lives in 3D world coordinates:
//   x  left/right in pixels (0 = middle of the table)
//   z  depth in pixels along the table (0 = the net, negative = CPU side, positive = your side)
//   h  height in pixels above the table top (the floor is at -TABLE_HEIGHT)
// and is drawn at screen (x, z + NET_SCREEN_Y - h), with a shadow on the table or floor below it.
//
// Shots don't use free physics: every shot picks a landing spot and a flight time and solves for
// the velocity (and lengthens the flight until the ball clears the net). Spin bends the ball
// sideways while it flies and kicks it sideways when it bounces.
//
// How you play:
//   * Swings are automatic when the ball is in reach. Tap A for a fast drive, tap B for a soft lob,
//     hold Left/Right while hitting to aim.
//   * HOLD A (forehand, reaches further right) or B (backhand, reaches further left) to charge a
//     smash: you move slowly while charging, and LET GO when the ball is in front of you. A full
//     meter + good timing = PERFECT (hit-stop, flash, shake, big crack).
//   * Serves bounce on your own side first. Press A (fast serve) or B (spin serve) to toss, then
//     press again at the top of the toss for a perfect serve.
//   * The CPU rates every shot you hit (power + how far it must run for the time it has): easy ones
//     get attacked, hard ones often get a slow high floater back (smash it!), very hard ones make it
//     miss. Before it hits, it turns its body to the side it will hit to: watch it.
//
// Training (pick it in the menu before a match) happens in the basement with a ball robot instead of
// an opponent. Serve practice: serve into the robot's net. Follow: the robot shoots every ball at you,
// so you only practise the timing of your swing. Random: it shoots all over the table, and turns its
// head just before, so you can see where the ball will go. It never ends: Start to change or quit.
//
// Art: tools/art/make_pingpong.py and make_pingpong_basement.py -> graphics/pingpong/.
// Sounds: tools/music/sfx_pingpong.py.
#include "bn_bg_palettes.h"
#include "bn_color.h"
#include "bn_fixed.h"
#include "bn_keypad.h"
#include "bn_log.h"
#include "bn_math.h"
#include "bn_optional.h"
#include "bn_regular_bg_ptr.h"
#include "bn_sprite_palettes.h"
#include "bn_sprite_ptr.h"
#include "bn_string.h"
#include "bn_string_view.h"
#include "bn_vector.h"

#include "bn_music_items.h"
#include "bn_regular_bg_items_pingpong_basement.h"
#include "bn_regular_bg_items_pingpong_btable.h"
#include "bn_regular_bg_items_pingpong_hall.h"
#include "bn_regular_bg_items_pingpong_table.h"
#include "bn_sound_items.h"
#include "bn_sprite_items_pingpong_ball.h"
#include "bn_sprite_items_pingpong_burst.h"
#include "bn_sprite_items_pingpong_flake.h"
#include "bn_sprite_items_pingpong_light.h"
#include "bn_sprite_items_pingpong_vmeter.h"
#include "bn_sprite_items_pingpong_oball.h"
#include "bn_sprite_items_pingpong_paddle.h"
#include "bn_sprite_items_pingpong_robot.h"
#include "bn_sprite_items_pingpong_shadow.h"
#include "bn_sprite_items_pingpong_vbar.h"
#include "bn_sprite_items_pingpong_vmark.h"
#include "bn_sprite_items_pingpong_vzone.h"
#include "bn_sprite_items_crowd_adam.h"
#include "bn_sprite_items_crowd_aiden.h"
#include "bn_sprite_items_crowd_alyssa.h"
#include "bn_sprite_items_crowd_billy.h"
#include "bn_sprite_items_crowd_bo.h"
#include "bn_sprite_items_crowd_charly.h"
#include "bn_sprite_items_crowd_cheng.h"
#include "bn_sprite_items_crowd_cherry.h"
#include "bn_sprite_items_crowd_ella.h"
#include "bn_sprite_items_crowd_lillian.h"

#include "sb_audio.h"
#include "sb_characters.h"
#include "sb_core.h"
#include "sb_games.h"
#include "sb_save.h"
#include "sb_text.h"
#include "sb_ui.h"

namespace
{
    // =============================================================================================
    // Tuning. Change one thing at a time, then replay.
    // =============================================================================================

    // ---- Match (the difficulty is picked in a menu before each match)
    constexpr int POINTS_TO_WIN = 5;            // first to this many points ...
    constexpr int WIN_BY = 2;                   // ... with a 2-point lead, like real table tennis
    constexpr int SERVE_SWITCH_EVERY = 2;       // the serve changes sides every this many points (every point at deuce)
    // Records: the best result for each difficulty (any win beats any loss, then the bigger margin).

    // ---- The scoreboard (must match draw_scoreboard() in tools/art/make_pingpong.py)
    constexpr int SCOREBOARD_YOU_X = 16 - 120;  // centre of the left card (your score), Butano coordinates
    constexpr int SCOREBOARD_CPU_X = 35 - 120;  // centre of the right card
    constexpr int SCOREBOARD_Y = 119 - 80;
    constexpr int NAME_BOARD_WIDTH = 96;        // the names board at the top is this wide (pixels)

    // ---- Ball physics (pixels and frames)
    constexpr bn::fixed GRAVITY = 0.08;         // pulls the ball down every frame (lower = floatier, slower)
    constexpr bn::fixed TABLE_BOUNCE = 0.8;     // vertical speed kept when bouncing on the table
    constexpr bn::fixed FLOOR_BOUNCE = 0.5;     // vertical speed kept when bouncing on the floor
    constexpr bn::fixed FLOOR_FRICTION = 0.7;   // sideways speed kept when bouncing on the floor
    constexpr bn::fixed NET_HEIGHT = 7;         // the ball must be higher than this to cross the net
    constexpr bn::fixed NET_MARGIN = 4;         // planned shots clear the net by at least this much
    constexpr int SHOT_MIN_FRAMES = 16;         // no shot is ever faster than this
    constexpr int SPIN_KICK = 14;               // a spinning ball jumps sideways this many frames' worth of spin when it bounces

    // ---- You
    constexpr bn::fixed PLAYER_SPEED = 1.6;     // left/right speed (pixels per frame)
    constexpr bn::fixed PLAYER_SPEED_Z = 0.8;   // up/down speed
    constexpr bn::fixed PLAYER_REACH_X = 22;    // auto-swing reaches balls this far to either side
    constexpr bn::fixed PLAYER_REACH_FRONT = 12;// ... this far in front of you (toward the net)
    constexpr bn::fixed PLAYER_REACH_BACK = 6;  // ... and this far behind you
    constexpr bn::fixed WHIFF_MARGIN = 14;      // a ball this much beyond reach still makes you swing (and miss)
    constexpr int SHOT_BUFFER_FRAMES = 20;      // A/B tapped up to this many frames before contact still counts

    // Your shots: flight time in frames (lower = faster), landing depth (negative = CPU side),
    // landing x when aiming with Left/Right, and the "power" the CPU feels when returning it.
    constexpr int DRIVE_FRAMES = 40;            // tap A: fast drive
    constexpr int DRIVE_DEPTH = -26;
    constexpr int DRIVE_AIM_X = 30;
    constexpr int DRIVE_POWER = 40;
    constexpr int LOB_FRAMES = 64;              // tap B: soft lob, slow and high, aims wider
    constexpr int LOB_DEPTH = -18;
    constexpr int LOB_AIM_X = 38;
    constexpr int LOB_POWER = 8;
    constexpr int PLAIN_FRAMES = 50;            // no button: a plain push to the middle
    constexpr int PLAIN_DEPTH = -22;
    constexpr int PLAIN_POWER = 12;

    // ---- Charged shots (hold A or B, let go to hit). How fast the meter fills and how exact the timing
    // must be depend on the difficulty: see LEVELS below.
    constexpr int CHARGE_START_FRAMES = 8;      // hold A/B this long to start charging (shorter = a tap)
    constexpr bn::fixed CHARGE_MOVE_FACTOR = 0.4; // you move this much slower while charging
    constexpr bn::fixed CHARGE_REACH_STRONG = 6;// your forehand (A) reaches this much further right, backhand (B) left
    constexpr bn::fixed CHARGE_REACH_WEAK = 8;  // ... and this much less on the other side
    constexpr bn::fixed CHARGE_REACH_FRONT = 4; // charged swings reach a little further toward the net
    constexpr int TIMING_HINT_FRAMES = 45;      // how long "Early" / "Late" shows after a smash that wasn't PERFECT
    constexpr int PERFECT_CHARGE = 75;          // % of the meter needed for a PERFECT shot (the gold part)
    // Where a charged shot goes: forehand (A) to the LEFT side of the table, backhand (B) to the RIGHT.
    // Hold the same direction to go wider, the other direction to go closer to the middle.
    constexpr int SMASH_SIDE_X = 26;
    constexpr int SMASH_WIDE_X = 38;
    constexpr int SMASH_NARROW_X = 12;
    constexpr bn::fixed SMASH_MIN_HEIGHT = 16;  // a charged hit on a ball this high is a SMASH, lower is a power drive
    constexpr int SMASH_SLOW_FRAMES = 34;       // smash flight time with an empty meter ...
    constexpr int SMASH_FAST_FRAMES = 20;       // ... and with a full one
    constexpr int POWER_SLOW_FRAMES = 40;       // same for a power drive (low ball)
    constexpr int POWER_FAST_FRAMES = 27;
    constexpr int PERFECT_FRAMES_BONUS = 4;     // a PERFECT shot is this many frames faster still
    constexpr int SMASH_DEPTH = -24;
    constexpr int SMASH_AIM_X = 32;
    constexpr int SMASH_POWER = 50;             // CPU difficulty: base + up to CHARGE_POWER for a full meter
    constexpr int POWER_DRIVE_POWER = 36;
    constexpr int CHARGE_POWER = 20;
    constexpr int PERFECT_POWER = 26;           // extra difficulty for the CPU on a PERFECT shot

    // ---- Serves: HOLD A (fast serve) or B (spin serve). A marker climbs a bar next to you; LET GO in the
    // red box for a perfect serve (green = strong, gold = ok, the rest = weak). The target sits somewhere
    // new every serve. If the marker reaches the top, the serve is cancelled: press again.
    // Speed and zone sizes: see LEVELS.
    constexpr bn::fixed TOSS_SPEED = 1.3;       // how fast the ball goes up when tossed (after you let go)
    constexpr int SERVE_TRACK = 58;             // the marker climbs this many pixels
    constexpr int SERVE_TOSS_HEIGHT = 30;       // the toss goes this high ...
    constexpr int SERVE_PADDLE_HEIGHT = 4;      // ... and falls back to paddle height exactly when the marker is in the red box
    constexpr int SERVE_SWING_FRAMES = 14;      // the serve's own swing animation (low behind, brushing up and forward)
    constexpr int CPU_SERVE_CORNER_X = 34;      // the CPU serves from a corner of the table, like the pros
    constexpr bn::fixed CPU_TOSS_SPEED = 1.7;   // its toss
    constexpr int SERVE_TARGET_MIN = 26;        // the perfect box is somewhere between here ...
    constexpr int SERVE_TARGET_MAX = 50;        // ... and here (pixels up the bar)
    constexpr int SERVE_OWN_BOUNCE_Z = 20;      // the serve first bounces this far from the net on the server's side
    constexpr int SERVE_FIRST_FRAMES = 12;      // flight time from the paddle to that first bounce
    constexpr int SERVE_AIM_X = 30;
    // Fast serve (A): flight frames after the first bounce, and CPU power. [weak, ok, strong, perfect]
    constexpr int SERVE_FAST_FRAMES[4] = { 46, 40, 34, 16 };
    // A plain ball starting low off your side must arc up to clear the net, so plan_shot stretches any fast
    // serve to about 34 frames. A PERFECT topspin serve instead dips hard (extra pull down while it flies),
    // so it can be fast and still clear the net: about 2.9 px/frame, a bit faster than the fastest smash.
    constexpr bn::fixed SERVE_TOPSPIN_DIP = 0.3;
    constexpr int SERVE_FAST_POWER[4] = { 10, 20, 32, 46 };
    constexpr int SERVE_FAST_DEPTH = -26;
    // Spin serve (B): slower, but curves and kicks sideways after it bounces. [weak, ok, strong, perfect]
    constexpr int SERVE_SPIN_FRAMES[4] = { 58, 55, 50, 46 };
    constexpr bn::fixed SERVE_SPIN_AMOUNT[4] = { 0.008, 0.015, 0.022, 0.03 };   // sideways push per frame
    constexpr int SERVE_SPIN_POWER[4] = { 8, 18, 32, 48 };
    constexpr int SERVE_SPIN_DEPTH = -18;

    // ---- CPU: three difficulty levels
    struct cpu_level
    {
        const char* name;
        bn::fixed speed;            // running speed (pixels per frame)
        int reaction;               // frames before it starts running after you hit
        int attack_below;           // your shot easier than this: it attacks
        int weak_from;              // this hard or harder: a slow, high floater you can smash ...
        int weak_chance_range;      // ... and up to this much below that, a growing chance of one
        int miss_from;              // this hard or harder: it hits the net or out
        int tell_frames;            // it turns to show where it will hit this many frames before hitting
        int normal_frames;          // its shots' flight time (lower = faster)
        int attack_frames;
        int serve_frames;
        int spin_serve_percent;     // how often it serves with spin
        // ... and for you:
        int charge_frames;          // frames to fill your smash meter (Hard fills faster ...)
        bn::fixed sweet_front;      // ... but a PERFECT needs the ball between this far in front of you
        bn::fixed sweet_back;       //     and this far behind you when you let go
        bn::fixed serve_speed;      // serve bar: how fast the marker climbs (pixels per frame)
        int serve_perfect;          // serve bar: within this many pixels of the line = PERFECT ...
        int serve_strong;           // ... this many = strong (green) ...
        int serve_ok;               // ... this many = ok (gold); further = weak. (Art: SERVE_ZONES in make_pingpong.py)
        int perfect_serve_floater;  // % chance that a PERFECT serve comes back as a slow floater
        int good_serve_floater;     // ... and a strong serve
        int perfect_return;         // % chance it still blocks back one of your PERFECT smashes ...
        int perfect_return_close;   // ... extra % when the smash goes close to where it already stands
    };

    constexpr cpu_level LEVELS[3] = {
        //  name      speed react attack weak range miss tell normal attack serve spin% | charge front back  bar   perf str ok | float strong | ret close
        { "Easy",   0.95, 12,   14,    50,  26,   82,  30,  58,    46,    58,   0,    30,    10,   3,    0.6,  2,   8,  14,  100, 40,    5,  20 },
        { "Medium", 1.1,  10,   20,    62,  26,   92,  22,  52,    40,    52,   20,   24,    6,    1,    0.8,  1,   7,  12,  100, 30,    30, 45 },
        { "Hard",   1.3,  8,    28,    72,  24,   102, 16,  46,    34,    44,   45,   24,    6,    1,    1.05, 1,   5,  10,  85,  20,    45, 40 },
    };

    // ---- Training with the ball robot (same Easy / Medium / Hard choice as a match)
    struct train_level
    {
        const char* name;
        int feed_frames;            // the robot's ball: flight time to its bounce on your side (lower = faster)
        int feed_gap;               // after your ball lands in its net, the robot shoots the next one this much later
        int miss_gap;               // ... and this much later after a miss
        bn::fixed sweet_front;      // a PERFECT smash needs the ball between this far in front of you ...
        bn::fixed sweet_back;       // ... and this far behind you when you let go
        int charge_frames;          // frames to fill the smash meter
    };

    constexpr train_level TRAIN_LEVELS[3] = {
        //  name      feed gap  miss  front back charge
        { "Easy",     64,  30,  60,   12,   4,   30 },
        { "Medium",   52,  22,  50,   8,    2,   24 },
        { "Hard",     34,  6,   34,   5,    1,   20 },
    };

    constexpr int TRAIN_PAUSE_FRAMES = 40;      // serve practice: pause after each serve
    constexpr int ROBOT_FIRST_WAIT = 60;        // the robot's first ball comes this long after you start
    constexpr int FOLLOW_SPREAD = 6;            // Follow: the ball arrives up to this far to either side of you
    constexpr int RANDOM_SPREAD = 48;           // Random: the ball arrives anywhere up to this far from the middle
    constexpr bn::fixed ROBOT_TURN_SPEED = 0.2; // head frames per frame while it turns
    // The robot's catch net, in ball coordinates: a back wall just past the far end of the table, side walls
    // along the far half of the table, up to NET_TOP_H high (see draw_robot() in the art script).
    constexpr int CATCH_BEYOND = 3;             // the back of the net is this far past the far end of the table
    constexpr bn::fixed NET_SIDE_X = 48;        // the side nets ...
    constexpr bn::fixed NET_SIDE_FROM_Z = -21;  // ... run from here to the back
    constexpr bn::fixed NET_TOP_H = 20;         // a ball higher than this stops at the top of the net and drops in
    constexpr bn::fixed NET_REST_H = -3;        // a caught ball ends up in the tray at this height

    enum drill_kind { DRILL_SERVE = 0, DRILL_FOLLOW = 1, DRILL_RANDOM = 2 };

    constexpr int CPU_RUN_WEIGHT = 60;         // difficulty = power + run * this, run = 1 when it only just makes it
    constexpr int CPU_JITTER = 8;               // small random wobble so it isn't robotic
    constexpr int CPU_SERVE_DELAY = 70;         // frames before the CPU serves
    // After returning a hard shot the CPU is off balance: it waits this long before walking back to the
    // middle: (power of your shot - RECOVER_FROM_POWER) x RECOVER_PERCENT / 100 frames. A PERFECT smash
    // out wide leaves the other side open for a while.
    constexpr int RECOVER_FROM_POWER = 30;
    constexpr int RECOVER_PERCENT = 60;
    constexpr int CPU_WEAK_FRAMES = 74;         // its floater is slow and high
    constexpr int RALLY_SPEEDUP_MAX = 8;        // long rallies get up to this many frames faster per shot
    constexpr int RALLY_HITS_PER_SPEEDUP = 2;   // ... one frame faster every this many hits
    constexpr bn::fixed CPU_REACH_X = 14;       // its swing reaches this far to either side
    constexpr bn::fixed CPU_REACH_FRONT = 10;   // ... this far in front (toward the net)
    constexpr bn::fixed CPU_REACH_BACK = 6;     // ... and behind
    constexpr bn::fixed CPU_HAND_OFFSET = 7;    // it runs to have the ball this far to its side

    // ---- Timing and effects
    constexpr int POINT_PAUSE_FRAMES = 80;      // pause after a point
    constexpr int MATCH_END_FRAMES = 110;       // the winner cheers this long before the result box
    constexpr int SWING_FRAMES = 12;            // length of the normal swing animation
    constexpr int SMASH_ANIM_FRAMES = 12;       // length of the jumping smash animation
    constexpr int SMASH_CONTACT_FRAMES = 2;     // the paddle meets the ball this long after you let go
    constexpr int PERFECT_FREEZE_FRAMES = 7;    // hit-stop on a PERFECT shot
    constexpr int PERFECT_SHAKE_FRAMES = 14;    // screen shake after it
    constexpr bn::fixed PERFECT_FLASH = 0.6;    // white flash strength (fades out)
    constexpr int TRAIL_LENGTH = 3;             // ghost balls behind a smash

    // ---- SORT LAYERS: Butano can only handle 16 different (BG priority, z order) pairs among the sprites
    // on screen at once ("No more sprite sort layers available"). Reuse the same few z values.

    // ---- Debug
    constexpr bool DEBUG_LOG = false;           // BN_LOG every point (for the test harness)

    // =============================================================================================
    // Geometry (must match tools/art/make_pingpong.py)
    // =============================================================================================
    constexpr int NET_SCREEN_Y = 5;             // screen y of the net (Butano coordinates)
    constexpr bn::fixed TABLE_HALF_W = 44;
    constexpr bn::fixed TABLE_FAR_Z = -37;
    constexpr bn::fixed TABLE_NEAR_Z = 36;
    constexpr bn::fixed TABLE_HEIGHT = 10;      // the floor is this far below the table top
    constexpr int FEET_TO_CENTER = 14;          // character sprite centre is this far above its feet

    constexpr bn::fixed PLAYER_X_LIMIT = 62;    // the edges of the rug (so you can't stand in front of the scoreboard)
    constexpr bn::fixed PLAYER_Z_MIN = 44;
    constexpr bn::fixed PLAYER_Z_MAX = 54;
    constexpr bn::fixed PLAYER_START_Z = 46;
    constexpr bn::fixed CPU_X_LIMIT = 66;
    constexpr bn::fixed CPU_Z_MIN = -54;
    constexpr bn::fixed CPU_Z_MAX = -40;
    constexpr bn::fixed CPU_HOME_Z = -45;
    constexpr bn::fixed HIT_MIN_H = -7;         // balls lower than this (below the table top) can't be hit

    // The ball robot (must match HEAD_X / HEAD_Y in tools/art/make_pingpong_basement.py: its head is drawn
    // centred on screen pixel (119, 28), one pixel above the middle of its 32x32 sprite).
    constexpr int ROBOT_HEAD_X = 119 - 120;     // sprite centre, Butano coordinates
    constexpr int ROBOT_HEAD_Y = 28 + 1 - 80;
    constexpr bn::fixed ROBOT_MOUTH_Z = -39;    // the ball leaves its mouth here (2 px below the head's centre)
    constexpr bn::fixed ROBOT_MOUTH_H = 16;

    enum side { PLAYER = 0, CPU = 1 };

    // ---- The spectators: everyone who isn't playing watches from the sides of the room and cheers after
    // every point. Art: tools/art/make_crowd.py (same order as sb_characters.cpp).
    const bn::sprite_item& crowd_sprite(int character)
    {
        static const bn::sprite_item* items[] = {
            &bn::sprite_items::crowd_adam, &bn::sprite_items::crowd_charly, &bn::sprite_items::crowd_billy,
            &bn::sprite_items::crowd_bo, &bn::sprite_items::crowd_cherry, &bn::sprite_items::crowd_lillian,
            &bn::sprite_items::crowd_cheng, &bn::sprite_items::crowd_ella, &bn::sprite_items::crowd_aiden,
            &bn::sprite_items::crowd_alyssa,
        };
        return *items[character];
    }

    enum crowd_frame { CROWD_FRONT = 0, CROWD_SIDE = 1, CROWD_CHEER = 2 };

    // Where they stand (sprite centre, Butano coordinates) and which way they face (+1 right, -1 left).
    constexpr int SPECTATOR_SPOTS[8][3] = {
        { -104, -24, 1 }, { -86, -16, 1 }, { -104, 4, 1 }, { -86, 8, 1 },     // left wall (above the scoreboard)
        { 86, 2, -1 }, { 104, 10, -1 }, { 86, 30, -1 }, { 104, 38, -1 },      // right side (below the tree)
    };
    constexpr int CHEER_FRAMES = 50;            // how long they jump after a point

    enum shot_kind
    {
        SHOT_PLAIN, SHOT_DRIVE, SHOT_SMASH, SHOT_POWER, SHOT_LOB, SHOT_SERVE_FAST, SHOT_SERVE_SPIN,
        SHOT_CPU_NORMAL, SHOT_CPU_ATTACK, SHOT_CPU_WEAK, SHOT_CPU_ERROR, SHOT_CPU_SERVE
    };

    enum cpu_plan { PLAN_ATTACK, PLAN_NORMAL, PLAN_WEAK, PLAN_ERROR };

    enum contact { C_NONE, C_TABLE, C_FLOOR, C_NET };

    enum class state { SERVE, TOSS, RALLY, POINT, DONE };

    enum serve_quality { SERVE_WEAK = 0, SERVE_OK = 1, SERVE_GOOD = 2, SERVE_PERFECT = 3 };

    struct ball_state
    {
        bn::fixed x, z, h, vx, vz, vh;
        bn::fixed spin;     // sideways push added to vx every frame
        bn::fixed dip = 0;  // extra pull down every frame (heavy topspin), gone after the next bounce
    };

    // What was picked last time in the menu: the difficulty (index into LEVELS / TRAIN_LEVELS), match or
    // training, and the drill.
    int g_level_choice = 1;
    bool g_training = false;
    int g_drill_choice = DRILL_FOLLOW;

    bool over_table(bn::fixed x, bn::fixed z)
    {
        return bn::abs(x) <= TABLE_HALF_W && z >= TABLE_FAR_Z && z <= TABLE_NEAR_Z;
    }

    // One frame of ball physics. Returns what the ball touched.
    contact step_ball(ball_state& b)
    {
        bn::fixed old_z = b.z;
        b.vx += b.spin;
        b.x += b.vx;
        b.z += b.vz;
        b.vh -= GRAVITY + b.dip;
        b.h += b.vh;

        // The net: crossing z = 0 too low.
        bool crossed = (old_z < 0 && b.z >= 0) || (old_z > 0 && b.z <= 0);

        if(crossed && b.h < NET_HEIGHT && b.h > -2 && bn::abs(b.x) <= TABLE_HALF_W + 3)
        {
            b.z = old_z < 0 ? bn::fixed(-1) : bn::fixed(1);
            b.vz = -b.vz / 5;
            b.vx /= 2;
            b.spin = 0;

            if(b.vh > 0)
            {
                b.vh = 0;
            }

            return C_NET;
        }

        if(b.vh < 0)
        {
            if(over_table(b.x, b.z) && b.h <= 0 && b.h - b.vh > -3)
            {
                b.h = 0;
                b.dip = 0;
                b.vh = -b.vh * TABLE_BOUNCE;

                // Spin makes the ball jump sideways off the table, then it fades.
                b.vx += b.spin * SPIN_KICK;
                b.spin /= 2;

                if(b.vh < bn::fixed(0.3))
                {
                    b.vh = 0;
                    b.vx = b.vx * FLOOR_FRICTION;
                    b.vz = b.vz * FLOOR_FRICTION;
                }

                return C_TABLE;
            }

            if(b.h <= -TABLE_HEIGHT)
            {
                b.h = -TABLE_HEIGHT;
                b.dip = 0;
                b.vh = -b.vh * FLOOR_BOUNCE;
                b.vx = b.vx * FLOOR_FRICTION;
                b.vz = b.vz * FLOOR_FRICTION;
                b.spin = 0;

                if(b.vh < bn::fixed(0.3))
                {
                    b.vh = 0;
                }

                return C_FLOOR;
            }
        }

        return C_NONE;
    }

    // Height after t frames of flight (matches step_ball exactly while nothing is touched).
    bn::fixed height_after(bn::fixed h0, bn::fixed vh0, int t, bn::fixed gravity)
    {
        return h0 + vh0 * t - gravity * (t * (t + 1) / 2);
    }

    // Aims the ball at (tx, tz) on the table, landing after `frames` frames (allowing for b.spin).
    // Lengthens the flight (a higher, slower arc) until the ball clears the net.
    void plan_shot(ball_state& b, bn::fixed tx, bn::fixed tz, int frames)
    {
        frames = bn::max(frames, SHOT_MIN_FRAMES);
        bn::fixed gravity = GRAVITY + b.dip;

        for(int attempt = 0; attempt < 30; ++attempt, frames += 2)
        {
            b.vx = (tx - b.x - b.spin * (frames * (frames + 1) / 2)) / frames;
            b.vz = (tz - b.z) / frames;
            b.vh = (gravity * (frames * (frames + 1) / 2) - b.h) / frames;

            if(b.vz == 0)
            {
                return;
            }

            bn::fixed t_net = -b.z / b.vz;

            if(t_net <= 0 || t_net >= frames)
            {
                return;
            }

            int t = t_net.ceil_integer();

            if(height_after(b.h, b.vh, t, gravity) >= NET_HEIGHT + NET_MARGIN)
            {
                return;
            }
        }
    }

    bn::fixed clamp(bn::fixed v, bn::fixed lo, bn::fixed hi)
    {
        return v < lo ? lo : v > hi ? hi : v;
    }

    bn::fixed approach(bn::fixed v, bn::fixed target, bn::fixed step)
    {
        if(v < target)
        {
            return bn::min(v + step, target);
        }

        return bn::max(v - step, target);
    }

    void set_white_flash(bn::fixed intensity)
    {
        bn::bg_palettes::set_fade(bn::color(31, 31, 31), intensity);
        bn::sprite_palettes::set_fade(bn::color(31, 31, 31), intensity);
    }

    // Start during training: 0 = continue, 1 = change drill (back to the menu), 2 = quit to the map.
    int training_pause()
    {
        sb::sfx(bn::sound_items::sfx_select);
        sb::panel(9, 3, 12, 3);
        sb::label title;
        title.set(0, 3 * 8 - 80 + 8 + 4, "Paused", sb::text_color::BLUE, sb::align::CENTER);
        constexpr bn::string_view items[] = { "Continue", "Change drill", "Quit to map" };
        int choice = sb::menu(8, 7, items, 0, true);
        title.clear();
        sb::panel_clear();
        sb::update();
        return choice < 0 ? 0 : choice;
    }

    // A character with a paddle.
    struct actor
    {
        int character;
        bn::sprite_ptr sprite;
        bn::sprite_ptr paddle;
        bn::fixed x, z;
        int walk = 0;
        int swing = 0;          // counts down during a normal swing
        int smash = 0;          // counts down during the jumping smash
        int paddle_side = 1;    // +1: paddle on the screen-right, -1: screen-left
        int charge_side = 0;    // you, charging: +1 forehand (right), -1 backhand (left)
        bool charge_full = false;
        int charge_pct = 0;     // how full the smash meter is (0..100): the paddle winds up further
        int windup_t = 0;       // frames it has been winding up
        bool topspin_jump = true;   // the swing (start_smash) jumps: yes for your smash, no for the CPU's shots
        int windup = 0;         // the CPU about to hit: +1 forehand (ball goes to the right of it on screen),
                                // -1 backhand (ball goes to its left)
        bn::fixed swing_x, swing_y; // screen spot the swing aims at
        bool cheering = false;
        bool serve_stance = false;  // sideways and bent over, getting ready to serve
        int serve_swing = 0;        // counts down during the serve's own swing
        int serve_dir = 1;          // serving: 1 = facing right (from the left half of the table), -1 = facing left
        bool facing_up;

        actor(int chr, bool up, bn::fixed start_x, bn::fixed start_z) :
            character(chr),
            sprite(sb::character_sprite(chr).create_sprite(0, 0, up ? sb::FRAME_UP : sb::FRAME_DOWN)),
            paddle(bn::sprite_items::pingpong_paddle.create_sprite(0, 0, 0)),
            x(start_x), z(start_z), facing_up(up)
        {
        }

        int center_y() const
        {
            return (z + NET_SCREEN_Y + TABLE_HEIGHT).round_integer() - FEET_TO_CENTER;
        }

        void start_swing(bn::fixed ball_x, bn::fixed ball_y_screen)
        {
            swing = SWING_FRAMES;
            smash = 0;
            paddle_side = ball_x >= x ? 1 : -1;
            swing_x = ball_x;
            swing_y = ball_y_screen;
        }

        void start_serve_swing()
        {
            serve_swing = SERVE_SWING_FRAMES;
            serve_stance = false;
            swing = 0;
            smash = 0;
        }

        // The CPU's shots use the same swing as your smash (without the jump), from the side it wound up on.
        void start_topspin(bn::fixed ball_x, bn::fixed ball_y_screen, int side)
        {
            smash = SMASH_ANIM_FRAMES;
            swing = 0;
            topspin_jump = false;
            paddle_side = side;
            swing_x = ball_x;
            swing_y = ball_y_screen;
        }

        void start_smash(bn::fixed ball_x, bn::fixed ball_y_screen)
        {
            smash = SMASH_ANIM_FRAMES;
            swing = 0;
            topspin_jump = true;
            // Swing from the side it was wound up on (forehand right, backhand left).
            paddle_side = charge_side != 0 ? charge_side : ball_x >= x ? 1 : -1;
            swing_x = ball_x;
            swing_y = ball_y_screen;
        }

        void draw(bool moving)
        {
            int cy = center_y();
            int lift = 0;
            int smash_t = SMASH_ANIM_FRAMES - smash;     // 0.. while smashing

            if(smash > 0)
            {
                // A little jump: up for 6 frames, then back down.
                lift = ! topspin_jump ? 0 : smash_t < 4 ? smash_t : bn::max(0, 8 - smash_t);
            }

            int frame;
            bool flip = false;

            if(cheering)
            {
                frame = sb::FRAME_CHEER;
            }
            else if(smash > 0 && smash_t < 4)
            {
                frame = sb::FRAME_SIDE;                 // still turned to the side it wound up on
                flip = paddle_side < 0;
            }
            else if(serve_stance || serve_swing > 0)
            {
                frame = sb::FRAME_SIDE;                 // side-on to the table, like a real serve
                flip = serve_dir < 0;                   // facing into the table from either side
                lift = serve_stance ? -2 : 0;           // bent over a little while waiting
            }
            else if(charge_side != 0 || windup != 0)
            {
                frame = sb::FRAME_SIDE;                 // body turned: forehand or backhand
                flip = (charge_side != 0 ? charge_side : windup) < 0;
            }
            else
            {
                int base = facing_up ? sb::FRAME_UP : sb::FRAME_DOWN;
                frame = base;

                if(moving)
                {
                    ++walk;
                    int step = (walk / 6) % 4;
                    frame = step == 1 ? base + 1 : step == 3 ? base + 2 : base;
                }
                else
                {
                    walk = 0;
                }
            }

            sprite.set_position(x.round_integer(), cy - lift);
            sprite.set_tiles(sb::character_sprite(character).tiles_item(), frame);
            sprite.set_horizontal_flip(flip);

            // Paddle: at the hand when resting, pulled back when winding up, reaching for the ball when swinging.
            int pframe = 0;
            int pside = paddle_side;
            bn::fixed px = x + paddle_side * 9;
            bn::fixed py = cy + 3;

            if(cheering)
            {
                py = cy - 10;
            }
            else if(smash > 0)
            {
                // Release, like a table tennis topspin: from the wind-up (low and back on its side), up and
                // forward through the ball, then the arm carries on up across the body to finish high on the
                // other side.
                --smash;
                int side = paddle_side;
                bn::fixed top = cy - lift;
                bn::fixed hit_x = swing_x - side * 3;
                // Keyframes: frame time, paddle x, paddle y, paddle frame, which way the blade points.
                struct key { int t; bn::fixed px, py; int frame, pside; };
                const key keys[] = {
                    { 0,  x + side * 11, top + 8,  3, side },      // wound up: low and back, blade angled down
                    { SMASH_CONTACT_FRAMES, hit_x, swing_y, 1, side },  // through the ball
                    { 4,  x + side * 5,  top - 7,  4, side },      // rising in front of the body
                    { 6,  x - side * 1,  top - 13, 0, side },      // up past the face
                    { 8,  x - side * 7,  top - 15, 0, -side },     // finish: high, over on the other side
                    { SMASH_ANIM_FRAMES, x - side * 7, top - 15, 0, -side },
                };
                int k = 0;

                while(k < 4 && smash_t >= keys[k + 1].t)
                {
                    ++k;
                }

                const key& a = keys[k];
                const key& b = keys[k + 1];
                bn::fixed f = bn::fixed(bn::min(smash_t, b.t) - a.t) / (b.t - a.t);
                px = a.px + (b.px - a.px) * f;
                py = a.py + (b.py - a.py) * f;
                pframe = f < bn::fixed(0.5) ? a.frame : b.frame;
                pside = f < bn::fixed(0.5) ? a.pside : b.pside;
            }
            else if(swing > 0)
            {
                --swing;
                int t = SWING_FRAMES - swing;   // 1..SWING_FRAMES

                if(t <= 3)
                {
                    pframe = 1;
                    // Lerp from the hand to just beside the ball.
                    bn::fixed f = bn::fixed(t) / 3;
                    bn::fixed tx = swing_x - paddle_side * 3;
                    bn::fixed ty = swing_y;
                    px = px + (tx - px) * f;
                    py = py + (ty - py) * f;
                }
                else if(t <= 8)
                {
                    pframe = 2;
                    bn::fixed tx = swing_x - paddle_side * 3;
                    px = (px + tx) / 2;
                    py = (py + swing_y) / 2 + 2;
                }
            }
            else if(serve_swing > 0)
            {
                // The serve: from low behind the body, brushing up under the ball and forward.
                --serve_swing;
                int t = SERVE_SWING_FRAMES - serve_swing;
                int dir = serve_dir;

                if(t <= 3)
                {
                    pframe = 2;
                    pside = -dir;
                    px = x - 9 * dir;
                    py = cy + 6;
                }
                else if(t <= 8)
                {
                    pframe = 1;
                    pside = dir;
                    bn::fixed f = bn::fixed(t - 3) / 5;
                    px = x - 9 * dir + 21 * dir * f;
                    py = cy + 6 - 10 * f;
                }
                else
                {
                    pframe = 0;
                    pside = dir;
                    px = x + 13 * dir;
                    py = cy - 9;
                }
            }
            else if(serve_stance)
            {
                // Paddle low and back behind the body.
                int dir = serve_dir;
                pframe = 2;
                pside = -dir;
                px = x - 9 * dir;
                py = cy + 6;
            }
            else if(charge_side != 0)
            {
                // Winding up: as the meter fills the paddle moves in a short arc back and down, close to the
                // body, ending with the blade angled about 45 degrees down (where the release swing starts).
                // It trembles when the meter is full.
                bn::fixed f = bn::fixed(bn::min(charge_pct, 100)) / 100;
                bn::fixed back = f < bn::fixed(0.5) ? f * 4 : bn::fixed(2);     // out a little first ...
                bn::fixed down = f < bn::fixed(0.5) ? f * 4 : 2 + (f - bn::fixed(0.5)) * 6;   // ... then down
                pframe = f < bn::fixed(0.5) ? 1 : 3;
                pside = charge_side;
                int shake = charge_full && (sb::frame_count() & 2) ? 1 : 0;
                px = x + charge_side * (9 + back) + shake;
                py = cy + 3 + down;
            }
            else if(windup != 0)
            {
                // The CPU about to hit: the same wind-up as your smash, back and down on the side it will hit
                // from (forehand to its right on screen, backhand to its left).
                bn::fixed f = bn::min(bn::fixed(windup_t) / 12, bn::fixed(1));
                bn::fixed back = f < bn::fixed(0.5) ? f * 4 : bn::fixed(2);
                bn::fixed down = f < bn::fixed(0.5) ? f * 4 : 2 + (f - bn::fixed(0.5)) * 6;
                pframe = f < bn::fixed(0.5) ? 1 : 3;
                pside = windup;
                px = x + windup * (9 + back);
                py = cy + 3 + down;
            }

            windup_t = windup != 0 ? windup_t + 1 : 0;
            paddle.set_tiles(bn::sprite_items::pingpong_paddle.tiles_item(), pframe);
            paddle.set_horizontal_flip(pside < 0);
            paddle.set_position(px.round_integer(), py.round_integer());
        }
    };

    struct match
    {
        // ---- scene
        bn::regular_bg_ptr hall = bn::regular_bg_items::pingpong_hall.create_bg(8, 48);
        bn::regular_bg_ptr table = bn::regular_bg_items::pingpong_table.create_bg(8, 48);
        bn::sprite_ptr ball_sprite = bn::sprite_items::pingpong_ball.create_sprite(0, 0, 1);
        bn::sprite_ptr shadow_sprite = bn::sprite_items::pingpong_shadow.create_sprite(0, 0, 0);
        bn::sprite_ptr meter = bn::sprite_items::pingpong_vmeter.create_sprite(0, 0, 0);
        bn::sprite_ptr burst = bn::sprite_items::pingpong_burst.create_sprite(0, 0, 0);
        bn::sprite_ptr serve_bar = bn::sprite_items::pingpong_vbar.create_sprite(0, 0, 0);
        bn::sprite_ptr serve_bar_top = bn::sprite_items::pingpong_vbar.create_sprite(0, 0, 1);
        bn::sprite_ptr serve_zone = bn::sprite_items::pingpong_vzone.create_sprite(0, 0, 0);
        bn::sprite_ptr serve_marker = bn::sprite_items::pingpong_vmark.create_sprite(0, 0, 0);
        bn::sprite_ptr robot = bn::sprite_items::pingpong_robot.create_sprite(ROBOT_HEAD_X, ROBOT_HEAD_Y, 2);
        sb::label my_score_label, cpu_score_label;
        sb::label timing_label;
        int timing_timer = 0;
        struct spectator
        {
            bn::sprite_ptr sprite;
            int spot;
        };
        bn::vector<spectator, 8> crowd;
        int cheer_timer = 0;
        bn::vector<bn::sprite_ptr, TRAIL_LENGTH> trail;
        bn::vector<bn::sprite_ptr, 6> flakes;
        bn::vector<bn::sprite_ptr, 8> lights;
        bn::optional<actor> me;
        bn::optional<actor> cpu;
        sb::label score_label, message_label, hint_label;
        int message_timer = 0;

        // ---- match
        int opponent = 0;
        int points_to_win = 5;
        const cpu_level* level = &LEVELS[1];
        int my_points = 0, cpu_points = 0;
        state st = state::SERVE;
        int timer = 0;
        int server = PLAYER;
        int last_winner = PLAYER;

        // ---- rally
        ball_state ball = {};
        int hitter = PLAYER;
        int bounces = 0;            // bounces on the receiver's side since the last hit
        bool point_decided = false;
        bool whiffed = false;
        int rally_hits = 0;
        shot_kind last_shot = SHOT_PLAIN;
        int toss_kind = SHOT_SERVE_FAST;
        int a_age = 1000, b_age = 1000;   // frames since A / B was last pressed
        int a_hold = 0, b_hold = 0;       // frames A / B have been held

        // ---- serves: the ball first bounces on the server's side, then goes here
        bool serve_bounce_pending = false;
        bn::fixed serve_tx = 0, serve_tz = 0, serve_spin = 0;
        int serve_frames = 40;
        int serve_quality_of_last = 1;
        // the serve timing bar
        bool serve_aiming = false;
        bool serve_wait_release = false;// after a cancelled serve: let go of the buttons first
        int serve_button = 0;           // 1 = A, 2 = B
        bn::fixed serve_mark = 0;       // 0..SERVE_TRACK, from the bottom of the bar
        int serve_zone_pos = 40;        // height of the perfect line, SERVE_TARGET_MIN..MAX

        // ---- charging a smash
        bool charging = false;
        int charge_button = 0;      // 1 = A (forehand), 2 = B (backhand)
        int charge = 0;
        bool last_perfect = false;  // your last hit was a PERFECT smash / shot
        int ball_hold = 0;          // a smash: frames until the paddle reaches the ball (it waits until then)
        bool pending_perfect = false;
        int incoming_power = 0;     // how hard your last shot was (the CPU recovers slower from hard ones)

        // ---- effects
        int freeze = 0, shake = 0, burst_timer = 0, trail_timer = 0;
        bn::fixed flash = 0;
        bn::fixed history_x[TRAIL_LENGTH * 3] = {}, history_y[TRAIL_LENGTH * 3] = {};

        // ---- CPU brain
        cpu_plan plan = PLAN_NORMAL;
        bn::fixed cpu_target_x = 0, cpu_target_z = CPU_HOME_Z;
        bn::fixed cpu_shot_x = 0;   // where its next shot will go ...
        bn::fixed cpu_shot_offset = 0; // ... as a distance from where it will stand (+ = to its right on screen)
        int cpu_shot_side = 1;      // +1 forehand (to its right), -1 backhand: it shows this before it hits
        int cpu_reaction = 0;

        // ---- training with the ball robot (no opponent, no crowd, the basement)
        bool training = false;
        bool in_basement = false;   // which room is on screen
        int drill = DRILL_FOLLOW;
        cpu_level train_settings = LEVELS[1];  // LEVELS[level] with the training smash timing
        bn::fixed robot_aim = 0;    // where the head points: -2 (far left) .. 2 (far right)
        bn::fixed robot_arrive_x = 0;   // where the next ball will reach you (x)
        int follow_offset = 0;
        bool caught = false;        // the ball is in the robot's net
        bool back_to_menu = false;  // play() ended because "Change drill" was picked in the pause menu
        bool target_ready = false;  // the robot has picked where its next ball goes (its head turns there)
        int robot_wait = 1;         // frames in the SERVE state before the robot shoots
        int rally_gap = TRAIN_PAUSE_FRAMES; // how long the POINT state lasts in training
        const bn::sprite_item* ball_item = &bn::sprite_items::pingpong_ball;    // white, or orange (the robot's)

        match()
        {
            robot.set_bg_priority(2);
            robot.set_z_order(0);

            hall.set_priority(3);
            table.set_priority(2);
            ball_sprite.set_bg_priority(1);
            shadow_sprite.set_bg_priority(2);
            meter.set_bg_priority(1);
            meter.set_visible(false);
            burst.set_bg_priority(1);
            burst.set_z_order(-5);
            burst.set_visible(false);


            serve_bar.set_bg_priority(1);
            serve_bar_top.set_bg_priority(1);
            serve_zone.set_bg_priority(1);
            serve_marker.set_bg_priority(1);
            serve_bar_top.set_visible(false);
            serve_bar_top.set_z_order(-3);
            serve_bar.set_visible(false);
            serve_zone.set_visible(false);
            serve_marker.set_visible(false);
            serve_bar.set_z_order(-3);
            serve_zone.set_z_order(-4);
            serve_marker.set_z_order(-5);

            for(int i = 0; i < TRAIL_LENGTH; ++i)
            {
                bn::sprite_ptr g = bn::sprite_items::pingpong_ball.create_sprite(0, 0, 0);
                g.set_bg_priority(1);
                g.set_z_order(1);
                g.set_visible(false);
                trail.push_back(g);
            }

            // Snow falling behind the window glass.
            for(int i = 0; i < flakes.max_size(); ++i)
            {
                bn::sprite_ptr f = bn::sprite_items::pingpong_flake.create_sprite(-100 + i * 7, -74 + (i * 11) % 22,
                                                                                    i % 2);
                f.set_bg_priority(3);
                flakes.push_back(f);
            }

            // Twinkling lights on the tree (screen pixel positions from the art).
            constexpr int light_pos[8][2] = { { 214, 15 }, { 221, 23 }, { 208, 30 }, { 226, 36 },
                                              { 213, 41 }, { 202, 50 }, { 229, 49 }, { 219, 53 } };

            for(const auto& p : light_pos)
            {
                bn::sprite_ptr l = bn::sprite_items::pingpong_light.create_sprite(p[0] - 120 + 1, p[1] - 80 + 1, 0);
                l.set_bg_priority(3);
                lights.push_back(l);
            }

            new_match();
        }

        // ------------------------------------------------------------------------------- setup
        void new_match()
        {
            opponent = sb::rand_range(0, sb::character_count - 2);

            if(opponent >= sb::player_character())
            {
                ++opponent;
            }

            points_to_win = POINTS_TO_WIN;
            training = g_training;
            drill = g_drill_choice;

            if(training)
            {
                // The robot doesn't think, so only your side of the settings matters, with training timing.
                const train_level& t = TRAIN_LEVELS[g_level_choice];
                train_settings = LEVELS[g_level_choice];
                train_settings.sweet_front = t.sweet_front;
                train_settings.sweet_back = t.sweet_back;
                train_settings.charge_frames = t.charge_frames;
                level = &train_settings;
            }
            else
            {
                level = &LEVELS[g_level_choice];
            }

            target_ready = false;
            back_to_menu = false;
            me.reset();
            cpu.reset();
            me.emplace(sb::player_character(), true, bn::fixed(0), PLAYER_START_Z);
            cpu.emplace(opponent, false, bn::fixed(0), CPU_HOME_Z);
            me->sprite.set_bg_priority(1);
            me->paddle.set_bg_priority(1);
            me->sprite.set_z_order(0);
            me->paddle.set_z_order(-2);
            cpu->sprite.set_bg_priority(3);
            cpu->paddle.set_bg_priority(3);
            cpu->sprite.set_z_order(0);
            cpu->paddle.set_z_order(-2);
            cpu->paddle_side = -1;

            crowd.clear();
            int spot = 0;

            // (Built for training too, so they're there if the menu shows the hall: set_room hides them.)
            for(int c = 0; c < sb::character_count && spot < 8; ++c)
            {
                if(c == sb::player_character() || c == opponent)
                {
                    continue;
                }

                const int* at = SPECTATOR_SPOTS[spot];
                bn::sprite_ptr sprite = crowd_sprite(c).create_sprite(at[0], at[1], CROWD_SIDE);
                sprite.set_horizontal_flip(at[2] < 0);
                sprite.set_bg_priority(2);
                sprite.set_z_order(at[1] > 0 ? -1 : 0);   // front row in front (only 2 values: see SORT LAYERS)
                crowd.push_back({ sprite, spot });
                ++spot;
            }

            cheer_timer = 0;
            my_points = 0;
            cpu_points = 0;
            last_winner = PLAYER;
            message_label.clear();
            hint_label.clear();
            score_label.clear();
            message_timer = 0;
            set_room(training);
            start_serve();
            // play() shows the serve messages; keep the screen clean for the intro / result boxes.
            message_label.clear();
            hint_label.clear();
            draw();
        }

        // The rec hall (matches) or the basement with the ball robot (training). The menu calls this too,
        // to show the room of whatever is highlighted.
        void set_room(bool basement)
        {
            if(basement != in_basement)
            {
                in_basement = basement;
                hall.set_item(basement ? bn::regular_bg_items::pingpong_basement : bn::regular_bg_items::pingpong_hall);
                table.set_item(basement ? bn::regular_bg_items::pingpong_btable : bn::regular_bg_items::pingpong_table);
            }

            robot.set_visible(basement);

            // The robot's balls are orange, like the ones in its hopper.
            ball_item = basement ? &bn::sprite_items::pingpong_oball : &bn::sprite_items::pingpong_ball;
            ball_sprite.set_item(*ball_item, 1);

            for(bn::sprite_ptr& g : trail)
            {
                g.set_item(*ball_item, 0);
            }

            for(bn::sprite_ptr& f : flakes)
            {
                f.set_visible(! basement);
            }

            for(bn::sprite_ptr& l : lights)
            {
                l.set_visible(! basement);
            }

            for(spectator& sp : crowd)
            {
                sp.sprite.set_visible(! basement);
            }

            if(cpu)
            {
                cpu->sprite.set_visible(! basement);
                cpu->paddle.set_visible(! basement);
            }
        }

        bool deuce() const
        {
            return my_points >= points_to_win - 1 && cpu_points >= points_to_win - 1;
        }

        bool match_over() const
        {
            int lead = bn::abs(my_points - cpu_points);
            return (my_points >= points_to_win || cpu_points >= points_to_win) && lead >= WIN_BY;
        }

        int current_server() const
        {
            if(training)
            {
                return drill == DRILL_SERVE ? PLAYER : CPU;     // CPU = the robot
            }

            int total = my_points + cpu_points;

            if(deuce())
            {
                return total % 2 == 0 ? PLAYER : CPU;
            }

            return (total / SERVE_SWITCH_EVERY) % 2 == 0 ? PLAYER : CPU;
        }

        void start_serve()
        {
            st = state::SERVE;
            timer = 0;
            server = current_server();
            point_decided = false;
            whiffed = false;
            rally_hits = 0;
            bounces = 0;
            serve_bounce_pending = false;
            cpu->serve_stance = false;
            cpu->cheering = false;
            cpu->windup = 0;
            cpu_target_z = CPU_HOME_Z;
            caught = false;
            ball_hold = 0;

            // Follow / Random flow like a rally: the robot's next ball leaves you where you are, still
            // charging a smash if you are.
            if(rally_drill())
            {
                hint_label.clear();
                robot_wait = 1;

                if(! target_ready)
                {
                    pick_robot_target();
                }

                hold_ball_at_server();
                return;
            }

            hide_serve_bar();
            serve_wait_release = false;
            stop_charging();
            me->cheering = false;
            me->z = PLAYER_START_Z;
            trail_timer = 0;

            if(training)
            {
                message_label.clear();
                message_timer = 0;

                hint_label.set(-116, 70, "Hold A: topspin  B: backspin", sb::text_color::WHITE);

                hold_ball_at_server();
                return;
            }

            if(server == CPU)
            {
                cpu_target_x = sb::rand_range(0, 1) ? CPU_SERVE_CORNER_X : -CPU_SERVE_CORNER_X;
                cpu->serve_dir = cpu_target_x > 0 ? -1 : 1;     // from its corner it faces into the table
                bn::string<32> text(sb::character_name(opponent));
                text.append(" serves");
                show_message(text, sb::text_color::WHITE, 70);
                hint_label.clear();
            }
            else
            {
                cpu_target_x = 0;
                show_message(deuce() && my_points == cpu_points ? "Deuce! Your serve" : "Your serve!",
                             sb::text_color::WHITE, 70);
                hint_label.set(-116, 70, "Hold A: topspin  B: backspin", sb::text_color::WHITE);
            }

            hold_ball_at_server();
        }

        void hold_ball_at_server()
        {
            if(server == PLAYER)
            {
                ball = { me->x + 9 * me->serve_dir, me->z - 2, 2, 0, 0, 0, 0 };
            }
            else if(training)
            {
                ball = { robot_mouth_x(), ROBOT_MOUTH_Z, ROBOT_MOUTH_H, 0, 0, 0, 0 };
            }
            else
            {
                ball = { cpu->x + 9 * cpu->serve_dir, cpu->z + 2, 2, 0, 0, 0, 0 };
            }
        }

        // ------------------------------------------------------------------------------- the ball robot
        // Follow or Random: the robot keeps shooting and play never stops.
        bool rally_drill() const
        {
            return training && drill != DRILL_SERVE;
        }

        bn::fixed robot_mouth_x() const
        {
            return bn::fixed(ROBOT_HEAD_X) + robot_aim * 2;
        }

        // Head frame 0..4 (far left .. far right) for an aim of -2..2.
        int robot_frame() const
        {
            return bn::max(0, bn::min(4, (robot_aim + 2).round_integer()));
        }

        // Where the next ball will reach you: Follow aims at you, Random at the spot it picked.
        bn::fixed robot_target_x() const
        {
            if(drill == DRILL_FOLLOW)
            {
                return clamp(me->x + follow_offset, -PLAYER_X_LIMIT + 6, PLAYER_X_LIMIT - 6);
            }

            return robot_arrive_x;
        }

        // Where the next ball goes: Follow aims at you when it shoots (give or take a little), Random picks
        // a spot now. Picked as soon as you hit the robot's ball back, so its head has time to turn.
        void pick_robot_target()
        {
            follow_offset = sb::rand_range(-FOLLOW_SPREAD, FOLLOW_SPREAD);
            robot_arrive_x = sb::rand_range(-RANDOM_SPREAD, RANDOM_SPREAD);
            target_ready = true;
        }

        // Every frame of Follow / Random: the head turns towards the next spot once it has one.
        void turn_robot_head()
        {
            if(target_ready)
            {
                robot_aim = approach(robot_aim, clamp(robot_target_x() / 22, -2, 2), ROBOT_TURN_SPEED);
            }
        }

        // Waiting to shoot (the ball is inside the robot).
        void update_robot()
        {
            hold_ball_at_server();

            if(timer >= robot_wait)
            {
                robot_feed();
            }
        }

        void robot_feed()
        {
            const train_level& t = TRAIN_LEVELS[g_level_choice];
            bn::fixed arrive_x = robot_target_x();
            bn::fixed tz = sb::rand_range(16, 28);

            // The ball keeps going the same way after its bounce, so to reach you at arrive_x it must land
            // a bit closer to the middle: by the time it gets from tz to you it has moved on by
            // (land - start) * (your z - tz) / (tz - start z).
            bn::fixed r = (me->z - tz) / (tz - ROBOT_MOUTH_Z);
            bn::fixed tx = (arrive_x + robot_mouth_x() * r) / (1 + r);
            tx = clamp(tx, -(TABLE_HALF_W - 4), TABLE_HALF_W - 4);

            ball = { robot_mouth_x(), ROBOT_MOUTH_Z, ROBOT_MOUTH_H, 0, 0, 0, 0 };
            plan_shot(ball, tx, tz, t.feed_frames);
            target_ready = false;
            after_hit(CPU, SHOT_CPU_NORMAL, 0);
            sb::sfx(bn::sound_items::sfx_paddle);
        }

        // The ball went past the far end of the table: the robot's net catches it and it drops into the tray.
        void catch_ball()
        {
            caught = true;
            ball.z = bn::max(ball.z, TABLE_FAR_Z - CATCH_BEYOND);
            ball.x = clamp(ball.x, -NET_SIDE_X, NET_SIDE_X);
            ball.h = bn::min(ball.h, NET_TOP_H);
            ball.vx = 0;
            ball.vz = 0;
            ball.spin = 0;
            ball.vh = bn::min(ball.vh, bn::fixed(0));
        }

        // One ball of training is over. Serve practice says how the serve went; Follow and Random just keep
        // going at the pace of a rally: the next ball comes soon after yours lands in the net (or after a miss).
        void training_rep(bool good, const bn::string_view& why)
        {
            point_decided = true;
            st = state::POINT;
            timer = 0;
            serve_bounce_pending = false;

            if(drill == DRILL_SERVE)
            {
                stop_charging();
            }

            if(drill == DRILL_SERVE)
            {
                // (How good the serve was already showed when you hit it: only a fault gets a message.)
                rally_gap = TRAIN_PAUSE_FRAMES;
                sb::sfx(good ? bn::sound_items::sfx_point : bn::sound_items::sfx_miss);

                if(! good)
                {
                    show_message(why, sb::text_color::RED, TRAIN_PAUSE_FRAMES);
                }

                return;
            }

            // Follow / Random: no "ping" when your ball lands in the net, only the miss sound.
            if(! good)
            {
                sb::sfx(bn::sound_items::sfx_miss);
            }

            const train_level& t = TRAIN_LEVELS[g_level_choice];
            rally_gap = good ? t.feed_gap : t.miss_gap;

            if(! target_ready)
            {
                pick_robot_target();
            }
        }

        // Names on the board at the top, the points on the flip scoreboard at the bottom left.
        void update_score_label()
        {
            const char* mine = sb::character_name(sb::player_character());
            const char* theirs = sb::character_name(opponent);
            bn::string<32> text(mine);
            text.append(" vs ");
            text.append(theirs);

            if(sb::text_width(text) > NAME_BOARD_WIDTH)
            {
                text = mine;
                text.append(" v ");
                text.append(theirs);
            }

            if(sb::text_width(text) > NAME_BOARD_WIDTH)
            {
                // Still too long: shorten both names.
                text.clear();

                for(int i = 0; i < 5 && mine[i]; ++i)
                {
                    text.push_back(mine[i]);
                }

                text.append(" v ");

                for(int i = 0; i < 5 && theirs[i]; ++i)
                {
                    text.push_back(theirs[i]);
                }
            }

            score_label.set(0, -71, text, sb::text_color::WHITE, sb::align::CENTER);
            my_score_label.set(SCOREBOARD_YOU_X, SCOREBOARD_Y, sb::to_text(my_points), sb::text_color::DARK,
                               sb::align::CENTER);
            cpu_score_label.set(SCOREBOARD_CPU_X, SCOREBOARD_Y, sb::to_text(cpu_points), sb::text_color::DARK,
                                sb::align::CENTER);
        }

        void show_message(const bn::string_view& text, sb::text_color color, int frames)
        {
            message_label.set(0, -14, text, color, sb::align::CENTER);
            message_timer = frames;
        }

        void hide_hud()
        {
            hide_serve_bar();
            score_label.clear();
            my_score_label.clear();
            cpu_score_label.clear();
            timing_label.clear();
            timing_timer = 0;
            message_label.clear();
            hint_label.clear();
            message_timer = 0;
            meter.set_visible(false);
        }

        // ------------------------------------------------------------------------------- input
        int aim_input() const
        {
            return bn::keypad::left_held() ? -1 : bn::keypad::right_held() ? 1 : 0;
        }

        // Which tap shot the buttons ask for: the most recently tapped of A and B.
        int chosen_button() const
        {
            bool a = a_age <= SHOT_BUFFER_FRAMES;
            bool b = b_age <= SHOT_BUFFER_FRAMES;

            if(a && b)
            {
                return a_age <= b_age ? 1 : 2;
            }

            return a ? 1 : b ? 2 : 0;
        }

        void stop_charging()
        {
            charging = false;
            charge = 0;
            charge_button = 0;

            if(me)
            {
                me->charge_side = 0;
                me->charge_full = false;
            }
        }

        // ------------------------------------------------------------------------------- shots
        bn::fixed default_aim_x() const
        {
            return clamp(ball.x / 2, -16, 16);
        }

        // Automatic swing (taps and no button).
        void player_hit()
        {
            // (Tapping A/B for a drive or a lob was removed: A and B are only for smashes.)
            int aim = aim_input();
            shot_kind kind = SHOT_PLAIN;
            int speedup = bn::min(rally_hits / RALLY_HITS_PER_SPEEDUP, RALLY_SPEEDUP_MAX);
            bn::fixed tx = default_aim_x();
            bn::fixed tz;
            int frames;
            int power;

            switch(kind)
            {
            case SHOT_DRIVE:
                tz = DRIVE_DEPTH;
                frames = DRIVE_FRAMES - speedup;
                tx = aim ? bn::fixed(aim * DRIVE_AIM_X) : tx;
                power = DRIVE_POWER;
                break;
            case SHOT_LOB:
                tz = LOB_DEPTH;
                frames = LOB_FRAMES;
                tx = aim ? bn::fixed(aim * LOB_AIM_X) : tx;
                power = LOB_POWER;
                break;
            default:
                tz = PLAIN_DEPTH;
                frames = PLAIN_FRAMES - speedup;
                power = PLAIN_POWER;
                break;
            }

            me->start_swing(ball.x, ball_screen_y());
            ball.spin = 0;
            plan_shot(ball, tx, tz, frames);
            last_perfect = false;
            after_hit(PLAYER, kind, power);
            sb::sfx(bn::sound_items::sfx_paddle);
        }

        // Can a charged swing reach the ball right now? (Forehand reaches further right, backhand left.)
        bool charged_reach(bn::fixed& dx, bn::fixed& dz) const
        {
            dx = ball.x - me->x;
            dz = ball.z - me->z;
            bn::fixed right = charge_button == 1 ? PLAYER_REACH_X + CHARGE_REACH_STRONG : PLAYER_REACH_X - CHARGE_REACH_WEAK;
            bn::fixed left = charge_button == 2 ? PLAYER_REACH_X + CHARGE_REACH_STRONG : PLAYER_REACH_X - CHARGE_REACH_WEAK;
            return ball.h >= HIT_MIN_H && dz >= -(PLAYER_REACH_FRONT + CHARGE_REACH_FRONT) && dz <= PLAYER_REACH_BACK &&
                   dx <= right && dx >= -left;
        }

        // Held the smash too long: a slow, soft block (the CPU will probably attack it).
        void weak_block()
        {
            me->start_swing(ball.x, ball_screen_y());
            stop_charging();
            ball.spin = 0;
            plan_shot(ball, default_aim_x(), LOB_DEPTH, LOB_FRAMES + 6);
            last_perfect = false;
            after_hit(PLAYER, SHOT_LOB, 0);
            sb::sfx(bn::sound_items::sfx_paddle);
            show_message("Too late! Weak return", sb::text_color::WHITE, 40);
        }

        // You let go of A/B after charging.
        void release_charge()
        {
            bn::fixed dx, dz;
            bool can_hit = st == state::RALLY && ! point_decided && hitter == CPU && bounces == 1 &&
                           charged_reach(dx, dz);

            if(! can_hit)
            {
                // Swung at nothing (too early or too late).
                if(st == state::RALLY && ! point_decided && hitter == CPU && bounces <= 1 && ball.z < me->z)
                {
                    show_timing_hint("Too early");
                }

                me->start_swing(me->x + me->charge_side * 12, me->center_y() - 4);
                sb::sfx(bn::sound_items::sfx_whoosh);
                stop_charging();
                a_age = b_age = 1000;
                return;
            }

            int pct = bn::min(charge * 100 / level->charge_frames, 100);
            bool sweet = dz >= -level->sweet_front && dz <= level->sweet_back;
            bool perfect = pct >= PERFECT_CHARGE && sweet;
            bool high = ball.h >= SMASH_MIN_HEIGHT;

            // Forehand goes to the left side of the table, backhand to the right side.
            int side = charge_button == 1 ? -1 : 1;
            int aim = aim_input();
            int width = aim == side ? SMASH_WIDE_X : aim == -side ? SMASH_NARROW_X : SMASH_SIDE_X;
            bn::fixed tx = side * width;
            int slow = high ? SMASH_SLOW_FRAMES : POWER_SLOW_FRAMES;
            int fast = high ? SMASH_FAST_FRAMES : POWER_FAST_FRAMES;
            int frames = slow - (slow - fast) * pct / 100 - (perfect ? PERFECT_FRAMES_BONUS : 0);
            int power = (high ? SMASH_POWER : POWER_DRIVE_POWER) + CHARGE_POWER * pct / 100 + (perfect ? PERFECT_POWER : 0);
            bn::fixed hit_x = ball.x, hit_y = ball_screen_y();

            me->start_smash(hit_x, hit_y);
            ball.spin = 0;
            plan_shot(ball, tx, SMASH_DEPTH, frames);
            stop_charging();
            last_perfect = perfect;
            after_hit(PLAYER, high ? SHOT_SMASH : SHOT_POWER, power);

            // The ball waits where it is until the paddle gets there (see tick), then the hit happens:
            // trail, sound, and for a PERFECT one the hit-stop, flash, shake and star.
            ball_hold = SMASH_CONTACT_FRAMES;
            pending_perfect = perfect;

            if(perfect)
            {
                show_message(high ? "PERFECT SMASH!" : "PERFECT SHOT!", sb::text_color::YELLOW, 50);
            }
            else
            {
                show_message(high ? "Smash!" : "Power shot!", sb::text_color::YELLOW, 36);

                // Why it wasn't PERFECT: a small hint over your head.
                const char* why = dz < -level->sweet_front ? "Early" : dz > level->sweet_back ? "Late" :
                                  "More power";
                show_timing_hint(why);
            }
        }

        // The paddle reached the ball: off it goes.
        void smash_contact()
        {
            start_trail(ball.x, ball_screen_y(), 40);

            if(pending_perfect)
            {
                freeze = PERFECT_FREEZE_FRAMES;
                shake = PERFECT_SHAKE_FRAMES;
                flash = PERFECT_FLASH;
                start_burst(ball.x, ball_screen_y());
                start_whoosh();
                sb::sfx(bn::sound_items::sfx_pp_perfect);
            }
            else
            {
                sb::sfx(bn::sound_items::sfx_smash);
            }
        }

        // A small note over your head after a smash that wasn't PERFECT ("Early", "Late", "More power").
        void show_timing_hint(const char* text)
        {
            timing_label.set(me->x.round_integer(), me->center_y() - 26, text, sb::text_color::WHITE,
                             sb::align::CENTER);
            timing_timer = TIMING_HINT_FRAMES;
        }

        void start_trail(bn::fixed x, bn::fixed y, int frames)
        {
            trail_timer = frames;

            for(int i = 0; i < TRAIL_LENGTH * 3; ++i)
            {
                history_x[i] = x;
                history_y[i] = y;
            }
        }

        void start_burst(bn::fixed x, bn::fixed y)
        {
            burst_timer = 14;
            burst.set_position(x.round_integer(), y.round_integer());
        }

        // The whoosh sound of a PERFECT swing.
        void start_whoosh()
        {
            sb::sfx(bn::sound_items::sfx_whoosh);
        }

        void player_serve(int quality)
        {
            int aim = aim_input();
            bool fast = toss_kind == SHOT_SERVE_FAST;
            bn::fixed tx = aim ? bn::fixed(aim * SERVE_AIM_X) : bn::fixed(sb::rand_range(-18, 18));
            me->start_serve_swing();
            bn::fixed hit_x = ball.x, hit_y = ball_screen_y();

            // First the ball goes down onto your own side ...
            ball.spin = 0;
            plan_shot(ball, ball.x + (tx - ball.x) / 4, SERVE_OWN_BOUNCE_Z, SERVE_FIRST_FRAMES);

            // ... then (after that bounce) on to here.
            serve_bounce_pending = true;
            serve_tx = tx;
            serve_tz = fast ? SERVE_FAST_DEPTH : SERVE_SPIN_DEPTH;
            serve_frames = fast ? SERVE_FAST_FRAMES[quality] : SERVE_SPIN_FRAMES[quality];
            int dir = aim ? aim : (sb::rand_range(0, 1) ? 1 : -1);
            serve_spin = fast ? bn::fixed(0) : SERVE_SPIN_AMOUNT[quality] * dir;
            int power = fast ? SERVE_FAST_POWER[quality] : SERVE_SPIN_POWER[quality];
            after_hit(PLAYER, fast ? SHOT_SERVE_FAST : SHOT_SERVE_SPIN, power);

            if(quality == SERVE_PERFECT)
            {
                // Like a PERFECT smash: hit-stop, flash, shake, a ghost trail behind the ball (again after its
                // first bounce, see update_ball), a star and a whoosh.
                freeze = PERFECT_FREEZE_FRAMES;
                flash = PERFECT_FLASH;
                shake = PERFECT_SHAKE_FRAMES;
                start_trail(hit_x, hit_y, 30);
                start_burst(hit_x, hit_y);
                start_whoosh();
                sb::sfx(bn::sound_items::sfx_pp_perfect);
                show_message(fast ? "Perfect serve!" : "Perfect spin!", sb::text_color::YELLOW, 40);
            }
            else if(quality == SERVE_GOOD)
            {
                sb::sfx(bn::sound_items::sfx_paddle);
                show_message("Strong serve!", sb::text_color::WHITE, 30);
            }
            else
            {
                sb::sfx(bn::sound_items::sfx_paddle);

                if(quality == SERVE_WEAK)
                {
                    show_message("Weak serve...", sb::text_color::WHITE, 30);
                }
            }
        }

        void cpu_serve()
        {
            bool spin = sb::rand_range(0, 99) < level->spin_serve_percent;
            // From a corner, usually across the table (and sometimes straight down the line).
            int across = cpu->x > 0 ? -1 : 1;
            bn::fixed tx = sb::rand_range(0, 3) ? bn::fixed(across * sb::rand_range(10, 30)) :
                                                  bn::fixed(-across * sb::rand_range(14, 28));
            cpu->start_serve_swing();
            ball.spin = 0;
            plan_shot(ball, ball.x + (tx - ball.x) / 4, -SERVE_OWN_BOUNCE_Z, SERVE_FIRST_FRAMES);
            serve_bounce_pending = true;
            serve_tx = tx;
            serve_tz = sb::rand_range(16, 28);
            serve_frames = level->serve_frames + (spin ? 6 : 0);
            serve_spin = spin ? SERVE_SPIN_AMOUNT[1] * (sb::rand_range(0, 1) ? 1 : -1) : bn::fixed(0);
            after_hit(CPU, SHOT_CPU_SERVE, 0);
            sb::sfx(bn::sound_items::sfx_paddle);
        }

        // A spot on your side, away from where you stand when `away` is set.
        bn::fixed cpu_aim_x(bool away, int min_off, int max_off) const
        {
            if(away)
            {
                int dir = me->x < 0 ? 1 : -1;
                return dir * sb::rand_range(min_off, max_off);
            }

            return sb::rand_range(-max_off, max_off);
        }

        void cpu_hit()
        {
            int speedup = bn::min(rally_hits / RALLY_HITS_PER_SPEEDUP, RALLY_SPEEDUP_MAX);
            cpu->start_topspin(ball.x, ball_screen_y(), cpu->windup != 0 ? cpu->windup : ball.x >= cpu->x ? 1 : -1);
            cpu->windup = 0;
            ball.spin = 0;
            shot_kind kind = SHOT_CPU_NORMAL;
            // Its shot goes to the side it showed: right of it for a forehand, left for a backhand.
            bn::fixed tx = clamp(cpu->x + cpu_shot_offset, -(TABLE_HALF_W - 4), TABLE_HALF_W - 4);

            switch(plan)
            {
            case PLAN_ATTACK:
                kind = SHOT_CPU_ATTACK;
                plan_shot(ball, tx, sb::rand_range(22, 32), level->attack_frames - speedup);
                break;
            case PLAN_NORMAL:
                plan_shot(ball, tx, sb::rand_range(14, 30), level->normal_frames - speedup);
                break;
            case PLAN_WEAK:
                // A slow, high floater: short, so it bounces up high. Smash it!
                kind = SHOT_CPU_WEAK;
                plan_shot(ball, tx, sb::rand_range(8, 18), CPU_WEAK_FRAMES);
                break;
            default:
            {
                kind = SHOT_CPU_ERROR;
                int which = sb::rand_range(0, 2);

                if(which == 0)
                {
                    // Into the net: a flat ball that reaches the net low.
                    int t = 22;
                    ball.vx = bn::fixed(sb::rand_range(-10, 10)) / t;
                    ball.vz = -ball.z / t;
                    ball.vh = (GRAVITY * (t * (t + 1) / 2) + NET_HEIGHT / 2 - ball.h) / t;
                }
                else if(which == 1)
                {
                    // Long: sails past the end of the table.
                    plan_shot(ball, tx / 2, TABLE_NEAR_Z + sb::rand_range(12, 20), level->normal_frames);
                }
                else
                {
                    // Wide: lands off the side of the table (the side it turned to).
                    int dir = cpu_shot_side;
                    plan_shot(ball, dir * (TABLE_HALF_W + sb::rand_range(10, 18)), sb::rand_range(10, 26),
                              level->normal_frames);
                }
                break;
            }
            }

            if(kind == SHOT_CPU_WEAK)
            {
                show_message(last_perfect ? "Blocked! Smash again!" : "A floater! Smash it!", sb::text_color::WHITE, 30);
            }

            after_hit(CPU, kind, 0);
            sb::sfx(kind == SHOT_CPU_ATTACK ? bn::sound_items::sfx_smash : bn::sound_items::sfx_paddle);
        }

        void after_hit(int who, shot_kind kind, int power)
        {
            hitter = who;
            bounces = 0;
            whiffed = false;
            last_shot = kind;
            ++rally_hits;
            a_age = b_age = 1000;
            st = state::RALLY;
            hint_label.clear();

            if(who == PLAYER)
            {
                incoming_power = power;

                if(training && drill != DRILL_SERVE)
                {
                    pick_robot_target();
                }

                think_cpu(kind, power);
            }
            else
            {
                // Walk back toward the middle, but after a hard shot it needs a moment first.
                trail_timer = 0;
                cpu_target_x = cpu->x / 3;
                cpu_target_z = CPU_HOME_Z;
                cpu_reaction = bn::max(0, incoming_power - RECOVER_FROM_POWER) * RECOVER_PERCENT / 100;
                incoming_power = 0;
            }
        }

        // The CPU sees your shot, works out where it can hit it and how hard that will be,
        // and decides what to do with it (and where its own shot will go).
        void think_cpu(shot_kind kind, int power)
        {
            if(training)
            {
                return;     // the robot never hits back: the ball just flies into its net
            }

            ball_state b = ball;
            int bounce_count = 0;
            int home_frame = -1, any_frame = -1, last_frame = 0;
            ball_state home_pos = b, any_pos = b, last_pos = b;
            bool own_bounce_first = serve_bounce_pending;   // a serve bounces on your side first

            for(int f = 1; f < 240; ++f)
            {
                contact c = step_ball(b);

                if(c == C_NET || c == C_FLOOR)
                {
                    break;
                }

                if(c == C_TABLE)
                {
                    if(b.z > 0)
                    {
                        if(own_bounce_first)
                        {
                            break;      // planned again after that bounce: think_cpu runs again then
                        }

                        break;
                    }

                    if(++bounce_count >= 2)
                    {
                        break;
                    }
                }

                if(bounce_count == 1 && b.h >= HIT_MIN_H)
                {
                    last_frame = f;
                    last_pos = b;

                    if(any_frame < 0 && b.z <= CPU_Z_MAX + CPU_REACH_FRONT - 2)
                    {
                        any_frame = f;
                        any_pos = b;
                    }

                    if(b.z <= CPU_HOME_Z + CPU_REACH_FRONT - 2)
                    {
                        home_frame = f;
                        home_pos = b;
                        break;
                    }
                }
            }

            int frame;
            ball_state at;
            bn::fixed stand_z;

            if(home_frame >= 0)
            {
                frame = home_frame;
                at = home_pos;
                stand_z = CPU_HOME_Z;
            }
            else if(any_frame >= 0)
            {
                frame = any_frame;
                at = any_pos;
                stand_z = clamp(at.z - CPU_REACH_FRONT + 3, CPU_Z_MIN, CPU_Z_MAX);
            }
            else
            {
                // Unreachable (or going out, or a serve still on your side): run toward it anyway.
                frame = bn::max(last_frame, 1);
                at = last_pos;
                stand_z = CPU_Z_MAX;
            }

            // Stand with the ball on the closer side.
            bn::fixed off = at.x >= cpu->x ? -CPU_HAND_OFFSET : CPU_HAND_OFFSET;
            cpu_target_x = clamp(at.x + off, -CPU_X_LIMIT, CPU_X_LIMIT);
            cpu_target_z = stand_z;
            cpu_reaction = level->reaction;

            if(own_bounce_first)
            {
                return;     // decide properly once the serve has bounced on your side
            }

            bn::fixed dist = bn::abs(cpu_target_x - cpu->x) + bn::abs(cpu_target_z - cpu->z) / 2;
            dist = bn::max(dist - (CPU_REACH_X - CPU_HAND_OFFSET), bn::fixed(0));
            int avail = bn::max(frame - level->reaction, 1);
            bn::fixed run = dist / (level->speed * avail);
            int difficulty = power + (run * CPU_RUN_WEIGHT).round_integer() + sb::rand_range(-CPU_JITTER, CPU_JITTER);

            // Hard shots: the harder they are, the more likely a slow floater comes back.
            int weak_start = level->weak_from - level->weak_chance_range;
            int weak_chance = difficulty <= weak_start ? 0 :
                              bn::min(100, (difficulty - weak_start) * 100 / level->weak_chance_range);

            // A great serve is rewarded: the return is (very likely) a slow floater.
            if(kind == SHOT_SERVE_FAST || kind == SHOT_SERVE_SPIN)
            {
                if(serve_quality_of_last == SERVE_PERFECT)
                {
                    weak_chance = bn::max(weak_chance, level->perfect_serve_floater);
                }
                else if(serve_quality_of_last == SERVE_GOOD)
                {
                    weak_chance = bn::max(weak_chance, level->good_serve_floater);
                }
            }

            if(home_frame < 0 && any_frame < 0)
            {
                plan = PLAN_ERROR;
            }
            else if(difficulty >= level->miss_from)
            {
                plan = PLAN_ERROR;
            }
            else if(difficulty >= level->weak_from || sb::rand_range(0, 99) < weak_chance)
            {
                plan = PLAN_WEAK;
            }
            else if(difficulty >= level->miss_from - 12 && (kind == SHOT_SERVE_FAST || kind == SHOT_SERVE_SPIN))
            {
                plan = PLAN_WEAK;   // a serve close to an ace: it just gets it back
            }

            // A PERFECT smash usually wins the point, but it can be blocked back, especially when it goes
            // right at the CPU (it hardly has to move).
            if(last_perfect && plan == PLAN_ERROR && (home_frame >= 0 || any_frame >= 0))
            {
                int chance = level->perfect_return + (dist < 18 ? level->perfect_return_close : 0);

                if(sb::rand_range(0, 99) < chance)
                {
                    plan = PLAN_WEAK;
                }
            }
            else if(difficulty < level->attack_below || (kind == SHOT_LOB && at.h >= SMASH_MIN_HEIGHT))
            {
                plan = PLAN_ATTACK;
            }
            else
            {
                plan = PLAN_NORMAL;
            }

            // Where its shot will go (it turns that way before hitting, so you can read it).
            switch(plan)
            {
            case PLAN_ATTACK:
                cpu_shot_x = cpu_aim_x(true, 16, 34);
                break;
            case PLAN_NORMAL:
                cpu_shot_x = cpu_aim_x(sb::rand_range(0, 1) == 0, 6, 30);
                break;
            case PLAN_WEAK:
                cpu_shot_x = sb::rand_range(-14, 14);
                break;
            default:
                cpu_shot_x = sb::rand_range(0, 1) ? 20 : -20;
                break;
            }

            // Forehand or backhand? Seen on screen, a forehand sends the ball to the right of where it
            // stands, a backhand to the left. Near the edge of the table only one of them fits.
            cpu_shot_offset = cpu_shot_x - cpu_target_x;

            if(cpu_target_x > 26)
            {
                cpu_shot_offset = -bn::max(bn::abs(cpu_shot_offset), bn::fixed(12));
            }
            else if(cpu_target_x < -26)
            {
                cpu_shot_offset = bn::max(bn::abs(cpu_shot_offset), bn::fixed(12));
            }
            else if(bn::abs(cpu_shot_offset) < 10)
            {
                cpu_shot_offset = cpu_shot_offset >= 0 ? 10 : -10;
            }

            cpu_shot_side = cpu_shot_offset >= 0 ? 1 : -1;

            if(DEBUG_LOG)
            {
                BN_LOG("PP shot kind=", int(kind), " frame=", frame, " run=", run, " diff=", difficulty,
                       " plan=", int(plan));
            }
        }

        // ------------------------------------------------------------------------------- points
        void award(int winner, const bn::string_view& why)
        {
            if(point_decided)
            {
                return;
            }

            if(training)
            {
                training_rep(winner == PLAYER, why);
                return;
            }

            point_decided = true;
            last_winner = winner;
            st = state::POINT;
            timer = 0;
            stop_charging();
            cpu->windup = 0;
            serve_bounce_pending = false;

            if(winner == PLAYER)
            {
                ++my_points;
                sb::sfx(bn::sound_items::sfx_point);
                show_message(why, sb::text_color::YELLOW, POINT_PAUSE_FRAMES);
            }
            else
            {
                ++cpu_points;
                sb::sfx(bn::sound_items::sfx_miss);
                show_message(why, sb::text_color::RED, POINT_PAUSE_FRAMES);
            }

            update_score_label();
            cheer_timer = CHEER_FRAMES;

            if(DEBUG_LOG)
            {
                BN_LOG("PP point winner=", winner, " score=", my_points, "-", cpu_points, " rally=", rally_hits,
                       " why=", why);
            }
        }

        void receiver_failed()
        {
            int receiver = 1 - hitter;

            if(receiver == CPU)
            {
                bool ace = last_shot == SHOT_SERVE_FAST || last_shot == SHOT_SERVE_SPIN;
                award(PLAYER, last_shot == SHOT_SMASH ? "Smash winner!" : ace ? "Ace!" : "Nice shot!");
            }
            else
            {
                award(CPU, "Missed it!");
            }
        }

        void hitter_fault(const bn::string_view& why)
        {
            award(1 - hitter, why);
        }

        // ------------------------------------------------------------------------------- frame
        bn::fixed ball_screen_y() const
        {
            return ball.z + NET_SCREEN_Y - ball.h;
        }

        void update_ball()
        {
            if(point_decided && (bn::abs(ball.x) > 160 || ball.z > 110 || ball.z < -110))
            {
                return;
            }

            bool at_back_net = ball.z < TABLE_FAR_Z - CATCH_BEYOND;
            bool at_side_net = ball.z < NET_SIDE_FROM_Z && bn::abs(ball.x) > NET_SIDE_X;

            if(training && ! caught && hitter == PLAYER && ! serve_bounce_pending && (at_back_net || at_side_net))
            {
                // Into the robot's net: in if it bounced on the robot's side first, long if it didn't.
                catch_ball();

                if(! point_decided)
                {
                    if(bounces >= 1)
                    {
                        receiver_failed();
                    }
                    else
                    {
                        hitter_fault("Long!");
                    }
                }
            }

            contact c = step_ball(ball);

            if(caught && ball.h <= NET_REST_H)
            {
                ball.h = NET_REST_H;    // dropped into the tray
                ball.vh = 0;
            }

            if(point_decided)
            {
                if(c == C_FLOOR || c == C_TABLE)
                {
                    if(ball.vh > bn::fixed(0.6))
                    {
                        sb::sfx(bn::sound_items::sfx_bounce);
                    }
                }

                return;
            }

            if(c == C_NET)
            {
                sb::sfx(bn::sound_items::sfx_bump);
                hitter_fault("Net!");
                return;
            }

            if(c == C_TABLE)
            {
                sb::sfx(bn::sound_items::sfx_bounce);
                int side_hit = ball.z > 0 ? PLAYER : CPU;
                trail_timer = 0;

                if(serve_bounce_pending)
                {
                    serve_bounce_pending = false;

                    if(side_hit != hitter)
                    {
                        hitter_fault("Bad serve!");
                        return;
                    }

                    // A good serve: on over the net now.
                    ball.spin = serve_spin;
                    ball.dip = hitter == PLAYER && last_shot == SHOT_SERVE_FAST && serve_quality_of_last == SERVE_PERFECT ?
                               SERVE_TOPSPIN_DIP : bn::fixed(0);
                    plan_shot(ball, serve_tx, serve_tz, serve_frames);

                    if(hitter == PLAYER && serve_quality_of_last == SERVE_PERFECT)
                    {
                        start_trail(ball.x, ball_screen_y(), 40);
                    }

                    if(hitter == PLAYER)
                    {
                        think_cpu(last_shot, last_shot == SHOT_SERVE_SPIN ?
                                  SERVE_SPIN_POWER[serve_quality_of_last] : SERVE_FAST_POWER[serve_quality_of_last]);
                    }

                    return;
                }

                if(side_hit == hitter)
                {
                    hitter_fault("Net!");
                }
                else if(++bounces >= 2)
                {
                    receiver_failed();
                }

                return;
            }

            if(c == C_FLOOR)
            {
                sb::sfx(bn::sound_items::sfx_bounce);

                if(bounces == 0)
                {
                    hitter_fault("Out!");
                }
                else
                {
                    receiver_failed();
                }
            }
        }

        void check_hits()
        {
            if(point_decided || bounces != 1)
            {
                return;
            }

            if(hitter == CPU)
            {
                if(charging)
                {
                    // You hit by letting go of the button. Held too long, and the ball is about to pass you:
                    // you just block it back, weakly.
                    bn::fixed cdx, cdz;

                    if(charged_reach(cdx, cdz) && cdz >= PLAYER_REACH_BACK - 2)
                    {
                        weak_block();
                    }

                    return;
                }

                bn::fixed dx = ball.x - me->x;
                bn::fixed dz = ball.z - me->z;

                if(ball.h >= HIT_MIN_H && dz >= -PLAYER_REACH_FRONT && dz <= PLAYER_REACH_BACK &&
                   bn::abs(dx) <= PLAYER_REACH_X)
                {
                    player_hit();
                }
                else if(! whiffed && dz > PLAYER_REACH_BACK && bn::abs(dx) <= PLAYER_REACH_X + WHIFF_MARGIN)
                {
                    whiffed = true;
                    me->start_swing(ball.x, ball_screen_y());
                    sb::sfx(bn::sound_items::sfx_whoosh);
                }
            }
            else if(! training)
            {
                bn::fixed dx = ball.x - cpu->x;
                bn::fixed dz = ball.z - cpu->z;

                if(ball.h >= HIT_MIN_H && dz <= CPU_REACH_FRONT && dz >= -CPU_REACH_BACK &&
                   bn::abs(dx) <= CPU_REACH_X)
                {
                    cpu_hit();
                }
                else if(! whiffed && dz < -CPU_REACH_BACK && bn::abs(dx) <= CPU_REACH_X + WHIFF_MARGIN)
                {
                    whiffed = true;
                    cpu->windup = 0;
                    cpu->start_swing(ball.x, ball_screen_y());
                    sb::sfx(bn::sound_items::sfx_whoosh);
                }
            }
        }

        // ---- the serve bar (hold A or B; the marker climbs; let go on the white line)
        void hide_serve_bar()
        {
            serve_aiming = false;

            if(me)
            {
                me->serve_stance = false;
            }

            serve_bar.set_visible(false);
            serve_bar_top.set_visible(false);
            serve_zone.set_visible(false);
            serve_marker.set_visible(false);
        }

        void update_serve_bar()
        {
            if(serve_wait_release)
            {
                if(! bn::keypad::a_held() && ! bn::keypad::b_held())
                {
                    serve_wait_release = false;
                }

                return;
            }

            if(! serve_aiming)
            {
                // On the right half of the table you face left and serve the other way.
                me->serve_dir = me->x > 0 ? -1 : 1;
                bool a = bn::keypad::a_pressed();
                bool b = bn::keypad::b_pressed();

                if(! a && ! b)
                {
                    return;
                }

                serve_aiming = true;
                serve_button = a ? 1 : 2;
                toss_kind = a ? SHOT_SERVE_FAST : SHOT_SERVE_SPIN;
                serve_mark = 0;
                serve_zone_pos = sb::rand_range(SERVE_TARGET_MIN, SERVE_TARGET_MAX);
                me->serve_stance = true;
                hint_label.set(-116, 70, "Let go in the red box!", sb::text_color::WHITE);
            }

            // The marker only climbs. At the top the serve is cancelled.
            serve_mark += level->serve_speed;

            if(serve_mark >= SERVE_TRACK)
            {
                hide_serve_bar();
                serve_wait_release = true;
                sb::sfx(bn::sound_items::sfx_bump);
                show_message("Too slow! Try again", sb::text_color::WHITE, 50);
                hint_label.set(-116, 70, "Hold A: topspin  B: backspin", sb::text_color::WHITE);
                return;
            }

            // Next to you, on your left.
            int bx = (me->x - 20 * me->serve_dir).round_integer();    // on the side away from the swing
            int bottom = me->center_y() + 14;          // screen y of the bar's bottom end
            serve_bar.set_position(bx, bottom - 16);
            serve_bar_top.set_position(bx, bottom - 48);
            serve_zone.set_tiles(bn::sprite_items::pingpong_vzone.tiles_item(), g_level_choice);
            serve_zone.set_position(bx, bottom - 2 - serve_zone_pos);
            serve_marker.set_position(bx, (bottom - 2 - serve_mark).round_integer());
            serve_bar.set_visible(true);
            serve_bar_top.set_visible(true);
            serve_zone.set_visible(true);
            serve_marker.set_visible(true);

            // The ball is tossed up while the marker climbs, and falls back to paddle height exactly when the
            // marker reaches the middle of the red box (then keeps falling if you wait).
            bn::fixed f = serve_mark / serve_zone_pos;
            ball.x = me->x + 6 * me->serve_dir;
            ball.z = me->z - 3;
            ball.h = bn::max(bn::fixed(SERVE_PADDLE_HEIGHT) + SERVE_TOSS_HEIGHT * 4 * f * (1 - f), bn::fixed(-8));
            ball.vh = 0;
            me->serve_stance = true;

            bool held = serve_button == 1 ? bn::keypad::a_held() : bn::keypad::b_held();

            if(! held)
            {
                // Let go: how close was the marker to the line?
                int miss = bn::abs((serve_mark - serve_zone_pos).round_integer());
                serve_quality_of_last = miss <= level->serve_perfect ? SERVE_PERFECT :
                                        miss <= level->serve_strong ? SERVE_GOOD :
                                        miss <= level->serve_ok ? SERVE_OK : SERVE_WEAK;

                if(serve_quality_of_last != SERVE_PERFECT)
                {
                    // Let go below the red box = early, above it = late.
                    show_timing_hint(serve_mark < serve_zone_pos ? "Early" : "Late");
                }
                hide_serve_bar();
                hint_label.clear();
                player_serve(serve_quality_of_last);    // hit the ball right where it is in the toss
            }
        }

        void update_charge()
        {
            a_hold = bn::keypad::a_held() ? a_hold + 1 : 0;
            b_hold = bn::keypad::b_held() ? b_hold + 1 : 0;

            // (In Follow / Random you can wind up any time, even before the robot's next ball is out.)
            bool can_charge = ((st == state::RALLY && ! point_decided) || rally_drill()) && ! me->cheering;

            if(! charging)
            {
                if(can_charge && (a_hold >= CHARGE_START_FRAMES || b_hold >= CHARGE_START_FRAMES))
                {
                    charging = true;
                    charge_button = a_hold >= CHARGE_START_FRAMES ? 1 : 2;
                    charge = a_hold >= CHARGE_START_FRAMES ? a_hold - CHARGE_START_FRAMES : b_hold - CHARGE_START_FRAMES;
                    me->charge_side = charge_button == 1 ? 1 : -1;
                    me->charge_pct = charge * 100 / level->charge_frames;
                    a_age = b_age = 1000;   // not a tap
                }

                return;
            }

            if(! can_charge)
            {
                stop_charging();
                return;
            }

            bool held = charge_button == 1 ? bn::keypad::a_held() : bn::keypad::b_held();

            if(held)
            {
                if(charge < level->charge_frames && charge + 1 >= level->charge_frames)
                {
                    sb::sfx(bn::sound_items::sfx_pp_charged);
                }

                charge = bn::min(charge + 1, level->charge_frames);
                me->charge_full = charge >= level->charge_frames;
                me->charge_pct = charge * 100 / level->charge_frames;
            }
            else
            {
                release_charge();
            }
        }

        bool move_player()
        {
            bn::fixed old_x = me->x, old_z = me->z;
            bn::fixed factor = charging ? CHARGE_MOVE_FACTOR : bn::fixed(1);

            if(! me->cheering)
            {
                if(bn::keypad::left_held())
                {
                    me->x -= PLAYER_SPEED * factor;
                }
                else if(bn::keypad::right_held())
                {
                    me->x += PLAYER_SPEED * factor;
                }

                // During your own serve you stay at the serving line.
                if(! (server == PLAYER && (st == state::SERVE || st == state::TOSS)))
                {
                    if(bn::keypad::up_held())
                    {
                        me->z -= PLAYER_SPEED_Z * factor;
                    }
                    else if(bn::keypad::down_held())
                    {
                        me->z += PLAYER_SPEED_Z * factor;
                    }
                }
            }

            me->x = clamp(me->x, -PLAYER_X_LIMIT, PLAYER_X_LIMIT);
            me->z = clamp(me->z, PLAYER_Z_MIN, PLAYER_Z_MAX);

            if(st == state::TOSS && server == PLAYER)
            {
                // Keep the tossed ball with you.
                ball.x += me->x - old_x;
            }

            return me->x != old_x || me->z != old_z;
        }

        bool move_cpu()
        {
            bn::fixed old_x = cpu->x, old_z = cpu->z;

            if(cpu_reaction > 0)
            {
                --cpu_reaction;
            }
            else if(! cpu->cheering)
            {
                cpu->x = approach(cpu->x, cpu_target_x, level->speed);
                cpu->z = approach(cpu->z, cpu_target_z, level->speed);
            }

            cpu->x = clamp(cpu->x, -CPU_X_LIMIT, CPU_X_LIMIT);
            cpu->z = clamp(cpu->z, CPU_Z_MIN, CPU_Z_MAX);
            return cpu->x != old_x || cpu->z != old_z;
        }

        // The CPU turns to the side it will hit to, a moment before it hits.
        void update_cpu_tell()
        {
            cpu->windup = 0;

            if(st != state::RALLY || point_decided || hitter != PLAYER || serve_bounce_pending || ball.vz >= 0)
            {
                return;
            }

            bn::fixed frames_left = (ball.z - cpu->z) / -ball.vz;

            if(ball.z < 0 && frames_left <= level->tell_frames)
            {
                cpu->windup = cpu_shot_side;
            }
        }

        // One frame of play. Returns false when the match is over.
        bool tick()
        {
            ++a_age;
            ++b_age;

            if(bn::keypad::a_pressed())
            {
                a_age = 0;
            }

            if(bn::keypad::b_pressed())
            {
                b_age = 0;
            }

            if(message_timer > 0 && --message_timer == 0)
            {
                message_label.clear();
            }

            if(timing_timer > 0 && --timing_timer == 0)
            {
                timing_label.clear();
            }

            update_charge();
            bool me_moving = move_player();
            bool cpu_moving = move_cpu();
            ++timer;

            switch(st)
            {
            case state::SERVE:
                if(training && server == CPU)
                {
                    update_robot();
                    break;
                }

                hold_ball_at_server();
                // A gentle bounce in the hand.
                ball.h = 2 + ((timer / 8) % 2);

                if(server == PLAYER)
                {
                    update_serve_bar();
                }
                else if(cpu->x == cpu_target_x)
                {
                    // In its corner: it gets into its serving stance, then tosses.
                    cpu->serve_stance = timer >= CPU_SERVE_DELAY - 30;

                    if(timer >= CPU_SERVE_DELAY)
                    {
                        ball.vh = CPU_TOSS_SPEED;
                        st = state::TOSS;
                        timer = 0;
                        sb::sfx(bn::sound_items::sfx_jump);
                    }
                }
                break;

            case state::TOSS:
                // The CPU's toss: up, and hit as it falls back to paddle height.
                ball.vh -= GRAVITY;
                ball.h += ball.vh;

                if(ball.vh < 0 && ball.h <= SERVE_PADDLE_HEIGHT)
                {
                    if(server == PLAYER)
                    {
                        player_serve(serve_quality_of_last);
                    }
                    else
                    {
                        cpu_serve();
                    }
                }
                break;

            case state::RALLY:
                if(ball_hold > 0)
                {
                    if(--ball_hold == 0)
                    {
                        smash_contact();
                    }

                    break;
                }

                update_ball();
                check_hits();
                break;

            case state::POINT:
                update_ball();

                if(training)
                {
                    if(timer >= rally_gap)
                    {
                        start_serve();
                    }

                    break;
                }

                if(timer >= POINT_PAUSE_FRAMES)
                {
                    if(match_over())
                    {
                        st = state::DONE;
                        timer = 0;
                        bool won = my_points > cpu_points;
                        me->cheering = won;
                        cpu->cheering = ! won;
                        bn::string<32> text;

                        if(won)
                        {
                            text = "You win!";
                            sb::sfx(bn::sound_items::sfx_record);
                        }
                        else
                        {
                            text = sb::character_name(opponent);
                            text.append(" wins!");
                        }

                        show_message(text, won ? sb::text_color::YELLOW : sb::text_color::WHITE, MATCH_END_FRAMES);
                    }
                    else
                    {
                        start_serve();
                    }
                }
                break;

            case state::DONE:
            default:
                if(timer >= MATCH_END_FRAMES)
                {
                    return false;
                }
                break;
            }

            if(st == state::POINT || st == state::DONE)
            {
                // Walk back toward the middle while the next point gets ready.
                cpu_target_x = approach(cpu_target_x, 0, 1);
                cpu_target_z = CPU_HOME_Z;
            }

            update_cpu_tell();

            if(training && drill != DRILL_SERVE)
            {
                turn_robot_head();
            }

            draw_actors(me_moving, cpu_moving);
            draw_ball();
            animate_scenery();
            return true;
        }

        void draw_actors(bool me_moving, bool cpu_moving)
        {
            me->draw(me_moving);
            cpu->draw(cpu_moving);
            cpu->sprite.set_visible(! in_basement);
            cpu->paddle.set_visible(! in_basement);

            if(in_basement)
            {
                robot.set_tiles(bn::sprite_items::pingpong_robot.tiles_item(), robot_frame());
            }

            // The power meter stands beside you while you charge, on the paddle's side (right for a forehand,
            // left for a backhand), so it never hides the ball.
            meter.set_visible(charging);

            if(charging)
            {
                int f = bn::min(charge * 8 / level->charge_frames, 8);
                meter.set_tiles(bn::sprite_items::pingpong_vmeter.tiles_item(), f);
                meter.set_position((me->x + me->charge_side * 21).round_integer(), me->center_y() - 2);
            }
        }

        void draw()
        {
            draw_actors(false, false);
            draw_ball();
            animate_scenery();
        }

        void draw_ball()
        {
            // (In Follow / Random the ball stays in the net until the robot shoots the next one.)
            bool fades = st == state::POINT && (! training || drill == DRILL_SERVE);
            int pause = training ? TRAIN_PAUSE_FRAMES : POINT_PAUSE_FRAMES;
            bool hidden = st == state::DONE || (fades && timer > pause - 10) ||
                          (training && server == CPU && st == state::SERVE);   // still inside the robot
            ball_sprite.set_visible(! hidden);
            shadow_sprite.set_visible(! hidden && ! caught);

            // Ghost balls behind a smash.
            for(int i = TRAIL_LENGTH * 3 - 1; i > 0; --i)
            {
                history_x[i] = history_x[i - 1];
                history_y[i] = history_y[i - 1];
            }

            history_x[0] = ball.x;
            history_y[0] = ball_screen_y();

            if(trail_timer > 0)
            {
                --trail_timer;
            }

            for(int i = 0; i < TRAIL_LENGTH; ++i)
            {
                bool show = trail_timer > 0 && ! hidden;
                trail[i].set_visible(show);

                if(show)
                {
                    int k = (i + 1) * 3 - 1;    // 2, 5, 8: (i + 1) * 3 read past the end, the sideways ghost
                    trail[i].set_position(history_x[k].round_integer(), history_y[k].round_integer());
                }
            }

            if(hidden)
            {
                return;
            }

            int frame = ball.h > 24 ? 2 : ball.h < -4 ? 0 : 1;
            ball_sprite.set_tiles(ball_item->tiles_item(), frame);
            ball_sprite.set_position(ball.x.round_integer(), ball_screen_y().round_integer());

            // Hidden behind the table's far edge, or behind the CPU?
            bool behind_table = ball.z < TABLE_FAR_Z && ball.h < 0 && ! caught;   // (caught: in front of the net)
            bool behind_cpu = ball.z < cpu->z;

            if(behind_table || behind_cpu)
            {
                ball_sprite.set_bg_priority(3);
                ball_sprite.set_z_order(1);
            }
            else
            {
                ball_sprite.set_bg_priority(1);
                ball_sprite.set_z_order(ball.z > me->z ? -1 : 1);
            }

            // Shadow on the table or on the floor.
            bool on_table = over_table(ball.x, ball.z) && ball.h >= 0;
            bn::fixed shadow_y = ball.z + NET_SCREEN_Y + (on_table ? bn::fixed(0) : TABLE_HEIGHT);
            shadow_sprite.set_position(ball.x.round_integer(), shadow_y.round_integer());
            bn::fixed above = ball.h - (on_table ? bn::fixed(0) : -TABLE_HEIGHT);
            shadow_sprite.set_tiles(bn::sprite_items::pingpong_shadow.tiles_item(), above > 20 ? 1 : 0);

            if(! on_table && ball.z < TABLE_FAR_Z + 2)
            {
                shadow_sprite.set_bg_priority(3);
                shadow_sprite.set_z_order(2);
            }
            else
            {
                shadow_sprite.set_bg_priority(2);
                shadow_sprite.set_z_order(2);
            }
        }

        // The spectators watch, and jump up and down after every point.
        void animate_crowd()
        {
            if(cheer_timer > 0)
            {
                --cheer_timer;
            }

            for(int i = 0; i < crowd.size(); ++i)
            {
                spectator& sp = crowd[i];
                const int* at = SPECTATOR_SPOTS[sp.spot];
                bool cheering_now = cheer_timer > 0;
                int hop = cheering_now && ((cheer_timer / 6 + i) % 2 == 0) ? 3 : 0;
                sp.sprite.set_tiles(crowd_sprite_tiles(i), cheering_now ? CROWD_CHEER : CROWD_SIDE);
                sp.sprite.set_y(at[1] - hop);
            }
        }

        const bn::sprite_tiles_item& crowd_sprite_tiles(int index) const
        {
            int c = 0, n = 0;

            for(; c < sb::character_count; ++c)
            {
                if(c == sb::player_character() || c == opponent)
                {
                    continue;
                }

                if(n == index)
                {
                    break;
                }

                ++n;
            }

            return crowd_sprite(c).tiles_item();
        }

        // Screen shake, white flash and the burst star after a PERFECT shot.
        void update_effects()
        {
            animate_crowd();

            int dx = 0, dy = 0;

            if(shake > 0)
            {
                --shake;
                int k = shake > 7 ? 2 : 1;
                dx = (shake % 4 < 2) ? k : -k;
                dy = (shake % 3 == 0) ? k : -k;
            }

            hall.set_position(8 + dx, 48 + dy);
            table.set_position(8 + dx, 48 + dy);
            robot.set_position(ROBOT_HEAD_X + dx, ROBOT_HEAD_Y + dy);

            if(flash > 0)
            {
                set_white_flash(flash);
                flash -= bn::fixed(0.05);

                if(flash <= 0)
                {
                    flash = 0;
                    set_white_flash(0);
                }
            }

            burst.set_visible(burst_timer > 0);

            if(burst_timer > 0)
            {
                burst.set_tiles(bn::sprite_items::pingpong_burst.tiles_item(), burst_timer > 7 ? 0 : 1);
                --burst_timer;
            }
        }

        void clear_effects()
        {
            shake = 0;
            flash = 0;
            freeze = 0;
            burst_timer = 0;
            trail_timer = 0;
            set_white_flash(0);
            update_effects();
        }

        void animate_scenery()
        {
            int t = sb::frame_count();

            for(int i = 0; i < flakes.size(); ++i)
            {
                bn::sprite_ptr& f = flakes[i];

                if((t + i * 3) % (3 + i % 3) == 0)
                {
                    bn::fixed y = f.y() + 1;
                    bn::fixed x = f.x() + (((t / 20) + i) % 4 == 0 ? 1 : 0);

                    if(y > -54)
                    {
                        y = -76;
                        x = -103 + ((t * 7 + i * 13) % 40);
                    }

                    if(x > -63)
                    {
                        x = -103;
                    }

                    f.set_position(x, y);
                }
            }

            for(int i = 0; i < lights.size(); ++i)
            {
                bool on = ((t / 24) + i * 3) % 5 != 0;
                lights[i].set_tiles(bn::sprite_items::pingpong_light.tiles_item(), on ? 0 : 1);
            }
        }

        void set_paused_visibility(bool visible)
        {
            message_label.set_visible(visible);
            hint_label.set_visible(visible);
            score_label.set_visible(visible);
            timing_label.set_visible(visible);
            my_score_label.set_visible(visible);
            cpu_score_label.set_visible(visible);
        }

        // Plays one match. Returns false if the player quit from the pause menu.
        bool play()
        {
            if(! training)
            {
                update_score_label();
            }

            start_serve();
            robot_wait = ROBOT_FIRST_WAIT;

            while(true)
            {
                // Update first, so the button that closed the intro / result box isn't seen as a serve.
                sb::update();

                if(bn::keypad::start_pressed() && st != state::DONE)
                {
                    clear_effects();
                    set_paused_visibility(false);
                    bool quit;

                    if(training)
                    {
                        // Training never ends: the pause menu is the way out (or to another drill).
                        int choice = training_pause();
                        back_to_menu = choice == 1;
                        quit = choice != 0;
                    }
                    else
                    {
                        quit = sb::game_pause();
                    }

                    set_paused_visibility(true);

                    if(quit)
                    {
                        return false;
                    }

                    // Don't let the button that closed the menu count as a shot.
                    a_age = b_age = 1000;
                    a_hold = b_hold = 0;
                    stop_charging();
                }

                if(freeze > 0)
                {
                    // Hit-stop: everything holds still for a moment on a PERFECT shot.
                    --freeze;
                    burst.set_visible(true);
                    continue;
                }

                bool going = tick();
                update_effects();

                if(! going)
                {
                    clear_effects();
                    return true;
                }
            }
        }
    };

    // The one-screen menu before playing: Match or Training, the drill (training only) and the level.
    // Up/Down picks a row, Left/Right changes it, A starts, B goes back to the map. The room behind the
    // box follows the Play row (the hall for a match, the basement for training). Remembers the last choice.
    bool choose_settings(match& g, bool start_on_level)
    {
        constexpr bn::string_view plays[] = { "Match", "Training" };
        constexpr bn::string_view drills[] = { "Serve", "Follow", "Random" };
        constexpr bn::string_view levels[] = { "Easy", "Medium", "Hard" };
        constexpr bn::string_view names[] = { "Play", "Drill", "Level" };
        constexpr const char* match_hint = "Play an opponent.";
        constexpr const char* drill_hints[] = { "Practise your serve.", "The robot aims at you.", "Watch where it aims!" };
        constexpr int box_x = 2, box_w = 26;
        constexpr int option_x = (box_x + 7) * 8 - 120;  // where the choices start (Butano x)

        int values[3] = { g_training ? 1 : 0, g_drill_choice, g_level_choice };
        constexpr int counts[3] = { 2, 3, 3 };
        int row = start_on_level ? (g_training ? 2 : 1) : 0;    // index into the visible rows
        bool redraw = true;
        sb::label row_names[3], options[3][3], cursor, hint;

        while(true)
        {
            bool training = values[0] == 1;
            int kinds[3] = { 0, 1, 2 };     // Play, Drill, Level (no Drill row for a match)
            int rows = 3;

            if(! training)
            {
                kinds[1] = 2;
                rows = 2;
            }

            if(redraw)
            {
                redraw = false;
                g.set_room(training);

                for(sb::label& l : row_names)
                {
                    l.clear();
                }

                for(auto& list : options)
                {
                    for(sb::label& l : list)
                    {
                        l.clear();
                    }
                }

                // The top stays put (so the Play row doesn't jump): the box grows down for the Drill row.
                int box_h = rows * 2 + 4;
                int box_y = (20 - (3 * 2 + 4)) / 2;
                sb::panel_clear();
                sb::panel(box_x, box_y, box_w, box_h);

                for(int r = 0; r < rows; ++r)
                {
                    int kind = kinds[r];
                    int y = (box_y + 1 + r * 2) * 8 - 80 + 8;
                    const bn::string_view* list = kind == 0 ? plays : kind == 1 ? drills : levels;
                    row_names[r].set((box_x + 2) * 8 - 120, y, names[kind], sb::text_color::DARK);
                    int x = option_x;

                    for(int i = 0; i < counts[kind]; ++i)
                    {
                        bool chosen = values[kind] == i;
                        bn::string<16> text;

                        if(chosen && r == row)
                        {
                            text.append("<");
                            text.append(list[i]);
                            text.append(">");
                        }
                        else
                        {
                            text.append(list[i]);
                        }

                        sb::text_color color = ! chosen ? sb::text_color::DARK :
                                               r == row ? sb::text_color::RED : sb::text_color::BLUE;
                        options[r][i].set(x, y, text, color);
                        x += sb::text_width(text) + 8;
                    }
                }

                cursor.set(box_x * 8 - 120 + 8, (box_y + 1 + row * 2) * 8 - 80 + 8, ">", sb::text_color::RED);
                hint.set(0, (box_y + 1 + rows * 2) * 8 - 80 + 8, training ? drill_hints[values[1]] : match_hint,
                         sb::text_color::DARK, sb::align::CENTER);
            }

            sb::update();
            int kind = kinds[row];

            if(bn::keypad::up_pressed() || bn::keypad::down_pressed())
            {
                row = (row + (bn::keypad::up_pressed() ? rows - 1 : 1)) % rows;
                sb::sfx(bn::sound_items::sfx_select);
                redraw = true;
            }
            else if(bn::keypad::left_pressed() || bn::keypad::right_pressed())
            {
                int n = counts[kind];
                values[kind] = (values[kind] + (bn::keypad::left_pressed() ? n - 1 : 1)) % n;
                sb::sfx(bn::sound_items::sfx_select);
                redraw = true;
            }
            else if(bn::keypad::a_pressed())
            {
                sb::sfx(bn::sound_items::sfx_confirm);
                g_training = training;
                g_drill_choice = values[1];
                g_level_choice = values[2];
                sb::panel_clear();
                return true;
            }
            else if(bn::keypad::b_pressed())
            {
                sb::sfx(bn::sound_items::sfx_back);
                sb::panel_clear();
                return false;
            }
        }
    }

    // The result box: who won, and the best result on every difficulty. Returns true to play again.
    bool show_results(const match& g)
    {
        bool won = g.my_points > g.cpu_points;
        int value = sb::pingpong_result_value(g.my_points, g.cpu_points);
        bool record = sb::submit_extra(g_level_choice, value, sb::player_character());
        sb::play_jingle(record ? bn::music_items::jingle_record : bn::music_items::jingle_over);

        sb::panel(3, 0, 24, 14);
        sb::label title, result, heading, lines[3], record_label;
        title.set(0, -70, won ? "You win!" : "Good game!", sb::text_color::BLUE, sb::align::CENTER);

        bn::string<40> text(won ? "You beat " : "");
        text.append(sb::character_name(g.opponent));

        if(! won)
        {
            text.append(" won");
        }

        text.append(" ");
        text.append(sb::to_text(bn::max(g.my_points, g.cpu_points)));
        text.append("-");
        text.append(sb::to_text(bn::min(g.my_points, g.cpu_points)));
        text.append(" (");
        text.append(LEVELS[g_level_choice].name);
        text.append(")");
        result.set(0, -54, text, sb::text_color::DARK, sb::align::CENTER);
        heading.set(0, -36, "{ Best results {", sb::text_color::RED, sb::align::CENTER);

        for(int lvl = 0; lvl < 3; ++lvl)
        {
            bn::string<40> line(LEVELS[lvl].name);
            line.append(":  ");

            if(sb::extra_score(lvl) > 0)
            {
                line.append(sb::pingpong_result_text(sb::extra_score(lvl)));
                line.append("  ");
                line.append(sb::extra_name(lvl));
            }
            else
            {
                line.append("---");
            }

            lines[lvl].set(-72, -20 + lvl * 15, line, lvl == g_level_choice ? sb::text_color::BLUE : sb::text_color::DARK);
        }

        for(int t = 0; t < 50; ++t)
        {
            if(record && t % 20 == 0)
            {
                record_label.set(0, 26, "{ NEW RECORD! {", sb::text_color::YELLOW, sb::align::CENTER);
            }

            sb::update();
        }

        if(record)
        {
            sb::sfx(bn::sound_items::sfx_record);
        }

        constexpr bn::string_view items[] = { "Play again", "Back to map" };
        int choice = sb::menu(8, 14, items, 0, true);
        sb::panel_clear();
        return choice == 0;
    }
}

namespace sb
{
    // A result as one number that sorts correctly: any win beats any loss; a win by more beats a closer
    // one; a loss with more of your points beats a heavier loss.
    int pingpong_result_value(int my_points, int cpu_points)
    {
        my_points = bn::min(my_points, 99);
        cpu_points = bn::min(cpu_points, 99);

        if(my_points > cpu_points)
        {
            return 100000 + (99 - cpu_points) * 100 + my_points;
        }

        return 1 + my_points * 100 + (99 - cpu_points);
    }

    bool pingpong_result_won(int value)
    {
        return value >= 100000;
    }

    bn::string<8> pingpong_result_text(int value)
    {
        int my_points, cpu_points;

        if(pingpong_result_won(value))
        {
            int v = value - 100000;
            cpu_points = 99 - v / 100;
            my_points = v % 100;
        }
        else
        {
            int v = value - 1;
            my_points = v / 100;
            cpu_points = 99 - v % 100;
        }

        bn::string<8> text(bn::to_string<4>(my_points));
        text.append("-");
        text.append(bn::to_string<4>(cpu_points));
        return text;
    }

    const char* pingpong_level_name(int level)
    {
        return LEVELS[level].name;
    }
}

void sb::pingpong_run()
{
    bn::bg_palettes::set_transparent_color(bn::color(26, 18, 11));

    {
        match g;
        play_music(bn::music_items::pingpong_song);
        fade_in();

        bool first = true;
        bool changing_drill = false;

        while(true)
        {
            if(first)
            {
                // Shown on the start screen (scroll with Up/Down) and in the pause menu. '#' = heading.
                static constexpr bn::string_view howto[] = {
                    "#Serve",
                    "Hold A or B and let go",
                    "when the meter is in the",
                    "red box: a perfect serve!",
                    "A: topspin (fast, flat)",
                    "B: backspin (slow, curvy)",
                    "",
                    "#Rally",
                    "Move < > to return the",
                    "ball: you swing by",
                    "yourself.",
                    "",
                    "#Smash",
                    "Ready to attack? Hold A or",
                    "B to build up power, then",
                    "let go just as the ball",
                    "reaches you. Full power +",
                    "good timing = PERFECT!",
                    "A: forehand, aims left",
                    "B: backhand, aims right",
                    "You move slower while",
                    "charging a smash.",
                    "",
                    "#Win",
                    "First to 5 points, and",
                    "you must win by 2.",
                    "",
                    "#Training",
                    "Pick Training in the menu",
                    "to practise in the",
                    "basement with the ball",
                    "robot. Serve: serve into",
                    "its net. Follow: it shoots",
                    "at you, just time your",
                    "swing. Random: it shoots",
                    "all over: watch its head",
                    "to see where it aims.",
                    "Start: change drill, quit.",
                };

                if(! game_intro(GAME_PINGPONG, howto))
                {
                    break;
                }

                first = false;
            }

            if(! choose_settings(g, changing_drill))
            {
                break;
            }

            g.new_match();
            changing_drill = false;

            if(! g.play())
            {
                if(g.back_to_menu)
                {
                    changing_drill = true;  // the menu opens on the Level row
                    g.hide_hud();   // "Change drill" in the pause menu: back to the menu
                    continue;
                }

                break;
            }

            g.hide_hud();

            if(! show_results(g))
            {
                break;
            }

            play_music(bn::music_items::pingpong_song);
        }

        g.hide_hud();
        fade_out();
    }

    panel_clear();
}
