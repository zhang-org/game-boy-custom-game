// Ping Pong Pals: table tennis against a CPU family member, first to 7 points.
//
// Seen from a 3/4 top-down angle: you stand at the bottom (seen from behind), the CPU at the top.
// The ball lives in 3D world coordinates:
//   x  left/right in pixels (0 = middle of the table)
//   z  depth in pixels along the table (0 = the net, negative = CPU side, positive = your side)
//   h  height in pixels above the table top (the floor is at -TABLE_HEIGHT)
// and is drawn at screen (x, z + NET_SCREEN_Y - h), with a shadow on the table or floor below it.
//
// Shots don't use free physics: every shot picks a landing spot and a flight time and solves for
// the velocity (and lengthens the flight until the ball clears the net). Swings are automatic when
// the ball is in reach; the buttons only choose the shot. The CPU rates every shot you hit
// (power + how far it must run for the time it has): easy shots get attacked, medium ones returned,
// hard ones get a weak lob back, very hard ones make it miss. See DEVELOPMENT_NOTES.md sections 6-7.
//
// Art: tools/art/make_pingpong.py -> graphics/pingpong/.
#include "bn_bg_palettes.h"
#include "bn_color.h"
#include "bn_fixed.h"
#include "bn_keypad.h"
#include "bn_log.h"
#include "bn_math.h"
#include "bn_optional.h"
#include "bn_regular_bg_ptr.h"
#include "bn_sprite_ptr.h"
#include "bn_string.h"
#include "bn_string_view.h"
#include "bn_vector.h"

#include "bn_music_items.h"
#include "bn_regular_bg_items_pingpong_hall.h"
#include "bn_regular_bg_items_pingpong_table.h"
#include "bn_sound_items.h"
#include "bn_sprite_items_pingpong_ball.h"
#include "bn_sprite_items_pingpong_flake.h"
#include "bn_sprite_items_pingpong_light.h"
#include "bn_sprite_items_pingpong_paddle.h"
#include "bn_sprite_items_pingpong_shadow.h"

#include "sb_audio.h"
#include "sb_characters.h"
#include "sb_core.h"
#include "sb_games.h"
#include "sb_text.h"
#include "sb_ui.h"

namespace
{
    // =============================================================================================
    // Tuning. Change one thing at a time, then replay.
    // =============================================================================================

    // ---- Match
    constexpr int POINTS_TO_WIN = 7;            // first to this many points wins the match
    constexpr int SERVE_SWITCH_EVERY = 2;       // the serve changes sides every this many points
    constexpr int SCORE_PER_POINT = 100;        // high score: per point you win
    constexpr int SCORE_WIN_BONUS = 300;        // high score: bonus for winning the match

    // ---- Ball physics (pixels and frames)
    constexpr bn::fixed GRAVITY = 0.08;         // pulls the ball down every frame (lower = floatier, slower)
    constexpr bn::fixed TABLE_BOUNCE = 0.8;     // vertical speed kept when bouncing on the table
    constexpr bn::fixed FLOOR_BOUNCE = 0.5;     // vertical speed kept when bouncing on the floor
    constexpr bn::fixed FLOOR_FRICTION = 0.7;   // sideways speed kept when bouncing on the floor
    constexpr bn::fixed NET_HEIGHT = 7;         // the ball must be higher than this to cross the net
    constexpr bn::fixed NET_MARGIN = 4;         // planned shots clear the net by at least this much
    constexpr int SHOT_MIN_FRAMES = 18;         // no shot is ever faster than this

    // ---- You
    constexpr bn::fixed PLAYER_SPEED = 1.6;     // left/right speed (pixels per frame)
    constexpr bn::fixed PLAYER_SPEED_Z = 0.8;   // up/down speed
    constexpr bn::fixed PLAYER_REACH_X = 22;    // auto-swing reaches balls this far to either side
    constexpr bn::fixed PLAYER_REACH_FRONT = 12;// ... this far in front of you (toward the net)
    constexpr bn::fixed PLAYER_REACH_BACK = 6;  // ... and this far behind you
    constexpr bn::fixed WHIFF_MARGIN = 14;      // a ball this much beyond reach still makes you swing (and miss)
    constexpr int SHOT_BUFFER_FRAMES = 20;      // A/B pressed up to this many frames before contact still counts

