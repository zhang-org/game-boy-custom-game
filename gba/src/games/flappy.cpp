// Jingle Flap: ride a flying reindeer between snowy chimneys and icicle clouds over the rooftops.
//
// Controls: A (or Up) flaps. Start pauses. +1 point for every gap you fly through.
// Art: tools/art/make_flappy.py (graphics/flappy/). Song: flappy_song.
#include "bn_bg_palettes.h"
#include "bn_color.h"
#include "bn_fixed.h"
#include "bn_keypad.h"
#include "bn_math.h"
#include "bn_regular_bg_ptr.h"
#include "bn_sprite_affine_mat_ptr.h"
#include "bn_sprite_palettes.h"
#include "bn_sprite_ptr.h"
#include "bn_string.h"
#include "bn_string_view.h"
#include "bn_vector.h"

// Testing aid: uncomment to let the game fly itself while R is held (used with the headless harness,
// e.g. "hold R 900", to test scoring, speed-ups and high scores). Never ship it enabled.
// #define FLAPPY_AUTOPILOT

#include "bn_music_items.h"
#include "bn_sound_items.h"
#include "bn_regular_bg_items_flappy_roofs.h"
#include "bn_regular_bg_items_flappy_sky.h"
#include "bn_regular_bg_items_flappy_town.h"
#include "bn_sprite_items_flappy_chimney.h"
#include "bn_sprite_items_flappy_chimney_top.h"
#include "bn_sprite_items_flappy_cloud.h"
#include "bn_sprite_items_flappy_cloud_cap.h"
#include "bn_sprite_items_flappy_moon.h"
#include "bn_sprite_items_flappy_reindeer.h"

#include "sb_audio.h"
#include "sb_characters.h"
#include "sb_core.h"
#include "sb_games.h"
#include "sb_snow.h"
#include "sb_text.h"
#include "sb_ui.h"

namespace
{
    // ------------------------------------------------------------------ tuning (start EASY: kids play this)

    // Flying
    constexpr bn::fixed GRAVITY = 0.11;            // pixels/frame^2 pulling down: higher = falls faster
    constexpr bn::fixed FLAP_SPEED = -2.4;         // vertical speed right after a flap (negative = up): more negative = bigger hop
    constexpr bn::fixed MAX_FALL_SPEED = 2.8;      // pixels/frame: the fastest the reindeer can drop
    constexpr int PLAYER_X = -56;                  // screen x of the reindeer (fixed; the world scrolls past)
    constexpr int CEILING_Y = -60;                 // reindeer can't fly higher than this (no penalty, it just stops)
    constexpr int GROUND_Y = 58;                   // screen y of the snowy rooftops: touching it = game over

    // Grace hitbox around the reindeer's centre (smaller than the art, so near misses count as misses)
    constexpr int HIT_HALF_W = 8;                  // pixels left/right of centre
    constexpr int HIT_TOP = 13;                    // pixels above centre (the rider's hat can poke into clouds)
    constexpr int HIT_BOTTOM = 6;                  // pixels below centre (hooves can dip into chimney snow)
    constexpr int OBSTACLE_HALF_W = 11;            // collision half width of chimneys and clouds (art is 12-15)

    // Scrolling and obstacles
    constexpr bn::fixed START_SCROLL = 0.9;        // pixels/frame the world moves at the start: higher = harder
    constexpr bn::fixed SCROLL_STEP = 0.1;         // added to the scroll speed every SPEEDUP_EVERY points
    constexpr bn::fixed MAX_SCROLL = 1.5;          // scroll speed never goes above this
    constexpr int SPEEDUP_EVERY = 10;              // points between speed-ups ("Faster!")
    constexpr int START_GAP = 74;                  // pixels of open air between top and bottom obstacle at the start
    constexpr int GAP_STEP = 3;                    // gap gets this much smaller at every speed-up
    constexpr int MIN_GAP = 58;                    // the gap never gets smaller than this
    constexpr int COLUMN_SPACING = 124;            // pixels between obstacle columns: bigger = more time to react
    constexpr int FIRST_COLUMN_X = 190;            // where the first column starts (screen right edge is 120)
    constexpr int MAX_GAP_SHIFT = 36;              // max up/down jump of the gap between neighbouring columns
    constexpr int MIN_CLOUD = 8;                   // a top cloud always reaches at least this far into the screen
    constexpr int MIN_CHIMNEY = 10;                // a chimney always sticks up at least this far above the roofs

