// Jingle Flap: ride a flying reindeer between snowy chimneys and icicle clouds over the rooftops, on a
// world tour: St. Louis first, then Gainesville, Seattle, the Bay Area and Moorestown in a random order
// (a new city every 10 gaps).
//
// Controls: A (or Up) flaps. Start pauses. +1 point for every gap you fly through (+2 per candy cane,
// switched off for now: CANDY_CHANCE).
// Art: tools/art/make_flappy.py, tools/art/make_flappy_cities.py (graphics/flappy/). Song: flappy_song.
#include "bn_bg_palettes.h"
#include "bn_blending.h"
#include "bn_optional.h"
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
#include "bn_regular_bg_items_flappy_city_gnv.h"
#include "bn_regular_bg_items_flappy_city_mtn.h"
#include "bn_regular_bg_items_flappy_city_sea.h"
#include "bn_regular_bg_items_flappy_city_sf.h"
#include "bn_regular_bg_items_flappy_city_stl.h"
#include "bn_sprite_items_flappy_candy.h"
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
    // ------------------------------------------------------------------ tuning

    // Flying: a little floaty, but not sluggish
    constexpr bn::fixed GRAVITY = 0.14;            // pixels/frame^2 pulling down: higher = falls faster
    constexpr bn::fixed FLAP_SPEED = -2.7;         // vertical speed right after a flap (negative = up): more negative = bigger hop
    constexpr bn::fixed MAX_FALL_SPEED = 3.3;      // pixels/frame: the fastest the reindeer can drop
    constexpr int PLAYER_X = -56;                  // screen x of the reindeer (fixed; the world scrolls past)
    constexpr int CEILING_Y = -70;                 // reindeer can't fly higher than this (no penalty, it just stops)
    constexpr int GROUND_Y = 66;                   // screen y of the snowy rooftops: touching it = game over
    constexpr int GROUND_SHIFT = 8;                // the rooftops and skyline are drawn this much lower than in the art
    // "Zoomed out": the reindeer and rider are drawn smaller, so there is more room to fly up and down.
    constexpr bn::fixed PLAYER_SCALE = 0.8;

    // Grace hitbox around the reindeer's centre (smaller than the art, so near misses count as misses)
    constexpr int HIT_HALF_W = 6;                  // pixels left/right of centre
    constexpr int HIT_TOP = 10;                    // pixels above centre (the rider's hat can poke into clouds)
    constexpr int HIT_BOTTOM = 5;                  // pixels below centre (hooves can dip into chimney snow)
    constexpr int OBSTACLE_HALF_W = 11;            // collision half width of chimneys and clouds (art is 12-15)

    // Scrolling and obstacles: it starts brisk and keeps getting faster, with tighter gaps
    constexpr bn::fixed START_SCROLL = 1.25;       // pixels/frame the world moves at the start: higher = harder
    constexpr bn::fixed SCROLL_STEP = 0.13;        // added to the scroll speed every SPEEDUP_EVERY gaps
    constexpr bn::fixed MAX_SCROLL = 2.5;          // scroll speed never goes above this
    constexpr int SPEEDUP_EVERY = 10;              // gaps between speed-ups (also when the city changes)
    constexpr int START_GAP = 60;                  // pixels of open air between top and bottom obstacle at the start
    constexpr int GAP_STEP = 3;                    // gap gets this much smaller at every speed-up
    constexpr int MIN_GAP = 46;                    // the gap never gets smaller than this ...
    constexpr int TIGHT_EVERY = 4;                 // ... except every 4th column, which is tighter still:
    constexpr int TIGHT_SHRINK = 8;                //     this many pixels smaller
    constexpr int TIGHTEST_GAP = 40;               //     but never below this
    constexpr int COLUMN_SPACING = 112;            // pixels between obstacle columns: bigger = more time to react
    constexpr int FIRST_COLUMN_X = 190;            // where the first column starts (screen right edge is 120)
    constexpr int MAX_GAP_SHIFT = 62;              // max up/down jump of the gap between neighbouring columns ...
    constexpr int MIN_GAP_SHIFT = 22;              // ... and at least this much whenever there is room (big height changes)
    constexpr int MIN_CLOUD = 4;                   // a top cloud always reaches at least this far into the screen
    constexpr int MIN_CHIMNEY = 6;                 // a chimney always sticks up at least this far above the roofs

    // Candy canes: bonus points in risky spots (right next to a cloud or a chimney)
    constexpr int CANDY_CHANCE = 0;                // % of columns that have one (0 = switched off for now)
    constexpr int CANDY_POINTS = 2;
    constexpr int CANDY_EDGE = 9;                  // how far from the edge of the gap it floats
    constexpr int CANDY_REACH = 11;                // how close the reindeer must get to grab it

    // World tour: a new city every SPEEDUP_EVERY gaps. St. Louis always comes first, the others in a
    // random order (shuffled again every lap).
    constexpr int CITY_FADE_FRAMES = 24;           // the old skyline fades out, the new one fades in
    constexpr int CITY_ENTER_X = -166;             // skyline position when a city starts: its landmark comes in from the right
    constexpr int CITY_START_X = -296;             // ... and at the very start of a round (the Arch is on screen)

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
        bn::optional<bn::sprite_ptr> candy;
        int candy_y = 0;
    };

    void build_column(column& c, bn::fixed x, int gap_centre, int gap)
    {
        c.x = x;
        c.gap_top = gap_centre - gap / 2;
        c.gap_bottom = c.gap_top + gap;
        c.scored = false;
        c.sprites.clear();
        c.candy.reset();

        // Sometimes a candy cane, close to the top or the bottom of the gap.
        if(sb::rand_range(0, 99) < CANDY_CHANCE)
        {
            c.candy_y = sb::rand_range(0, 1) ? c.gap_top + CANDY_EDGE : c.gap_bottom - CANDY_EDGE;
            c.candy = bn::sprite_items::flappy_candy.create_sprite(x, c.candy_y);
            c.candy->set_bg_priority(2);
        }

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

        if(c.candy)
        {
            c.candy->set_x(c.x);
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
        int score = 0;       // gaps + candy canes
        int gaps = 0;        // gaps flown through (speed-ups and cities go by this)
        int level = 0;
        int last_gap_centre = -12;
        int flap_timer = 0;
        int columns_built = 0;
    };

    // The gap for the next column (every TIGHT_EVERY-th one is tighter).
    int next_gap(round_state& s)
    {
        ++s.columns_built;
        return s.columns_built % TIGHT_EVERY == 0 ? bn::max(s.gap - TIGHT_SHRINK, TIGHTEST_GAP) : s.gap;
    }

    constexpr int CITY_COUNT = 5;
    constexpr const char* CITY_NAMES[CITY_COUNT] = { "St. Louis", "Gainesville", "Seattle", "Bay Area", "Moorestown" };

    const bn::regular_bg_item& city_item(int city)
    {
        switch(city)
        {
        case 1:
            return bn::regular_bg_items::flappy_city_gnv;
        case 2:
            return bn::regular_bg_items::flappy_city_sea;
        case 3:
            return bn::regular_bg_items::flappy_city_sf;
        case 4:
            return bn::regular_bg_items::flappy_city_mtn;
        default:
            return bn::regular_bg_items::flappy_city_stl;
        }
    }

    // The city that came right after St. Louis last time (so the next game starts differently).
    int g_last_second_city = -1;

    // The order of the tour: St. Louis, then the others shuffled.
    struct tour_order
    {
        int cities[CITY_COUNT];
        int pos = 0;

        void shuffle()
        {
            // Stir the random numbers with the frame count (your timing), then shuffle. Never start two
            // tours in a row with the same second city, so it feels random.
            for(int k = sb::frame_count() % 13; k > 0; --k)
            {
                sb::rng().update();
            }

            for(int attempt = 0; attempt < 8; ++attempt)
            {
                for(int i = 0; i < CITY_COUNT; ++i)
                {
                    cities[i] = i;
                }

                for(int i = CITY_COUNT - 1; i > 1; --i)
                {
                    int j = sb::rand_range(1, i);
                    int t = cities[i];
                    cities[i] = cities[j];
                    cities[j] = t;
                }

                if(cities[1] != g_last_second_city)
                {
                    break;
                }
            }

            g_last_second_city = cities[1];
            pos = 0;
        }

        int current() const
        {
            return cities[pos];
        }

        int peek_next() const
        {
            return pos + 1 < CITY_COUNT ? cities[pos + 1] : 0;
        }

        void advance()
        {
            if(++pos >= CITY_COUNT)
            {
                shuffle();      // back to St. Louis, then a new order
            }
        }
    };

    int gap_centre_range_lo(int gap)
    {
        return -80 + MIN_CLOUD + gap / 2;
    }

    int gap_centre_range_hi(int gap)
    {
        return GROUND_Y - MIN_CHIMNEY - gap / 2;
    }

    int next_gap_centre(round_state& s, int gap)
    {
        int lo = gap_centre_range_lo(gap);
        int hi = gap_centre_range_hi(gap);
        int last = bn::max(lo, bn::min(hi, s.last_gap_centre));
        int up_room = bn::min(last - lo, MAX_GAP_SHIFT);       // how far the gap can move up ...
        int down_room = bn::min(hi - last, MAX_GAP_SHIFT);     // ... and down
        bool go_up = sb::rand_range(0, 1) == 0;

        if(go_up && up_room < MIN_GAP_SHIFT)
        {
            go_up = false;
        }
        else if(! go_up && down_room < MIN_GAP_SHIFT)
        {
            go_up = true;
        }

        int centre;

        if(go_up && up_room >= MIN_GAP_SHIFT)
        {
            centre = last - sb::rand_range(MIN_GAP_SHIFT, up_room);
        }
        else if(! go_up && down_room >= MIN_GAP_SHIFT)
        {
            centre = last + sb::rand_range(MIN_GAP_SHIFT, down_room);
        }
        else
        {
            centre = sb::rand_range(lo, hi);
        }

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
    // Shown on the start screen (scroll with Up/Down) and in the pause menu. '#' = heading.
    static constexpr bn::string_view howto[] = {
        "#Fly",
        "Press A (or Up) to flap.",
        "Your reindeer falls when",
        "you stop flapping.",
        "",
        "#Score",
        "Fly through the gaps",
        "between the chimneys and",
        "the icy clouds: 1 point",
        "for every gap.",
        "",
        "#World tour",
        "Start in St. Louis. Every",
        "10 gaps you fly to a new",
        "city, and it gets faster.",
        "Some gaps are extra",
        "tight: watch out!",
    };

    // ---- Scene
    bn::bg_palettes::set_transparent_color(bn::color(2, 3, 8));
    bn::regular_bg_ptr sky = bn::regular_bg_items::flappy_sky.create_bg(0, 0);
    sky.set_priority(3);
    bn::regular_bg_ptr town = bn::regular_bg_items::flappy_city_stl.create_bg(0, GROUND_SHIFT);
    town.set_priority(2);
    tour_order tour;
    int city_fade = 0;       // counts down while the skyline changes (fade out, swap, fade in)
    bn::regular_bg_ptr roofs = bn::regular_bg_items::flappy_roofs.create_bg(0, GROUND_SHIFT);
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
    tilt.set_scale(PLAYER_SCALE);
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
        bn::fixed ox = (c * RIDER_DX + s * RIDER_DY) * PLAYER_SCALE;
        bn::fixed oy = (c * RIDER_DY - s * RIDER_DX) * PLAYER_SCALE;
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
        score_label.set_visible(false);
        notice_label.set_visible(false);
        hint_label.set_visible(false);
        bool leave = game_pause();
        score_label.set_visible(true);
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
            int gap = next_gap(state);
            build_column(columns.back(), FIRST_COLUMN_X + i * COLUMN_SPACING, next_gap_centre(state, gap), gap);
            place_column(columns.back());
        }

        // Every round starts in St. Louis, with the Arch in view.
        tour.pos = 0;           // St. Louis now; the rest of the order is picked at the first flap
        tour.cities[0] = 0;
        city_fade = 0;
        town.set_item(city_item(0));
        town.set_blending_enabled(false);
        town_x = CITY_START_X;
        town.set_x(town_x);

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
                tour.shuffle();         // decided now, at the moment of your first flap
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
                    int gap = next_gap(state);
                    build_column(c, rightmost + COLUMN_SPACING, next_gap_centre(state, gap), gap);
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

                // Candy cane?
                if(c.candy && bn::abs(dx) < CANDY_REACH && bn::abs(py - c.candy_y) < CANDY_REACH)
                {
                    c.candy.reset();
                    state.score += CANDY_POINTS;
                    sfx(bn::sound_items::sfx_catch);
                    show_score();
                    notice_label.set(PLAYER_X + 10, py - 22, "+2", text_color::YELLOW, align::CENTER);
                    notice_timer = 30;
                }

                if(! c.scored && dx < 0)
                {
                    c.scored = true;
                    ++state.score;
                    ++state.gaps;
                    sfx(bn::sound_items::sfx_point);
                    show_score();

                    if(state.gaps % SPEEDUP_EVERY == 0)
                    {
                        // Next stop on the world tour.
                        city_fade = CITY_FADE_FRAMES * 2;
                        town.set_blending_enabled(true);
                        bn::string<32> stop("Next stop: ");
                        stop.append(CITY_NAMES[tour.peek_next()]);
                        notice_label.set(24, -56, stop, text_color::YELLOW, align::CENTER);
                        notice_timer = FASTER_NOTICE_FRAMES + 30;
                        bn::fixed faster = START_SCROLL + SCROLL_STEP * (state.level + 1);

                        if(faster <= MAX_SCROLL || state.gap > MIN_GAP)
                        {
                            ++state.level;
                            state.scroll = bn::min(faster, MAX_SCROLL);
                            state.gap = bn::max(START_GAP - GAP_STEP * state.level, MIN_GAP);
                            hint_label.set(24, -40, "Faster!", text_color::WHITE, align::CENTER);
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
                hint_label.clear();
            }

            // The skyline changes: fade the old city out, swap, fade the new one in.
            if(city_fade > 0)
            {
                --city_fade;

                if(city_fade == CITY_FADE_FRAMES)
                {
                    tour.advance();
                    town.set_item(city_item(tour.current()));
                    town_x = CITY_ENTER_X;
                    town.set_x(town_x);
                }

                bn::fixed alpha = city_fade > CITY_FADE_FRAMES ?
                                  bn::fixed(city_fade - CITY_FADE_FRAMES) / CITY_FADE_FRAMES :
                                  bn::fixed(CITY_FADE_FRAMES - city_fade) / CITY_FADE_FRAMES;
                bn::blending::set_transparency_alpha(alpha);

                if(city_fade == 0)
                {
                    town.set_blending_enabled(false);
                }
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
        hint_label.clear();     // e.g. "Faster!" (its timer stops counting once you crash)
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