    // Your shots: flight time in frames (lower = faster), landing depth (negative = CPU side),
    // landing x when aiming with Left/Right, and the "power" the CPU feels when returning it.
    constexpr int DRIVE_FRAMES = 40;            // A: fast drive
    constexpr int DRIVE_DEPTH = -26;
    constexpr int DRIVE_AIM_X = 30;
    constexpr int DRIVE_POWER = 40;
    constexpr int SMASH_FRAMES = 26;            // A on a high ball: smash
    constexpr int SMASH_DEPTH = -24;
    constexpr int SMASH_AIM_X = 32;
    constexpr int SMASH_POWER = 58;
    constexpr bn::fixed SMASH_MIN_HEIGHT = 18;  // the ball must be at least this high to smash it
    constexpr int LOB_FRAMES = 64;              // B: soft lob, slow and high, aims wider
    constexpr int LOB_DEPTH = -18;
    constexpr int LOB_AIM_X = 38;
    constexpr int LOB_POWER = 8;
    constexpr int PLAIN_FRAMES = 50;            // no button: a plain push to the middle
    constexpr int PLAIN_DEPTH = -22;
    constexpr int PLAIN_POWER = 12;
    constexpr int SERVE_FAST_FRAMES = 44;       // serve with A
    constexpr int SERVE_FAST_DEPTH = -24;
    constexpr int SERVE_FAST_POWER = 24;
    constexpr int SERVE_SOFT_FRAMES = 60;       // serve with B
    constexpr int SERVE_SOFT_DEPTH = -16;
    constexpr int SERVE_SOFT_POWER = 8;
    constexpr int SERVE_AIM_X = 30;
    constexpr bn::fixed TOSS_SPEED = 1.6;       // how high the ball is tossed before a serve

    // ---- CPU
    constexpr bn::fixed CPU_SPEED = 1.05;       // running speed (pixels per frame)
    constexpr int CPU_REACTION_FRAMES = 10;     // frames before it starts running after you hit
    constexpr bn::fixed CPU_REACH_X = 14;       // its swing reaches this far to either side
    constexpr bn::fixed CPU_REACH_FRONT = 10;   // ... this far in front (toward the net)
    constexpr bn::fixed CPU_REACH_BACK = 6;     // ... and behind
    constexpr bn::fixed CPU_HAND_OFFSET = 7;    // it runs to have the ball this far to its side
    constexpr int CPU_SERVE_DELAY = 70;         // frames before the CPU serves
    constexpr int CPU_SERVE_FRAMES = 56;        // its serve is gentle
    // How hard your shot is for the CPU: difficulty = power + run * CPU_RUN_WEIGHT (+- jitter),
    // where run = distance it must run / distance it can run in the time it has (1 = just makes it).
    constexpr int CPU_RUN_WEIGHT = 60;
    constexpr int CPU_JITTER = 8;               // small random wobble so it isn't robotic
    constexpr int CPU_ATTACK_BELOW = 20;        // easier than this: it attacks (fast shot away from you)
    constexpr int CPU_WEAK_FROM = 66;           // this hard or harder: weak, high return you can smash
    constexpr int CPU_MISS_FROM = 92;           // this hard or harder: it hits the net or out
    // CPU shots: flight frames (lower = faster). Landing spots are picked on your side.
    constexpr int CPU_NORMAL_FRAMES = 54;
    constexpr int CPU_ATTACK_FRAMES = 42;
    constexpr int CPU_WEAK_FRAMES = 72;
    constexpr int RALLY_SPEEDUP_MAX = 8;        // long rallies get up to this many frames faster per shot
    constexpr int RALLY_HITS_PER_SPEEDUP = 2;   // ... one frame faster every this many hits

    // ---- Timing
    constexpr int POINT_PAUSE_FRAMES = 80;      // pause after a point
    constexpr int MATCH_END_FRAMES = 110;       // the winner cheers this long before the result box
    constexpr int SWING_FRAMES = 12;            // length of the swing animation

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

    constexpr bn::fixed PLAYER_X_LIMIT = 84;
    constexpr bn::fixed PLAYER_Z_MIN = 44;
    constexpr bn::fixed PLAYER_Z_MAX = 54;
    constexpr bn::fixed PLAYER_START_Z = 46;
    constexpr bn::fixed CPU_X_LIMIT = 80;
    constexpr bn::fixed CPU_Z_MIN = -54;
    constexpr bn::fixed CPU_Z_MAX = -40;
    constexpr bn::fixed CPU_HOME_Z = -45;
    constexpr bn::fixed HIT_MIN_H = -7;         // balls lower than this (below the table top) can't be hit