    // Parallax (fraction of the obstacle scroll speed)
    constexpr bn::fixed SKY_PARALLAX = 0.08;       // stars
    constexpr bn::fixed TOWN_PARALLAX = 0.35;      // far houses
    constexpr bn::fixed SNOW_PARALLAX = 0.5;       // falling snow drifts left with the wind

    // Look and feel
    constexpr int RIDER_DX = -5;                   // rider position relative to the reindeer sprite centre
    constexpr int RIDER_DY = -11;
    constexpr bn::fixed TILT_PER_SPEED = 9;        // degrees of nose tilt per pixel/frame of vertical speed
    constexpr bn::fixed MAX_TILT_UP = 20;          // degrees
    constexpr bn::fixed MAX_TILT_DOWN = 35;        // degrees
    constexpr int FLAP_ANIM_FRAMES = 16;           // length of the leg-flap animation after each flap
    constexpr int FASTER_NOTICE_FRAMES = 70;       // how long "Faster!" stays on screen
    constexpr int SNOWFLAKES = 14;                 // falling snow sprites

    // Art layout (from tools/art/make_flappy.py)
    constexpr int REINDEER_GLIDE = 0, REINDEER_DOWN = 1, REINDEER_TUCK = 2, REINDEER_CRASH = 3;
    constexpr int CHIMNEY_TOP_SNOW = 2;            // first solid row of snow in flappy_chimney_top (16 px tall)
    constexpr int CLOUD_CAP_EDGE = 24;             // row of flappy_cloud_cap treated as the cloud's bottom edge

    constexpr int COLUMN_COUNT = 3;

    struct column
    {
        bn::fixed x;
        int gap_top = 0;     // screen y of the bottom edge of the top cloud
        int gap_bottom = 0;  // screen y of the top of the chimney
        bool scored = false;
        bn::vector<bn::sprite_ptr, 6> sprites;
    };

    void build_column(column& c, bn::fixed x, int gap_centre, int gap)
    {
        c.x = x;
        c.gap_top = gap_centre - gap / 2;
        c.gap_bottom = c.gap_top + gap;
        c.scored = false;
        c.sprites.clear();

        int ix = x.round_integer();

        // Chimney: snowy top, then 64 px brick pieces down behind the rooftops.
        int top_y = c.gap_bottom - CHIMNEY_TOP_SNOW + 8;
        bn::sprite_ptr top = bn::sprite_items::flappy_chimney_top.create_sprite(ix, top_y);
        top.set_bg_priority(2);
        c.sprites.push_back(top);

        for(int y = top_y + 8 + 32; y - 32 < 80 && ! c.sprites.full(); y += 64)
        {
            bn::sprite_ptr body = bn::sprite_items::flappy_chimney.create_sprite(ix, y);
            body.set_bg_priority(2);
            c.sprites.push_back(body);
        }

        // Cloud: icicle cap at the bottom, 64 px puffy pieces up to the top of the screen.
        int cap_y = c.gap_top - CLOUD_CAP_EDGE + 16;
        bn::sprite_ptr cap = bn::sprite_items::flappy_cloud_cap.create_sprite(ix, cap_y);
        cap.set_bg_priority(2);
        c.sprites.push_back(cap);

        for(int y = cap_y - 16 - 32; y + 32 > -80 && ! c.sprites.full(); y -= 64)
        {
            bn::sprite_ptr body = bn::sprite_items::flappy_cloud.create_sprite(ix, y);
            body.set_bg_priority(2);
            c.sprites.push_back(body);
        }
    }

    void place_column(column& c)
    {
        for(bn::sprite_ptr& sprite : c.sprites)
        {
            sprite.set_x(c.x);
        }
    }