    enum side { PLAYER = 0, CPU = 1 };

    enum shot_kind
    {
        SHOT_PLAIN, SHOT_DRIVE, SHOT_SMASH, SHOT_LOB, SHOT_SERVE_FAST, SHOT_SERVE_SOFT,
        SHOT_CPU_NORMAL, SHOT_CPU_ATTACK, SHOT_CPU_WEAK, SHOT_CPU_ERROR, SHOT_CPU_SERVE
    };

    enum cpu_plan { PLAN_ATTACK, PLAN_NORMAL, PLAN_WEAK, PLAN_ERROR };

    enum contact { C_NONE, C_TABLE, C_FLOOR, C_NET };

    enum class state { SERVE, TOSS, RALLY, POINT, DONE };

    struct ball_state
    {
        bn::fixed x, z, h, vx, vz, vh;
    };

    bool over_table(bn::fixed x, bn::fixed z)
    {
        return bn::abs(x) <= TABLE_HALF_W && z >= TABLE_FAR_Z && z <= TABLE_NEAR_Z;
    }

    // One frame of ball physics. Returns what the ball touched.
    contact step_ball(ball_state& b)
    {
        bn::fixed old_z = b.z;
        b.x += b.vx;
        b.z += b.vz;
        b.vh -= GRAVITY;
        b.h += b.vh;

        // The net: crossing z = 0 too low.
        bool crossed = (old_z < 0 && b.z >= 0) || (old_z > 0 && b.z <= 0);

        if(crossed && b.h < NET_HEIGHT && b.h > -2 && bn::abs(b.x) <= TABLE_HALF_W + 3)
        {
            b.z = old_z < 0 ? bn::fixed(-1) : bn::fixed(1);
            b.vz = -b.vz / 5;
            b.vx /= 2;

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
                b.vh = -b.vh * TABLE_BOUNCE;

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
                b.vh = -b.vh * FLOOR_BOUNCE;
                b.vx = b.vx * FLOOR_FRICTION;
                b.vz = b.vz * FLOOR_FRICTION;

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
    bn::fixed height_after(bn::fixed h0, bn::fixed vh0, int t)
    {
        return h0 + vh0 * t - GRAVITY * (t * (t + 1) / 2);
    }

    // Aims the ball at (tx, tz) on the table, landing after `frames` frames. Lengthens the flight
    // (a higher, slower arc) until the ball clears the net.
    void plan_shot(ball_state& b, bn::fixed tx, bn::fixed tz, int frames)
    {
        frames = bn::max(frames, SHOT_MIN_FRAMES);

        for(int attempt = 0; attempt < 30; ++attempt, frames += 2)
        {
            b.vx = (tx - b.x) / frames;
            b.vz = (tz - b.z) / frames;
            b.vh = (GRAVITY * (frames * (frames + 1) / 2) - b.h) / frames;

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

            if(height_after(b.h, b.vh, t) >= NET_HEIGHT + NET_MARGIN)
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

    // A character with a paddle.
    struct actor
    {
        int character;
        bn::sprite_ptr sprite;
        bn::sprite_ptr paddle;
        bn::fixed x, z;
        int walk = 0;
        int swing = 0;          // counts down during a swing
        int paddle_side = 1;    // +1: paddle on the screen-right, -1: screen-left
        bn::fixed swing_x, swing_y; // screen spot the swing aims at
        bool cheering = false;
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
            paddle_side = ball_x >= x ? 1 : -1;
            swing_x = ball_x;
            swing_y = ball_y_screen;
        }

        void draw(bool moving)
        {
            int cy = center_y();
            sprite.set_position(x.round_integer(), cy);

            int frame;

            if(cheering)
            {
                frame = sb::FRAME_CHEER;
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

            sprite.set_tiles(sb::character_sprite(character).tiles_item(), frame);

            // Paddle: at the hand when resting, reaching toward the ball while swinging.
            int pframe = 0;
            bn::fixed px = x + paddle_side * 9;
            bn::fixed py = cy + 3;

            if(cheering)
            {
                py = cy - 10;
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

            paddle.set_tiles(bn::sprite_items::pingpong_paddle.tiles_item(), pframe);
            paddle.set_horizontal_flip(paddle_side < 0);
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
        bn::vector<bn::sprite_ptr, 6> flakes;
        bn::vector<bn::sprite_ptr, 8> lights;
        bn::optional<actor> me;
        bn::optional<actor> cpu;
        sb::label score_label, message_label, hint_label;
        int message_timer = 0;

        // ---- match
        int opponent = 0;
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

        // ---- CPU brain
        cpu_plan plan = PLAN_NORMAL;
        bn::fixed cpu_target_x = 0, cpu_target_z = CPU_HOME_Z;
        int cpu_reaction = 0;

        match()
        {
            hall.set_priority(3);
            table.set_priority(2);
            ball_sprite.set_bg_priority(1);
            shadow_sprite.set_bg_priority(2);

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

            my_points = 0;
            cpu_points = 0;
            last_winner = PLAYER;
            message_label.clear();
            hint_label.clear();
            score_label.clear();
            message_timer = 0;
            start_serve();
            // play() shows the serve messages; keep the screen clean for the intro / result boxes.
            message_label.clear();
            hint_label.clear();
            draw();
        }

        int current_server() const
        {
            int total = my_points + cpu_points;
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
            me->cheering = false;
            cpu->cheering = false;
            me->z = PLAYER_START_Z;
            cpu_target_z = CPU_HOME_Z;

            if(server == CPU)
            {
                cpu_target_x = sb::rand_range(-24, 24);
                bn::string<32> text(sb::character_name(opponent));
                text.append(" serves");
                show_message(text, sb::text_color::WHITE, 70);
                hint_label.clear();
            }
            else
            {
                cpu_target_x = 0;
                show_message("Your serve!", sb::text_color::WHITE, 70);
                hint_label.set(-116, 70, "A: Fast  B: Soft", sb::text_color::WHITE);
            }

            hold_ball_at_server();
        }

        void hold_ball_at_server()
        {
            if(server == PLAYER)
            {
                ball = { me->x + 9, me->z - 2, 2, 0, 0, 0 };
            }
            else
            {
                ball = { cpu->x - 9, cpu->z + 2, 2, 0, 0, 0 };
            }
        }

        void update_score_label()
        {
            bn::string<48> text(sb::character_name(sb::player_character()));
            text.append(" ");
            text.append(sb::to_text(my_points));
            text.append(" - ");
            text.append(sb::to_text(cpu_points));
            text.append(" ");
            text.append(sb::character_name(opponent));
            score_label.set(0, -71, text, sb::text_color::WHITE, sb::align::CENTER);
        }

        void show_message(const bn::string_view& text, sb::text_color color, int frames)
        {
            message_label.set(0, -14, text, color, sb::align::CENTER);
            message_timer = frames;
        }

        void hide_hud()
        {
            score_label.clear();
            message_label.clear();
            hint_label.clear();
            message_timer = 0;
        }

        // ------------------------------------------------------------------------------- input
        int aim_input() const
        {
            return bn::keypad::left_held() ? -1 : bn::keypad::right_held() ? 1 : 0;
        }

        // Which shot the buttons ask for: the most recently pressed (or held) of A and B.
        int chosen_button() const
        {
            bool a = bn::keypad::a_held() || a_age <= SHOT_BUFFER_FRAMES;
            bool b = bn::keypad::b_held() || b_age <= SHOT_BUFFER_FRAMES;

            if(a && b)
            {
                return a_age <= b_age ? 1 : 2;
            }

            return a ? 1 : b ? 2 : 0;
        }

        // ------------------------------------------------------------------------------- shots
        bn::fixed default_aim_x() const
        {
            return clamp(ball.x / 2, -16, 16);
        }

        void player_hit()
        {
            int button = chosen_button();
            int aim = aim_input();
            shot_kind kind = button == 1 ? (ball.h >= SMASH_MIN_HEIGHT ? SHOT_SMASH : SHOT_DRIVE)
                                         : button == 2 ? SHOT_LOB : SHOT_PLAIN;
            int speedup = bn::min(rally_hits / RALLY_HITS_PER_SPEEDUP, RALLY_SPEEDUP_MAX);
            bn::fixed tx = default_aim_x();
            bn::fixed tz;
            int frames;

            switch(kind)
            {
            case SHOT_DRIVE:
                tz = DRIVE_DEPTH;
                frames = DRIVE_FRAMES - speedup;
                tx = aim ? bn::fixed(aim * DRIVE_AIM_X) : tx;
                break;
            case SHOT_SMASH:
                tz = SMASH_DEPTH;
                frames = SMASH_FRAMES;
                tx = aim ? bn::fixed(aim * SMASH_AIM_X) : tx;
                break;
            case SHOT_LOB:
                tz = LOB_DEPTH;
                frames = LOB_FRAMES;
                tx = aim ? bn::fixed(aim * LOB_AIM_X) : tx;
                break;
            default:
                tz = PLAIN_DEPTH;
                frames = PLAIN_FRAMES - speedup;
                break;
            }

            me->start_swing(ball.x, ball_screen_y());
            plan_shot(ball, tx, tz, frames);
            after_hit(PLAYER, kind);

            if(kind == SHOT_SMASH)
            {
                sb::sfx(bn::sound_items::sfx_smash);
                show_message("Smash!", sb::text_color::YELLOW, 40);
            }
            else
            {
                sb::sfx(bn::sound_items::sfx_paddle);
            }
        }

        void player_serve()
        {
            int aim = aim_input();
            bool fast = toss_kind == SHOT_SERVE_FAST;
            bn::fixed tx = aim ? bn::fixed(aim * SERVE_AIM_X) : default_aim_x();
            me->start_swing(ball.x, ball_screen_y());
            plan_shot(ball, tx, fast ? SERVE_FAST_DEPTH : SERVE_SOFT_DEPTH, fast ? SERVE_FAST_FRAMES : SERVE_SOFT_FRAMES);
            after_hit(PLAYER, fast ? SHOT_SERVE_FAST : SHOT_SERVE_SOFT);
            sb::sfx(bn::sound_items::sfx_paddle);
        }

        void cpu_serve()
        {
            bn::fixed tx = sb::rand_range(-26, 26);
            bn::fixed tz = sb::rand_range(16, 28);
            cpu->start_swing(ball.x, ball_screen_y());
            plan_shot(ball, tx, tz, CPU_SERVE_FRAMES);
            after_hit(CPU, SHOT_CPU_SERVE);
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
            cpu->start_swing(ball.x, ball_screen_y());
            shot_kind kind = SHOT_CPU_NORMAL;

            switch(plan)
            {
            case PLAN_ATTACK:
                kind = SHOT_CPU_ATTACK;
                plan_shot(ball, cpu_aim_x(true, 16, 34), sb::rand_range(22, 32), CPU_ATTACK_FRAMES - speedup);
                break;
            case PLAN_NORMAL:
                plan_shot(ball, cpu_aim_x(sb::rand_range(0, 1) == 0, 6, 30), sb::rand_range(14, 30),
                          CPU_NORMAL_FRAMES - speedup);
                break;
            case PLAN_WEAK:
                kind = SHOT_CPU_WEAK;
                plan_shot(ball, sb::rand_range(-14, 14), sb::rand_range(8, 20), CPU_WEAK_FRAMES);
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
                    plan_shot(ball, sb::rand_range(-20, 20), TABLE_NEAR_Z + sb::rand_range(12, 20), CPU_NORMAL_FRAMES);
                }
                else
                {
                    // Wide: lands off the side of the table.
                    int dir = sb::rand_range(0, 1) ? 1 : -1;
                    plan_shot(ball, dir * (TABLE_HALF_W + sb::rand_range(10, 18)), sb::rand_range(10, 26),
                              CPU_NORMAL_FRAMES);
                }
                break;
            }
            }

            after_hit(CPU, kind);
            sb::sfx(kind == SHOT_CPU_ATTACK ? bn::sound_items::sfx_smash : bn::sound_items::sfx_paddle);
        }

        void after_hit(int who, shot_kind kind)
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
                think_cpu(kind);
            }
            else
            {
                // Recover toward the middle.
                cpu_target_x = cpu->x / 3;
                cpu_target_z = CPU_HOME_Z;
                cpu_reaction = 0;
            }
        }

        int shot_power(shot_kind kind) const
        {
            switch(kind)
            {
            case SHOT_DRIVE: return DRIVE_POWER;
            case SHOT_SMASH: return SMASH_POWER;
            case SHOT_LOB: return LOB_POWER;
            case SHOT_SERVE_FAST: return SERVE_FAST_POWER;
            case SHOT_SERVE_SOFT: return SERVE_SOFT_POWER;
            default: return PLAIN_POWER;
            }
        }

        // The CPU sees your shot, works out where it can hit it and how hard that will be.
        void think_cpu(shot_kind kind)
        {
            ball_state b = ball;
            int bounce_count = 0;
            int home_frame = -1, any_frame = -1, last_frame = 0;
            ball_state home_pos = b, any_pos = b, last_pos = b;

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
                // Unreachable (or going out): run toward it anyway.
                frame = bn::max(last_frame, 1);
                at = last_pos;
                stand_z = CPU_Z_MAX;
            }

            // Stand with the ball on the closer side.
            bn::fixed off = at.x >= cpu->x ? -CPU_HAND_OFFSET : CPU_HAND_OFFSET;
            cpu_target_x = clamp(at.x + off, -CPU_X_LIMIT, CPU_X_LIMIT);
            cpu_target_z = stand_z;
            cpu_reaction = CPU_REACTION_FRAMES;

            bn::fixed dist = bn::abs(cpu_target_x - cpu->x) + bn::abs(cpu_target_z - cpu->z) / 2;
            dist = bn::max(dist - (CPU_REACH_X - CPU_HAND_OFFSET), bn::fixed(0));
            int avail = bn::max(frame - CPU_REACTION_FRAMES, 1);
            bn::fixed run = dist / (CPU_SPEED * avail);
            int difficulty = shot_power(kind) + (run * CPU_RUN_WEIGHT).round_integer() +
                             sb::rand_range(-CPU_JITTER, CPU_JITTER);

            if(home_frame < 0 && any_frame < 0)
            {
                plan = PLAN_ERROR;
            }
            else if(difficulty >= CPU_MISS_FROM)
            {
                plan = PLAN_ERROR;
            }
            else if(difficulty >= CPU_WEAK_FROM)
            {
                plan = PLAN_WEAK;
            }
            else if(difficulty < CPU_ATTACK_BELOW || (kind == SHOT_LOB && at.h >= SMASH_MIN_HEIGHT))
            {
                plan = PLAN_ATTACK;
            }
            else
            {
                plan = PLAN_NORMAL;
            }

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

            point_decided = true;
            last_winner = winner;
            st = state::POINT;
            timer = 0;

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
                award(PLAYER, last_shot == SHOT_SMASH ? "Smash winner!" : "Nice shot!");
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

            contact c = step_ball(ball);

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
            else
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
                    cpu->start_swing(ball.x, ball_screen_y());
                    sb::sfx(bn::sound_items::sfx_whoosh);
                }
            }
        }

        bool move_player()
        {
            bn::fixed old_x = me->x, old_z = me->z;

            if(! me->cheering)
            {
                if(bn::keypad::left_held())
                {
                    me->x -= PLAYER_SPEED;
                }
                else if(bn::keypad::right_held())
                {
                    me->x += PLAYER_SPEED;
                }

                // During your own serve you stay at the serving line.
                if(! (server == PLAYER && (st == state::SERVE || st == state::TOSS)))
                {
                    if(bn::keypad::up_held())
                    {
                        me->z -= PLAYER_SPEED_Z;
                    }
                    else if(bn::keypad::down_held())
                    {
                        me->z += PLAYER_SPEED_Z;
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
                cpu->x = approach(cpu->x, cpu_target_x, CPU_SPEED);
                cpu->z = approach(cpu->z, cpu_target_z, CPU_SPEED);
            }

            cpu->x = clamp(cpu->x, -CPU_X_LIMIT, CPU_X_LIMIT);
            cpu->z = clamp(cpu->z, CPU_Z_MIN, CPU_Z_MAX);
            return cpu->x != old_x || cpu->z != old_z;
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

            bool me_moving = move_player();
            bool cpu_moving = move_cpu();
            ++timer;

            switch(st)
            {
            case state::SERVE:
                hold_ball_at_server();
                // A gentle bounce in the hand.
                ball.h = 2 + ((timer / 8) % 2);

                if(server == PLAYER)
                {
                    bool a = bn::keypad::a_pressed();
                    bool b = bn::keypad::b_pressed();

                    if(a || b)
                    {
                        toss_kind = a ? SHOT_SERVE_FAST : SHOT_SERVE_SOFT;
                        ball.vh = TOSS_SPEED;
                        st = state::TOSS;
                        hint_label.clear();
                        sb::sfx(bn::sound_items::sfx_jump);
                    }
                }
                else if(timer >= CPU_SERVE_DELAY && cpu->x == cpu_target_x)
                {
                    ball.vh = TOSS_SPEED;
                    st = state::TOSS;
                    sb::sfx(bn::sound_items::sfx_jump);
                }
                break;

            case state::TOSS:
                ball.vh -= GRAVITY;
                ball.h += ball.vh;

                if(ball.vh <= 0)
                {
                    if(server == PLAYER)
                    {
                        player_serve();
                    }
                    else
                    {
                        cpu_serve();
                    }
                }
                break;

            case state::RALLY:
                update_ball();
                check_hits();
                break;

            case state::POINT:
                update_ball();

                if(timer >= POINT_PAUSE_FRAMES)
                {
                    if(my_points >= POINTS_TO_WIN || cpu_points >= POINTS_TO_WIN)
                    {
                        st = state::DONE;
                        timer = 0;
                        bool won = my_points >= POINTS_TO_WIN;
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

            draw_actors(me_moving, cpu_moving);
            draw_ball();
            animate_scenery();
            return true;
        }

        void draw_actors(bool me_moving, bool cpu_moving)
        {
            me->draw(me_moving);
            cpu->draw(cpu_moving);
        }

        void draw()
        {
            draw_actors(false, false);
            draw_ball();
            animate_scenery();
        }

        void draw_ball()
        {
            bool hidden = st == state::DONE || (st == state::POINT && timer > POINT_PAUSE_FRAMES - 10);
            ball_sprite.set_visible(! hidden);
            shadow_sprite.set_visible(! hidden);

            if(hidden)
            {
                return;
            }

            int frame = ball.h > 24 ? 2 : ball.h < -4 ? 0 : 1;
            ball_sprite.set_tiles(bn::sprite_items::pingpong_ball.tiles_item(), frame);
            ball_sprite.set_position(ball.x.round_integer(), ball_screen_y().round_integer());

            // Hidden behind the table's far edge, or behind the CPU?
            bool behind_table = ball.z < TABLE_FAR_Z && ball.h < 0;
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
        }

        // Plays one match. Returns false if the player quit from the pause menu.
        bool play()
        {
            update_score_label();
            start_serve();

            while(true)
            {
                // Update first, so the button that closed the intro / result box isn't seen as a serve.
                sb::update();

                if(bn::keypad::start_pressed() && st != state::DONE)
                {
                    set_paused_visibility(false);
                    bool quit = sb::game_pause();
                    set_paused_visibility(true);

                    if(quit)
                    {
                        return false;
                    }

                    // Don't let the button that closed the menu count as a shot.
                    a_age = b_age = 1000;
                }

                if(! tick())
                {
                    return true;
                }
            }
        }

        int score() const
        {
            return my_points * SCORE_PER_POINT + (my_points >= POINTS_TO_WIN ? SCORE_WIN_BONUS : 0);
        }
    };
}

void sb::pingpong_run()
{
    bn::bg_palettes::set_transparent_color(bn::color(26, 18, 11));

    {
        match g;
        play_music(bn::music_items::pingpong_song);
        fade_in();

        bool first = true;

        while(true)
        {
            if(first)
            {
                bn::string<32> versus("Today: vs ");
                versus.append(character_name(g.opponent));
                const bn::string_view howto[] = {
                    "Auto-swing when in reach!",
                    "A: fast drive  B: soft lob",
                    "Hold < or > to aim a shot",
                    versus,
                };

                if(! game_intro(GAME_PINGPONG, howto))
                {
                    break;
                }

                first = false;
            }

            if(! g.play())
            {
                break;
            }

            int final_score = g.score();
            bool won = g.my_points >= POINTS_TO_WIN;
            bn::string<32> detail;

            if(won)
            {
                detail = "You won ";
            }
            else
            {
                detail = character_name(g.opponent);
                detail.append(" won ");
            }

            detail.append(to_text(bn::max(g.my_points, g.cpu_points)));
            detail.append("-");
            detail.append(to_text(bn::min(g.my_points, g.cpu_points)));

            if(won)
            {
                detail.append("!");
            }

            g.hide_hud();

            if(! game_over(GAME_PINGPONG, final_score, detail))
            {
                break;
            }

            g.new_match();
            play_music(bn::music_items::pingpong_song);
        }

        g.hide_hud();
        fade_out();
    }

    panel_clear();
}