    bn::fixed clamp(bn::fixed v, bn::fixed lo, bn::fixed hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    bn::fixed wrap(bn::fixed v, int period)
    {
        while(v <= -period)
        {
            v += period;
        }

        return v;
    }

    // Everything that changes during one round.
    struct round_state
    {
        bn::fixed y = -8;
        bn::fixed vy = 0;
        bn::fixed scroll = START_SCROLL;
        int gap = START_GAP;
        int score = 0;
        int level = 0;
        int last_gap_centre = -12;
        int flap_timer = 0;
    };

    int gap_centre_range_lo(int gap)
    {
        return -80 + MIN_CLOUD + gap / 2;
    }

    int gap_centre_range_hi(int gap)
    {
        return GROUND_Y - MIN_CHIMNEY - gap / 2;
    }

    int next_gap_centre(round_state& s)
    {
        int lo = gap_centre_range_lo(s.gap);
        int hi = gap_centre_range_hi(s.gap);
        int from = bn::max(lo, s.last_gap_centre - MAX_GAP_SHIFT);
        int to = bn::min(hi, s.last_gap_centre + MAX_GAP_SHIFT);
        int centre = from >= to ? (lo + hi) / 2 : sb::rand_range(from, to);
        s.last_gap_centre = centre;
        return centre;
    }

    int reindeer_frame(int flap_timer)
    {
        // Flap: legs push down, tuck up, push down again, then glide.
        if(flap_timer > 12)
        {
            return REINDEER_DOWN;
        }

        if(flap_timer > 7)
        {
            return REINDEER_TUCK;
        }

        if(flap_timer > 2)
        {
            return REINDEER_DOWN;
        }

        return REINDEER_GLIDE;
    }
}

void sb::flappy_run()
{
    constexpr bn::string_view howto[] = {
        "Press A to flap and fly!",
        "Fly between the chimneys",
        "and the icy clouds.",
        "1 point for every gap.",
    };

    // ---- Scene
    bn::bg_palettes::set_transparent_color(bn::color(2, 3, 8));
    bn::regular_bg_ptr sky = bn::regular_bg_items::flappy_sky.create_bg(0, 0);
    sky.set_priority(3);
    bn::regular_bg_ptr town = bn::regular_bg_items::flappy_town.create_bg(0, 0);
    town.set_priority(2);
    bn::regular_bg_ptr roofs = bn::regular_bg_items::flappy_roofs.create_bg(0, 0);
    roofs.set_priority(1);

    bn::sprite_ptr moon = bn::sprite_items::flappy_moon.create_sprite(78, -52);
    moon.set_bg_priority(3);

    int character = player_character();
    const bn::sprite_item& rider_item = character_sprite(character);
    bn::sprite_ptr reindeer = bn::sprite_items::flappy_reindeer.create_sprite(PLAYER_X, -8, REINDEER_GLIDE);
    reindeer.set_bg_priority(1);
    bn::sprite_ptr rider = rider_item.create_sprite(PLAYER_X + RIDER_DX, -8 + RIDER_DY, FRAME_SIDE_SIT);
    rider.set_bg_priority(1);
    rider.set_z_order(-1);  // in front of the reindeer
    bn::sprite_affine_mat_ptr tilt = bn::sprite_affine_mat_ptr::create();
    reindeer.set_affine_mat(tilt);
    rider.set_affine_mat(tilt);

    snowfall snow(SNOWFLAKES, 2);
    bn::vector<column, COLUMN_COUNT> columns;
    label score_label, notice_label, hint_label;

    bn::fixed sky_x = 0, town_x = 0, roofs_x = 0;
    round_state state;

    // Draws the reindeer and rider at height y with a nose tilt in degrees (positive = nose up).
    auto place_player = [&](bn::fixed y, bn::fixed angle) {
        reindeer.set_position(PLAYER_X, y);
        tilt.set_rotation_angle(angle < 0 ? angle + 360 : angle);
        // Rotate the rider's seat offset with the reindeer (screen y points down, angle turns counterclockwise).
        bn::fixed s = bn::degrees_lut_sin(angle < 0 ? angle + 360 : angle);
        bn::fixed c = bn::degrees_lut_cos(angle < 0 ? angle + 360 : angle);
        bn::fixed ox = c * RIDER_DX + s * RIDER_DY;
        bn::fixed oy = c * RIDER_DY - s * RIDER_DX;
        rider.set_position(PLAYER_X + ox, y + oy);
    };

    auto set_reindeer_frame = [&](int frame) {
        reindeer.set_tiles(bn::sprite_items::flappy_reindeer.tiles_item(), frame);
    };

    auto scroll_world = [&](bn::fixed speed) {
        sky_x = wrap(sky_x - speed * SKY_PARALLAX, 256);
        town_x = wrap(town_x - speed * TOWN_PARALLAX, 512);
        roofs_x = wrap(roofs_x - speed, 512);
        sky.set_x(sky_x);
        town.set_x(town_x);
        roofs.set_x(roofs_x);
        snow.update(-speed * SNOW_PARALLAX);
    };

    auto show_score = [&]() {
        bn::string<24> text("Score: ");
        text.append(to_text(state.score));
        score_label.set(-114, -70, text, text_color::WHITE);
    };

    // Pause box; hides our text so it doesn't show through the box.
    auto pause = [&]() {
        notice_label.set_visible(false);
        hint_label.set_visible(false);
        bool leave = game_pause();
        notice_label.set_visible(true);
        hint_label.set_visible(true);
        return leave;
    };

    auto reset_round = [&]() {
        state = round_state();
        columns.clear();

        for(int i = 0; i < COLUMN_COUNT; ++i)
        {
            columns.emplace_back();
            build_column(columns.back(), FIRST_COLUMN_X + i * COLUMN_SPACING, next_gap_centre(state), state.gap);
            place_column(columns.back());
        }

        set_reindeer_frame(REINDEER_GLIDE);
        place_player(state.y, 0);
        notice_label.clear();
        hint_label.clear();
        score_label.clear();
    };

    reset_round();
    play_music(bn::music_items::flappy_song);
    fade_in();

    bool show_intro = true;
    bool quit = false;

    while(! quit)
    {
        if(show_intro && ! game_intro(GAME_FLAPPY, howto))
        {
            break;
        }

        show_intro = false;
        show_score();

        // ---- Get ready: hover in place until the first flap.
        notice_label.set(24, -24, "Get ready!", text_color::YELLOW, align::CENTER);
        hint_label.set(24, -6, "Press A to flap", text_color::WHITE, align::CENTER);

        for(int t = 0; ; ++t)
        {
            bn::fixed bob = bn::degrees_lut_sin((t * 4) % 360) * 3;
            state.flap_timer = (t % 40 == 0) ? FLAP_ANIM_FRAMES : bn::max(0, state.flap_timer - 1);
            set_reindeer_frame(reindeer_frame(state.flap_timer));
            place_player(state.y + bob, 0);
            scroll_world(START_SCROLL / 2);
            update();

            if(bn::keypad::start_pressed())
            {
                if(pause())
                {
                    quit = true;
                    break;
                }

                continue;
            }

            if(bn::keypad::a_pressed() || bn::keypad::up_pressed())
            {
                state.y += bob;
                break;
            }
        }

        notice_label.clear();
        hint_label.clear();

        if(quit)
        {
            break;
        }

        // ---- Play. The first flap happens right away (the A press that ended "Get ready").
        bool flap = true;
        bool crashed = false;
        int notice_timer = 0;

        while(! crashed)
        {
            if(flap)
            {
                state.vy = FLAP_SPEED;
                state.flap_timer = FLAP_ANIM_FRAMES;
                sfx(bn::sound_items::sfx_flap);
            }

            state.vy = bn::min(state.vy + GRAVITY, MAX_FALL_SPEED);
            state.y += state.vy;

            if(state.y < CEILING_Y)
            {
                state.y = CEILING_Y;
                state.vy = 0;
            }

            if(state.flap_timer > 0)
            {
                --state.flap_timer;
            }

            set_reindeer_frame(reindeer_frame(state.flap_timer));
            place_player(state.y, clamp(-state.vy * TILT_PER_SPEED, -MAX_TILT_DOWN, MAX_TILT_UP));

            // Move the obstacles; recycle columns that left the screen on the right.
            bn::fixed rightmost = -1000;

            for(column& c : columns)
            {
                c.x -= state.scroll;
                rightmost = bn::max(rightmost, c.x);
            }

            for(column& c : columns)
            {
                if(c.x < -120 - 20)
                {
                    build_column(c, rightmost + COLUMN_SPACING, next_gap_centre(state), state.gap);
                    rightmost = c.x;
                }

                place_column(c);
            }

            scroll_world(state.scroll);

            // Score and collisions.
            int py = state.y.round_integer();

            for(column& c : columns)
            {
                bn::fixed dx = c.x - PLAYER_X;

                if(! c.scored && dx < 0)
                {
                    c.scored = true;
                    ++state.score;
                    sfx(bn::sound_items::sfx_point);
                    show_score();

                    if(state.score % SPEEDUP_EVERY == 0)
                    {
                        bn::fixed faster = START_SCROLL + SCROLL_STEP * (state.level + 1);

                        if(faster <= MAX_SCROLL || state.gap > MIN_GAP)
                        {
                            ++state.level;
                            state.scroll = bn::min(faster, MAX_SCROLL);
                            state.gap = bn::max(START_GAP - GAP_STEP * state.level, MIN_GAP);
                            notice_label.set(24, -40, "Faster!", text_color::YELLOW, align::CENTER);
                            notice_timer = FASTER_NOTICE_FRAMES;
                            sfx(bn::sound_items::sfx_whoosh);
                        }
                    }
                }

                if(bn::abs(dx) < OBSTACLE_HALF_W + HIT_HALF_W &&
                   (py - HIT_TOP < c.gap_top || py + HIT_BOTTOM > c.gap_bottom))
                {
                    crashed = true;
                }
            }

            if(py + HIT_BOTTOM >= GROUND_Y)
            {
                crashed = true;
            }

            if(notice_timer > 0 && --notice_timer == 0)
            {
                notice_label.clear();
            }

            update();

            if(crashed)
            {
                break;
            }

            if(bn::keypad::start_pressed())
            {
                if(pause())
                {
                    quit = true;
                    break;
                }
            }

            flap = bn::keypad::a_pressed() || bn::keypad::up_pressed();

#ifdef FLAPPY_AUTOPILOT
            if(bn::keypad::r_held())
            {
                int target = 0;
                bn::fixed best = 1000;

                for(const column& c : columns)
                {
                    if(c.x + OBSTACLE_HALF_W + HIT_HALF_W > PLAYER_X && c.x < best)
                    {
                        best = c.x;
                        target = (c.gap_top + c.gap_bottom) / 2;
                    }
                }

                flap = state.y > target + 6 && state.vy >= 0;
            }
#endif
        }

        if(quit)
        {
            break;
        }

        // ---- Crash: flash, then tumble down onto the rooftops.
        sfx(bn::sound_items::sfx_crash);
        notice_label.clear();
        set_reindeer_frame(REINDEER_CRASH);
        bn::bg_palettes::set_fade(bn::color(31, 31, 31), bn::fixed(0.5));
        bn::sprite_palettes::set_fade(bn::color(31, 31, 31), bn::fixed(0.5));
        update();
        update();
        bn::bg_palettes::set_fade(bn::color(31, 31, 31), 0);
        bn::sprite_palettes::set_fade(bn::color(31, 31, 31), 0);
        state.vy = bn::min(state.vy, bn::fixed(-1));

        for(int t = 0; t < 90 && state.y + HIT_BOTTOM < GROUND_Y + 4; ++t)
        {
            state.vy = bn::min(state.vy + GRAVITY * 2, MAX_FALL_SPEED * 2);
            state.y += state.vy;
            place_player(state.y, -MAX_TILT_DOWN);
            snow.update();
            update();
        }

        wait_frames(30);

        score_label.clear();  // the result box shows the score (and would sit on top of this label)

        if(! game_over(GAME_FLAPPY, state.score))
        {
            break;
        }

        // Play again: new round, no intro.
        reset_round();
        play_music(bn::music_items::flappy_song);
    }

    fade_out();
    score_label.clear();
    notice_label.clear();
    hint_label.clear();
    panel_clear();
}
