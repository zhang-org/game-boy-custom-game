// Sled Dash: a downhill time trial on the Big Hill.
//
// Pick a course (Easy, Medium or Hard). Every course is the same each time you ride it, so you can learn it
// and beat your best time. Steer through the slalom gates (a missed gate adds MISSED_GATE_PENALTY), hit the
// snow ramps to fly over logs, and dodge trees, rocks and snowmen (a crash costs you speed). Hold B to brake
// and carve (slower, sharper turns). (There used to be a "tuck" on A: too fast to dodge anything.)
//
// View: top-down 3/4. The sled stays in the upper part of the screen and the course scrolls UP past it.
// Everything is drawn at ZOOM (80%) so you can see further down the hill. All the game logic works in
// unzoomed "course" coordinates (as before the zoom); only draw_x() / draw_y() scale things for the screen.
// Records: the best time on each course (sb_save extra records SLED_RECORD_SLOT + course).
// Ghost: your best run on each course is saved too (SRAM from GHOST_SRAM_OFFSET, see
// docs/decisions/0003-sram-map.md) and rides along as a see-through sled.
//
// Art: tools/art/make_sled.py -> graphics/sled/. Music: sled_song.
#include "bn_bg_palettes.h"
#include "bn_blending.h"
#include "bn_color.h"
#include "bn_sprite_affine_mat_ptr.h"
#include "bn_sram.h"
#include "bn_unique_ptr.h"
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
#include "bn_regular_bg_items_sled_hill.h"
#include "bn_sound_items.h"
#include "bn_sprite_items_sled_banner.h"
#include "bn_sprite_items_sled_course.h"
#include "bn_sprite_items_sled_fx.h"
#include "bn_sprite_items_sled_obstacles.h"
#include "bn_sprite_items_sled_puff.h"
#include "bn_sprite_items_sled_ramp.h"
#include "bn_sprite_items_sled_sled.h"
#include "bn_sprite_items_sled_track.h"

#include "sb_audio.h"
#include "sb_characters.h"
#include "sb_core.h"
#include "sb_games.h"
#include "sb_save.h"
#include "sb_snow.h"
#include "sb_text.h"
#include "sb_ui.h"

namespace
{
    // ---------------------------------------------------------------------------- tuning
    // Speeds are in pixels per frame (60 frames per second). Distances along the course are in pixels.

    // The three courses. Everything about how hard a course is lives in one row.
    struct course_def
    {
        const char* name;
        int length;             // pixels from the start to the finish line
        int gate_min, gate_max; // distance between gates
        int gate_half;          // half the width of a gate (between the poles)
        int swing_min, swing_max; // how far left/right of the middle the gates sit
        int ramp_every;         // a ramp (with a log after it) every this many gates
        int obstacles;          // obstacles between two gates (plus 0..1 random)
        int clearance;          // obstacles stay at least this far from the line between two gates
        bn::fixed cruise;       // normal top speed
        unsigned seed;          // the course layout (same every time)
    };

    constexpr course_def COURSES[3] = {
        //  name      length  gates      half  swing     ramp obst clear cruise seed
        { "Easy",   4200,   280, 340,  29,   22, 44,   6,   1,   28,   2.0,  1201 },
        { "Medium", 5300,   240, 300,  24,   30, 56,   4,   2,   22,   2.35, 7703 },
        { "Hard",   6400,   200, 260,  20,   36, 66,   3,   3,   18,   2.7,  4419 },
    };
    constexpr int COURSE_COUNT = 3;

    constexpr bn::fixed BRAKE_SPEED = 0.5;      // hold B: top speed x this ...
    constexpr bn::fixed BRAKE_STEER = 1.4;      // ... and you turn sharper
    constexpr bn::fixed SPEED_EASE_UP = bn::fixed(1) / 50;   // how quickly you pick up speed
    constexpr bn::fixed SPEED_EASE_DOWN = bn::fixed(1) / 16; // ... and lose it

    constexpr int MISSED_GATE_PENALTY = 120;    // frames (2 seconds) added for every missed gate
    constexpr int AIR_FRAMES = 42;              // time in the air after a ramp
    constexpr int AIR_HEIGHT = 16;              // how high the sled flies
    constexpr bn::fixed RAMP_BONUS = 0.5;       // extra speed when you land
    constexpr int RAMP_HALF = 20;               // hit the ramp within this many pixels of its middle (the art
                                                // in make_sled.py: lip 36 px wide, foot 44 px)
    constexpr int GATE_MARGIN = 0;              // the sled's middle must be this far inside the poles

    // Steering (sideways) physics: a little momentum so it feels like sliding on snow.
    constexpr bn::fixed STEER_ACCEL = bn::fixed(3) / 20;
    constexpr bn::fixed STEER_COUNTER_ACCEL = bn::fixed(1) / 4;
    constexpr bn::fixed STEER_MAX = 2;
    constexpr bn::fixed STEER_FRICTION = bn::fixed(9) / 10;
    constexpr bn::fixed LEAN_THRESHOLD = bn::fixed(6) / 10;

    // Crashing into a tree, rock, snowman or log.
    constexpr int CRASH_FRAMES = 60;                          // blinking time after a crash
    constexpr bn::fixed CRASH_SPEED_FACTOR = bn::fixed(1) / 4; // speed kept right after a crash
    constexpr bn::fixed CRASH_BUMP = bn::fixed(3) / 2;         // sideways push away from what you hit

    // Where the sled rides and how far it can go sideways (course coordinates: the y of things on the course
    // is SLED_Y + their distance below the sled).
    constexpr int SLED_Y = -36;
    constexpr int SLED_X_LIMIT = 84;
    constexpr int START_CLEAR = 220;            // pixels of open hill before the first gate
    constexpr int SPAWN_AHEAD = 200;            // things appear this far below the sled (just off screen)
    constexpr int DESPAWN_Y = -104;
    constexpr int FINISH_COAST_FRAMES = 80;     // gliding to a stop after the finish line

    // Zoomed out: the course is drawn at ZOOM, with the sled at screen y SLED_DRAW_Y. At 80% with the sled
    // 16 px higher you see ~165 px of hill ahead instead of 116.
    constexpr bn::fixed ZOOM = 0.8;
    constexpr int SLED_DRAW_Y = -52;

    bn::fixed draw_x(bn::fixed x)
    {
        return x * ZOOM;
    }

    bn::fixed draw_y(bn::fixed y)
    {
        return SLED_DRAW_Y + (y - SLED_Y) * ZOOM;
    }

    // The ghost: your best run on each course, a sample every GHOST_EVERY frames (up to GHOST_MAX samples,
    // ~100 s), saved in SRAM after the main save (sb_save uses bytes 0..2047).
    constexpr int GHOST_EVERY = 4;
    constexpr int GHOST_MAX = 1536;
    constexpr int GHOST_SRAM_OFFSET = 4096;
    constexpr int GHOST_SRAM_STRIDE = 4864;     // bytes per course
    constexpr bn::fixed GHOST_ALPHA = 0.5;

    struct ghost_run
    {
        char tag[8];                // "SBGHOST1"
        int course;
        int character;
        int count;                  // samples used
        signed char x[GHOST_MAX];   // sled x
        unsigned short d[GHOST_MAX]; // distance travelled
        unsigned checksum;
    };

    static_assert(sizeof(ghost_run) <= GHOST_SRAM_STRIDE);
    static_assert(GHOST_SRAM_OFFSET + 3 * GHOST_SRAM_STRIDE <= 32 * 1024);

    constexpr char GHOST_TAG[8] = { 'S', 'B', 'G', 'H', 'O', 'S', 'T', '1' };

    unsigned ghost_checksum(const ghost_run& g)
    {
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&g);
        unsigned sum = 0x5eed;

        for(int i = 0; i < int(sizeof(ghost_run) - sizeof(unsigned)); ++i)
        {
            sum = (sum << 5) + sum + bytes[i];
        }

        return sum;
    }

    // Pools.
    constexpr int OBJECT_POOL = 32;
    constexpr int TRACK_POOL = 14;
    constexpr int PUFF_POOL = 8;
    constexpr int SNOWFLAKES = 12;
    constexpr int TRACK_STEP = 6;
    constexpr int MAX_ITEMS = 300;              // gates, obstacles, ramps, logs and the finish (Hard uses ~150).
                                                // The trees along the edges are not stored: see edge_trees.

    // Records: sb_save extra record slots SLED_RECORD_SLOT .. +2 (Easy, Medium, Hard).
    constexpr int SLED_RECORD_SLOT = 3;
    constexpr int SLED_RECORD_BASE = 1000000;   // record value = this - time in hundredths (higher = better)

    // ------------------------------------------------------------------------ art layout
    constexpr int SPRITE_PRIORITY = 2;
    constexpr int TRACK_PRIORITY = 3;
    constexpr int BG_PRIORITY = 3;

    enum item_type
    {
        IT_PINE, IT_SNOWY_PINE, IT_ROCK, IT_ROCKS, IT_SNOWMAN,      // = frames in sled_obstacles
        IT_GATE, IT_RAMP, IT_LOG, IT_FINISH
    };

    enum course_frame { CF_GATE_RED = 0, CF_GATE_BLUE, CF_LOG_L, CF_LOG_R, CF_SHADOW };

    struct hitbox
    {
        int half_w, top, bottom;
    };

    constexpr hitbox obstacle_boxes[5] = {
        { 7, 5, 13 },   // pine
        { 7, 5, 13 },   // snowy pine
        { 9, 1, 12 },   // rock
        { 12, 1, 12 },  // two rocks
        { 6, 2, 12 },   // snowman
    };
    constexpr hitbox log_box = { 29, -6, 12 };
    constexpr hitbox sled_box = { 8, 4, 13 };
    constexpr int FOOT_OFFSET = 12;
    constexpr int SLED_FOOT_OFFSET = 12;
    constexpr int RIDER_DY = -4;

    // Depth: only a few z values (Butano has 16 sprite sort layers in total).
    constexpr int Z_FX = -3;
    constexpr int Z_FRONT = -2;
    constexpr int Z_RIDER = -1;
    constexpr int Z_SLED = 0;
    constexpr int Z_BEHIND = 1;

    int z_for(int foot_y)
    {
        return foot_y > SLED_Y + SLED_FOOT_OFFSET ? Z_FRONT : Z_BEHIND;
    }

    // One thing on the course, at distance d from the start.
    struct course_item
    {
        int d;
        short x;
        char type;
        char variant;   // gate colour, obstacle flip
    };

    // A tiny random generator just for laying out courses, so each course is the same every time.
    struct course_random
    {
        unsigned state;

        int range(int lo, int hi)
        {
            state = state * 1103515245u + 12345u;
            return lo + int((state >> 16) % unsigned(hi - lo + 1));
        }
    };

    // The trees along both edges of the course, made a pair at a time as the sled gets near them.
    struct edge_trees
    {
        static constexpr int FIRST = 60;

        course_random rnd;
        int next_d = FIRST;             // distance of the next pair
        int end_d = 0;
        course_item pending[2];
        int pending_count = 0;
        int pending_index = 0;

        void reset(const course_def& c)
        {
            rnd = { c.seed };
            next_d = FIRST;
            end_d = c.length + 120;
            pending_count = 0;
            pending_index = 0;
        }

        // The next tree, if it is within `ahead` of distance `travel`.
        bool next(int travel, int ahead, course_item& out)
        {
            if(pending_index == pending_count)
            {
                if(next_d >= end_d)
                {
                    return false;
                }

                // 7 random numbers per pair, then 1 for the gap to the next pair (see skip()).
                // (Further out than before the zoom, so they line the screen's edges. Out of reach of the
                // sled: SLED_X_LIMIT + half a sled < 100 - half a tree. The ranges don't change how many
                // random numbers are used, so the courses keep their layout.)
                int left_x = -rnd.range(100, 138);
                int left_type = rnd.range(0, 1) ? IT_PINE : IT_SNOWY_PINE;
                int left_flip = rnd.range(0, 1);
                int right_d = next_d + rnd.range(10, 30);
                int right_x = rnd.range(100, 138);
                int right_type = rnd.range(0, 1) ? IT_PINE : IT_SNOWY_PINE;
                int right_flip = rnd.range(0, 1);
                pending[0] = { next_d, short(left_x), char(left_type), char(left_flip) };
                pending[1] = { right_d, short(right_x), char(right_type), char(right_flip) };
                pending_count = 2;
                pending_index = 0;
                next_d += rnd.range(36, 64);
            }

            if(pending[pending_index].d - travel > ahead)
            {
                return false;
            }

            out = pending[pending_index++];
            return true;
        }

        // Moves `rnd` past the random numbers the edge trees use (the course is laid out after them).
        static void skip(const course_def& c, course_random& rnd)
        {
            for(int d = FIRST; d < c.length + 120; d += rnd.range(36, 64))
            {
                for(int i = 0; i < 7; ++i)
                {
                    rnd.range(0, 1);
                }
            }
        }
    };

    void build_course(int course, bn::vector<course_item, MAX_ITEMS>& items)
    {
        const course_def& c = COURSES[course];
        course_random rnd = { c.seed };
        items.clear();

        auto add = [&](int d, int x, int type, int variant) {
            BN_ASSERT(! items.full(), "Sled course ", course, " has more than MAX_ITEMS items");
            items.push_back({ d, short(x), char(type), char(variant) });
        };

        auto pick_obstacle = [&]() {
            int roll = rnd.range(0, 99);
            return roll < 42 ? IT_PINE : roll < 62 ? IT_SNOWY_PINE : roll < 82 ? IT_ROCK : roll < 91 ? IT_ROCKS : IT_SNOWMAN;
        };

        int d = START_CLEAR;
        int x = 0;
        int gate = 0;
        int until_ramp = c.ramp_every;

        // The trees along both edges are made while you ride (edge_trees), not stored: there are ~250 of
        // them on Hard, which used to fill the list and cut the course off (no finish line). Skip the same
        // random numbers here so every course keeps the layout it had before.
        edge_trees::skip(c, rnd);

        while(d < c.length - 160)
        {
            int side = gate % 2 == 0 ? 1 : -1;
            int nx = side * rnd.range(c.swing_min, c.swing_max);
            add(d, nx, IT_GATE, gate % 2);
            ++gate;

            int nd = d + rnd.range(c.gate_min, c.gate_max);

            if(nd >= c.length - 160)
            {
                break;
            }

            int nx2 = -side * rnd.range(c.swing_min, c.swing_max);

            auto line_x = [&](int dd) {
                return nx + (nx2 - nx) * (dd - d) / (nd - d);
            };

            // Sometimes a ramp on the racing line, and a log across the line right after it.
            bool ramp_here = --until_ramp == 0;

            if(ramp_here)
            {
                until_ramp = c.ramp_every;
                int rd = d + (nd - d) * 30 / 100;
                add(rd, line_x(rd), IT_RAMP, 0);
                int ld = rd + 76;
                add(ld, line_x(ld), IT_LOG, 0);
            }

            // Obstacles off the racing line.
            int count = c.obstacles + rnd.range(0, 1);

            for(int i = 0; i < count; ++i)
            {
                for(int attempt = 0; attempt < 10; ++attempt)
                {
                    int od = rnd.range(d + 50, nd - 50);
                    int ox = rnd.range(-80, 80);

                    if(bn::abs(ox - line_x(od)) < c.clearance + 14)
                    {
                        continue;
                    }

                    if(ramp_here && od > d + (nd - d) * 20 / 100 && od < d + (nd - d) * 30 / 100 + 110)
                    {
                        continue;   // keep the ramp and its landing clear
                    }

                    add(od, ox, pick_obstacle(), rnd.range(0, 1));
                    break;
                }
            }

            d = nd;
            x = nx2;
        }

        (void) x;
        add(c.length, 0, IT_FINISH, 0);
        BN_LOG("Sled course ", course, ": ", items.size(), " items, ", gate, " gates");

        // Sort by distance (insertion sort: a few hundred items, once per run).
        for(int i = 1; i < items.size(); ++i)
        {
            course_item key = items[i];
            int j = i - 1;

            while(j >= 0 && items[j].d > key.d)
            {
                items[j + 1] = items[j];
                --j;
            }

            items[j + 1] = key;
        }

        BN_ASSERT(items.back().type == IT_FINISH, "Sled course ", course, " doesn't end with the finish line");
    }

    int count_gates(const bn::vector<course_item, MAX_ITEMS>& items)
    {
        int n = 0;

        for(const course_item& it : items)
        {
            n += it.type == IT_GATE;
        }

        return n;
    }

    // "41.3" (seconds with one decimal) from hundredths of a second.
    bn::string<12> time_text(int hundredths)
    {
        bn::string<12> text(sb::to_text(hundredths / 100));
        text.append(".");
        text.append(sb::to_text((hundredths / 10) % 10));
        return text;
    }

    int frames_to_hundredths(int frames)
    {
        return frames * 100 / 60;
    }

    struct world_obj
    {
        bn::optional<bn::sprite_ptr> a, b;
        course_item it = {};    // a copy (edge trees are not in the course list)
        bool active = false;
        bn::fixed y;
        bool checked = false;   // gates and ramps: already passed the sled
    };

    struct track
    {
        bn::sprite_ptr sprite;
        bn::fixed y;
        bool active = false;
    };

    struct puff
    {
        bn::sprite_ptr sprite;
        bn::fixed x, y, dx;
        int age = 0;
        bool active = false;
    };

    class sled_game
    {
    public:
        sled_game() :
            _bg(bn::regular_bg_items::sled_hill.create_bg(0, 0)),
            _sled(bn::sprite_items::sled_sled.create_sprite(0, SLED_Y, 0)),
            _rider(sb::character_sprite(sb::player_character()).create_sprite(0, SLED_Y + RIDER_DY, sb::FRAME_SIT)),
            _shadow(bn::sprite_items::sled_course.create_sprite(0, SLED_Y, CF_SHADOW)),
            _fx(bn::sprite_items::sled_fx.create_sprite(0, 0, 0)),
            _ghost_sled(bn::sprite_items::sled_sled.create_sprite(0, 200, 0)),
            _zoom(bn::sprite_affine_mat_ptr::create()),
            _zoom_flipped(bn::sprite_affine_mat_ptr::create()),
            _snow(SNOWFLAKES, 1),
            _ghost(bn::make_unique<ghost_run>()),
            _recording(bn::make_unique<ghost_run>())
        {
            _zoom.set_scale(ZOOM);
            _zoom_flipped.set_scale(ZOOM);
            _zoom_flipped.set_horizontal_flip(true);
            _bg.set_priority(BG_PRIORITY);
            _sled.set_bg_priority(SPRITE_PRIORITY);
            _sled.set_affine_mat(_zoom);
            _rider.set_bg_priority(SPRITE_PRIORITY);
            _rider.set_affine_mat(_zoom);
            _shadow.set_bg_priority(SPRITE_PRIORITY);
            _shadow.set_affine_mat(_zoom);
            _shadow.set_z_order(Z_BEHIND);
            _shadow.set_visible(false);
            _fx.set_bg_priority(SPRITE_PRIORITY);
            _fx.set_affine_mat(_zoom);
            _fx.set_visible(false);
            _ghost_sled.set_bg_priority(SPRITE_PRIORITY);
            _ghost_sled.set_affine_mat(_zoom);
            _ghost_sled.set_z_order(Z_BEHIND);
            _ghost_sled.set_blending_enabled(true);
            _ghost_sled.set_visible(false);

            for(int i = 0; i < OBJECT_POOL; ++i)
            {
                _objects.push_back(world_obj());
            }

            for(int i = 0; i < TRACK_POOL; ++i)
            {
                bn::sprite_ptr sprite = bn::sprite_items::sled_track.create_sprite(0, 0, 0);
                sprite.set_bg_priority(TRACK_PRIORITY);
                sprite.set_affine_mat(_zoom);
                sprite.set_visible(false);
                _tracks.push_back({ sprite, 0, false });
            }

            for(int i = 0; i < PUFF_POOL; ++i)
            {
                bn::sprite_ptr sprite = bn::sprite_items::sled_puff.create_sprite(0, 0, 0);
                sprite.set_bg_priority(SPRITE_PRIORITY);
                sprite.set_affine_mat(_zoom);
                sprite.set_visible(false);
                _puffs.push_back({ sprite, 0, 0, 0, 0, false });
            }
        }

        void reset(int course)
        {
            _course = course;
            build_course(course, _items);
            _total_gates = count_gates(_items);
            _next_item = 0;
            _edge_waiting = false;
            _x = 0;
            _vx = 0;
            _speed = 0;
            _travel = 0;
            _frames = 0;
            _time_frames = 0;
            _penalty_frames = 0;
            _gates_passed = 0;
            _gates_missed = 0;
            _crash = 0;
            _air = 0;
            _countdown = 3 * COUNT_STEP;
            _finished = false;
            _coast = 0;
            _fx_timer = 0;
            _popup_timer = 0;
            _bg_y = 0;
            _next_track = TRACK_STEP;

            for(world_obj& o : _objects)
            {
                o.a.reset();
                o.b.reset();
                o.active = false;
            }

            _edges.reset(def());

            for(track& t : _tracks)
            {
                t.active = false;
                t.sprite.set_visible(false);
            }

            for(puff& p : _puffs)
            {
                p.active = false;
                p.sprite.set_visible(false);
            }

            _fx.set_visible(false);
            _shadow.set_visible(false);
            _popup.clear();
            _count_label.clear();
            _bg.set_y(0);
            load_ghost();
            _recording->count = 1;
            _recording->x[0] = 0;
            _recording->d[0] = 0;
            spawn_items();
            place_sled();
            place_ghost();
        }

        // Saves this run as the course's ghost (call when it set a new record).
        void save_ghost()
        {
            ghost_run& g = *_recording;

            for(int i = 0; i < 8; ++i)
            {
                g.tag[i] = GHOST_TAG[i];
            }

            g.course = _course;
            g.character = sb::player_character();
            g.checksum = ghost_checksum(g);
            bn::sram::write_offset(g, GHOST_SRAM_OFFSET + _course * GHOST_SRAM_STRIDE);
        }

        // Total time in hundredths of a second (riding time + penalties).
        int result_hundredths() const
        {
            return frames_to_hundredths(_time_frames + _penalty_frames);
        }

        int gates_missed() const
        {
            return _gates_missed;
        }

        int total_gates() const
        {
            return _total_gates;
        }

        void hide_hud()
        {
            _time_label.clear();
            _gates_label.clear();
            _popup.clear();
            _count_label.clear();
        }

        void set_hud_visible(bool visible)
        {
            _time_label.set_visible(visible);
            _gates_label.set_visible(visible);
            _popup.set_visible(visible);
            _count_label.set_visible(visible);
        }

        // One frame. Returns true when the run is over (finished and stopped).
        bool step()
        {
            ++_frames;

            if(_countdown > 0)
            {
                update_countdown();
                _snow.update();
                return false;
            }

            if(_finished)
            {
                _speed = _speed * bn::fixed(96) / 100;
                _vx = _vx * bn::fixed(9) / 10;
                scroll_world();
                update_fx();
                _snow.update(0, -_speed / 2 * ZOOM);
                place_sled();
                place_ghost();
                return --_coast <= 0;
            }

            ++_time_frames;
            update_speed();
            steer();
            scroll_world();
            record_ghost();
            spawn_items();
            spawn_tracks();
            check_course();
            update_fx();
            _snow.update(0, -_speed * bn::fixed(7) / 10 * ZOOM);
            place_sled();
            place_ghost();
            update_hud();
            return false;
        }

    private:
        static constexpr int COUNT_STEP = 36;

        bn::regular_bg_ptr _bg;
        bn::sprite_ptr _sled;
        bn::sprite_ptr _rider;
        bn::sprite_ptr _shadow;
        bn::sprite_ptr _fx;
        bn::sprite_ptr _ghost_sled;
        bn::sprite_affine_mat_ptr _zoom;
        bn::sprite_affine_mat_ptr _zoom_flipped;
        sb::snowfall _snow;
        bn::unique_ptr<ghost_run> _ghost;       // the best run on this course (count 0 = none yet)
        bn::unique_ptr<ghost_run> _recording;   // this run
        bn::optional<bn::sprite_ptr> _ghost_rider;
        bn::fixed _fx_x, _fx_y;
        bn::vector<course_item, MAX_ITEMS> _items;
        edge_trees _edges;
        course_item _edge_item = {};
        bool _edge_waiting = false;
        bn::vector<world_obj, OBJECT_POOL> _objects;
        bn::vector<track, TRACK_POOL> _tracks;
        bn::vector<puff, PUFF_POOL> _puffs;
        sb::label _time_label, _gates_label, _popup, _count_label;

        int _course = 0;
        int _next_item = 0;
        int _total_gates = 0;
        bn::fixed _x, _vx, _speed, _travel, _bg_y, _next_track;
        int _frames = 0;
        int _time_frames = 0;
        int _penalty_frames = 0;
        int _gates_passed = 0;
        int _gates_missed = 0;
        int _crash = 0;
        int _air = 0;
        int _countdown = 0;
        bool _finished = false;
        int _coast = 0;
        int _fx_timer = 0;
        int _fx_frame = 0;
        int _popup_timer = 0;
        int _shown_tenths = -1;
        int _shown_gates = -1;

        const course_def& def() const
        {
            return COURSES[_course];
        }

        // ------------------------------------------------------------------- per frame
        void update_countdown()
        {
            if(_countdown % COUNT_STEP == 0)
            {
                _count_label.set(0, 8, sb::to_text(_countdown / COUNT_STEP), sb::text_color::YELLOW, sb::align::CENTER);
                sb::sfx(bn::sound_items::sfx_countdown);
            }

            --_countdown;

            if(_countdown == 0)
            {
                _count_label.set(0, 8, "GO!", sb::text_color::YELLOW, sb::align::CENTER);
                sb::sfx(bn::sound_items::sfx_go);
                _speed = def().cruise / 2;
                _shown_tenths = -1;
                _shown_gates = -1;
                update_hud();
            }
        }

        bool braking() const
        {
            return bn::keypad::b_held();
        }

        void update_speed()
        {
            if(_time_frames == 40)
            {
                _count_label.clear();
            }

            if(_crash > 0)
            {
                --_crash;
            }

            bn::fixed target = def().cruise;

            if(braking())
            {
                target *= BRAKE_SPEED;
            }

            if(_speed < target)
            {
                _speed = bn::min(target, _speed + SPEED_EASE_UP);
            }
            else if(_air == 0)
            {
                _speed = bn::max(target, _speed - SPEED_EASE_DOWN);
            }

            // Spray when braking.
            if(braking() && _speed > 1 && _frames % 5 == 0 && _air == 0)
            {
                spawn_puff(_x - 11, -1);
                spawn_puff(_x + 11, 1);
            }
        }

        void steer()
        {
            bool left = bn::keypad::left_held();
            bool right = bn::keypad::right_held();

#ifdef SLED_AUTOPILOT
            // Test builds only (make USERFLAGS=-DSLED_AUTOPILOT): steers for the middle of the next gate or
            // ramp, so the harness can ride a whole course. It does not dodge obstacles.
            for(int i = 0; i < _items.size(); ++i)
            {
                const course_item& it = _items[i];

                if((it.type == IT_GATE || it.type == IT_RAMP || it.type == IT_FINISH) && it.d > _travel + 4)
                {
                    bn::fixed ahead = _x + _vx * 8;     // where the slide takes us
                    left = ahead > it.x + 2;
                    right = ahead < it.x - 2;
                    break;
                }
            }
#endif
            bn::fixed grip = braking() ? BRAKE_STEER : bn::fixed(1);

            if(_air > 0)
            {
                grip = grip / 3;    // hardly any steering in the air
            }

            if(left && ! right)
            {
                _vx -= (_vx > 0 ? STEER_COUNTER_ACCEL : STEER_ACCEL) * grip;
            }
            else if(right && ! left)
            {
                _vx += (_vx < 0 ? STEER_COUNTER_ACCEL : STEER_ACCEL) * grip;
            }
            else if(_air == 0)
            {
                _vx *= STEER_FRICTION;

                if(bn::abs(_vx) < bn::fixed(1) / 32)
                {
                    _vx = 0;
                }
            }

            bn::fixed max = STEER_MAX * (braking() ? BRAKE_STEER : bn::fixed(1));
            _vx = bn::clamp(_vx, -max, max);
            _x += _vx;

            if(_x < -SLED_X_LIMIT)
            {
                _x = -SLED_X_LIMIT;
                _vx = 0;
            }
            else if(_x > SLED_X_LIMIT)
            {
                _x = SLED_X_LIMIT;
                _vx = 0;
            }

            if((left || right) && bn::abs(_vx) > 1 && _frames % 4 == 0 && _air == 0)
            {
                int side = _vx < 0 ? 1 : -1;
                spawn_puff(_x + side * 11, side);
            }
        }

        // Screen y of something at course distance d.
        bn::fixed screen_y(int d) const
        {
            return bn::fixed(SLED_Y) + (bn::fixed(d) - _travel);
        }

        void scroll_world()
        {
            _travel += _speed;
            _bg_y -= _speed * ZOOM;

            if(_bg_y < -256)
            {
                _bg_y += 256;
            }

            _bg.set_y(_bg_y);

            for(world_obj& o : _objects)
            {
                if(! o.active)
                {
                    continue;
                }

                const course_item& it = o.it;
                o.y = screen_y(it.d);

                if(o.y < DESPAWN_Y)
                {
                    o.a.reset();
                    o.b.reset();
                    o.active = false;
                    continue;
                }

                place_object(o, it);
            }

            for(track& t : _tracks)
            {
                if(! t.active)
                {
                    continue;
                }

                t.y -= _speed;

                if(t.y < -88)
                {
                    t.active = false;
                    t.sprite.set_visible(false);
                    continue;
                }

                t.sprite.set_y(draw_y(t.y));
            }

            for(puff& p : _puffs)
            {
                if(! p.active)
                {
                    continue;
                }

                ++p.age;

                if(p.age >= 18)
                {
                    p.active = false;
                    p.sprite.set_visible(false);
                    continue;
                }

                p.x += p.dx;
                p.y -= _speed - bn::fixed(1) / 4;
                p.sprite.set_position(draw_x(p.x), draw_y(p.y));
                p.sprite.set_tiles(bn::sprite_items::sled_puff.tiles_item(), p.age / 6);
            }

            _next_track -= _speed;
        }

        void place_object(world_obj& o, const course_item& it)
        {
            int z = z_for(o.y.integer() + FOOT_OFFSET);

            switch(it.type)
            {
            case IT_GATE:
                // Two poles, gate_half to the left and right of the middle.
                o.a->set_position(draw_x(it.x - def().gate_half), draw_y(o.y));
                o.b->set_position(draw_x(it.x + def().gate_half), draw_y(o.y));
                break;
            case IT_LOG:
                o.a->set_position(draw_x(it.x - 16), draw_y(o.y));
                o.b->set_position(draw_x(it.x + 16), draw_y(o.y));
                break;
            case IT_FINISH:
                o.a->set_position(draw_x(-32), draw_y(o.y));
                o.b->set_position(draw_x(32), draw_y(o.y));
                z = Z_FRONT;
                break;
            case IT_RAMP:
                o.a->set_position(draw_x(it.x), draw_y(o.y));
                z = Z_BEHIND;   // you ride on top of it
                break;
            default:
                o.a->set_position(draw_x(it.x), draw_y(o.y));
                break;
            }

            o.a->set_z_order(z);

            if(o.b)
            {
                o.b->set_z_order(z);
            }
        }

        bn::sprite_ptr make_sprite(const bn::sprite_item& item, int frame)
        {
            bn::sprite_ptr sprite = item.create_sprite(0, 200, frame);
            sprite.set_bg_priority(SPRITE_PRIORITY);
            sprite.set_affine_mat(_zoom);
            return sprite;
        }

        void spawn_items()
        {
            int travel = _travel.integer();

            while(_next_item < _items.size() && _items[_next_item].d - travel <= SPAWN_AHEAD)
            {
                if(! spawn(_items[_next_item]))
                {
                    return;     // no free slot: try again next frame
                }

                ++_next_item;
            }

            while(true)
            {
                if(! _edge_waiting)
                {
                    if(! _edges.next(travel, SPAWN_AHEAD, _edge_item))
                    {
                        break;
                    }

                    _edge_waiting = true;
                }

                if(! spawn(_edge_item))
                {
                    break;
                }

                _edge_waiting = false;
            }
        }

        // Puts a course item on screen. Returns false if all the object slots are in use.
        bool spawn(const course_item& it)
        {
            world_obj* slot = nullptr;

            for(world_obj& o : _objects)
            {
                if(! o.active)
                {
                    slot = &o;
                    break;
                }
            }

            if(! slot)
            {
                return false;
            }

            {
                slot->it = it;
                slot->active = true;
                slot->checked = false;

                switch(it.type)
                {
                case IT_GATE:
                {
                    int frame = it.variant ? CF_GATE_BLUE : CF_GATE_RED;
                    slot->a = make_sprite(bn::sprite_items::sled_course, frame);
                    slot->b = make_sprite(bn::sprite_items::sled_course, frame);
                    break;
                }
                case IT_RAMP:
                    slot->a = make_sprite(bn::sprite_items::sled_ramp, 0);
                    break;
                case IT_LOG:
                    slot->a = make_sprite(bn::sprite_items::sled_course, CF_LOG_L);
                    slot->b = make_sprite(bn::sprite_items::sled_course, CF_LOG_R);
                    break;
                case IT_FINISH:
                    slot->a = make_sprite(bn::sprite_items::sled_banner, 0);
                    slot->b = make_sprite(bn::sprite_items::sled_banner, 1);
                    break;
                default:
                    slot->a = make_sprite(bn::sprite_items::sled_obstacles, it.type);
                    // (Affine sprites flip with their matrix, not set_horizontal_flip().)
                    if(it.type != IT_SNOWMAN && it.variant)
                    {
                        slot->a->set_affine_mat(_zoom_flipped);
                    }
                    break;
                }

                slot->y = screen_y(it.d);
                place_object(*slot, it);
            }

            return true;
        }

        void spawn_puff(bn::fixed x, int direction)
        {
            for(puff& p : _puffs)
            {
                if(! p.active)
                {
                    p.active = true;
                    p.x = x;
                    p.y = SLED_Y + 10;
                    p.dx = bn::fixed(direction) * (bn::fixed(1) / 2 + bn::fixed(sb::rand_range(0, 8)) / 16);
                    p.age = 0;
                    p.sprite.set_position(draw_x(p.x), draw_y(p.y));
                    p.sprite.set_tiles(bn::sprite_items::sled_puff.tiles_item(), 0);
                    p.sprite.set_z_order(Z_BEHIND);
                    p.sprite.set_visible(true);
                    return;
                }
            }
        }

        void spawn_tracks()
        {
            if(_next_track > 0)
            {
                return;
            }

            _next_track += TRACK_STEP;

            if(_air > 0)
            {
                return;     // no tracks while flying
            }

            for(track& t : _tracks)
            {
                if(! t.active)
                {
                    t.active = true;
                    t.y = SLED_Y + 10;
                    bn::fixed slope = _speed > 0 ? _vx / _speed : bn::fixed(0);
                    int frame = slope < bn::fixed(-3) / 10 ? 1 : slope > bn::fixed(3) / 10 ? 2 : 0;
                    t.sprite.set_tiles(bn::sprite_items::sled_track.tiles_item(), frame);
                    t.sprite.set_position(draw_x(_x), draw_y(t.y));
                    t.sprite.set_visible(true);
                    return;
                }
            }
        }

        void popup(const bn::string_view& text, sb::text_color color)
        {
            _popup.set(0, -24, text, color, sb::align::CENTER);    // just below the sled (it rides at SLED_DRAW_Y)
            _popup_timer = 50;
        }

        // ----------------------------------------------------------------- the course
        void check_course()
        {
            if(_air > 0 && --_air == 0)
            {
                // Landed: a burst of speed.
                _speed += RAMP_BONUS;
                sb::sfx(bn::sound_items::sfx_land);
                spawn_puff(_x - 10, -1);
                spawn_puff(_x + 10, 1);
            }

            int sl = (_x - sled_box.half_w).integer();
            int sr = (_x + sled_box.half_w).integer();
            int st = SLED_Y + sled_box.top;
            int sbottom = SLED_Y + sled_box.bottom;

            for(world_obj& o : _objects)
            {
                if(! o.active)
                {
                    continue;
                }

                const course_item& it = o.it;
                int oy = o.y.integer();

                switch(it.type)
                {
                case IT_GATE:
                    // Judged when the poles reach the sled.
                    if(! o.checked && oy <= SLED_Y + 6)
                    {
                        o.checked = true;

                        if(bn::abs(_x - it.x) < def().gate_half - GATE_MARGIN)
                        {
                            ++_gates_passed;
                            sb::sfx(bn::sound_items::sfx_select);
                        }
                        else
                        {
                            ++_gates_missed;
                            _penalty_frames += MISSED_GATE_PENALTY;
                            sb::sfx(bn::sound_items::sfx_miss);
                            popup("Missed gate! +2s", sb::text_color::RED);
                        }
                    }
                    break;

                case IT_RAMP:
                    if(! o.checked && oy <= SLED_Y + 8)
                    {
                        o.checked = true;

                        if(_air == 0 && _crash == 0 && bn::abs(_x - it.x) < RAMP_HALF)
                        {
                            _air = AIR_FRAMES;
                            sb::sfx(bn::sound_items::sfx_jump);
                            popup("Big air!", sb::text_color::YELLOW);
                        }
                    }
                    break;

                case IT_FINISH:
                    if(oy <= SLED_Y + 8)
                    {
                        finish();
                        return;
                    }
                    break;

                default:
                {
                    if(_air > 0 || _crash > 0)
                    {
                        break;  // flying over it, or still blinking after a crash
                    }

                    const hitbox& box = it.type == IT_LOG ? log_box : obstacle_boxes[int(it.type)];
                    int ox = it.x;

                    if(ox + box.half_w >= sl && ox - box.half_w <= sr && oy + box.bottom >= st && oy + box.top <= sbottom)
                    {
                        crash(ox, oy + box.top);
                        return;
                    }
                    break;
                }
                }
            }
        }

        void crash(int ox, int oy)
        {
            sb::sfx(bn::sound_items::sfx_crash);
            _speed *= CRASH_SPEED_FACTOR;
            _vx = _x < ox ? -CRASH_BUMP : CRASH_BUMP;
            _crash = CRASH_FRAMES;
            show_fx(((_x.integer() + ox) / 2), (SLED_Y + 8 + oy) / 2, 2);
            popup("Crash!", sb::text_color::RED);

            for(int i = 0; i < 3; ++i)
            {
                spawn_puff(_x - 10 + i * 10, i - 1);
            }
        }

        void finish()
        {
            _finished = true;
            _coast = FINISH_COAST_FRAMES;
            _air = 0;
            sb::sfx(bn::sound_items::sfx_record);
            show_fx(_x.integer(), SLED_Y - 4, 0);
            update_hud();
            _count_label.set(0, 8, "Finish!", sb::text_color::YELLOW, sb::align::CENTER);
        }

        void show_fx(int x, int y, int frame)
        {
            _fx_frame = frame;
            _fx.set_tiles(bn::sprite_items::sled_fx.tiles_item(), frame);
            _fx_x = x;
            _fx_y = y;
            _fx.set_position(draw_x(_fx_x), draw_y(_fx_y));
            _fx.set_z_order(Z_FX);
            _fx.set_visible(true);
            _fx_timer = 24;
        }

        void update_fx()
        {
            if(_fx_timer > 0)
            {
                --_fx_timer;
                _fx_y -= _speed;
                _fx.set_position(draw_x(_fx_x), draw_y(_fx_y));

                if(_fx_timer == 10 && _fx_frame == 0)
                {
                    _fx.set_tiles(bn::sprite_items::sled_fx.tiles_item(), 1);
                }

                if(_fx_timer == 0)
                {
                    _fx.set_visible(false);
                }
            }

            if(_popup_timer > 0 && --_popup_timer == 0)
            {
                _popup.clear();
            }
        }

        // ---------------------------------------------------------------- the ghost
        void load_ghost()
        {
            ghost_run& g = *_ghost;
            bn::sram::read_offset(g, GHOST_SRAM_OFFSET + _course * GHOST_SRAM_STRIDE);
            bool ok = g.course == _course && g.count > 1 && g.count <= GHOST_MAX && g.checksum == ghost_checksum(g);

            for(int i = 0; i < 8 && ok; ++i)
            {
                ok = g.tag[i] == GHOST_TAG[i];
            }

            if(! ok || g.character < 0 || g.character >= sb::character_count)
            {
                g.count = 0;
            }

            _ghost_rider.reset();
            _ghost_sled.set_visible(false);

            if(g.count > 0)
            {
                _ghost_rider = sb::character_sprite(g.character).create_sprite(0, 200, sb::FRAME_SIT);
                _ghost_rider->set_bg_priority(SPRITE_PRIORITY);
                _ghost_rider->set_affine_mat(_zoom);
                _ghost_rider->set_z_order(Z_BEHIND);
                _ghost_rider->set_blending_enabled(true);
                _ghost_rider->set_visible(false);
                bn::blending::set_transparency_alpha(GHOST_ALPHA);
            }
        }

        void record_ghost()
        {
            ghost_run& g = *_recording;

            if(_time_frames % GHOST_EVERY != 0 || g.count >= GHOST_MAX)
            {
                return;
            }

            g.x[g.count] = static_cast<signed char>(_x.round_integer());
            g.d[g.count] = static_cast<unsigned short>(bn::min(_travel.integer(), 65535));
            ++g.count;
        }

        // The ghost sled where your best run was at this moment of the race (between two samples).
        void place_ghost()
        {
            const ghost_run& g = *_ghost;

            if(g.count == 0 || ! _ghost_rider)
            {
                return;
            }

            int k = _time_frames / GHOST_EVERY;
            int frac = _time_frames % GHOST_EVERY;
            bn::fixed gx, gd;
            int lean = 0;

            if(k + 1 < g.count)
            {
                gx = bn::fixed(g.x[k]) + bn::fixed(g.x[k + 1] - g.x[k]) * frac / GHOST_EVERY;
                gd = bn::fixed(g.d[k]) + bn::fixed(g.d[k + 1] - g.d[k]) * frac / GHOST_EVERY;
                lean = g.x[k + 1] - g.x[k];
            }
            else
            {
                gx = g.x[g.count - 1];      // the ghost has finished: it waits at the finish line
                gd = g.d[g.count - 1];
            }

            bn::fixed gy = bn::fixed(SLED_Y) + (gd - _travel);
            bool visible = gy > DESPAWN_Y && gy < SLED_Y + SPAWN_AHEAD;
            _ghost_sled.set_visible(visible);
            _ghost_rider->set_visible(visible);

            if(visible)
            {
                int frame = lean < -2 ? 1 : lean > 2 ? 2 : 0;   // samples are 4 frames apart: > 0.6 px/frame
                _ghost_sled.set_tiles(bn::sprite_items::sled_sled.tiles_item(), frame);
                _ghost_sled.set_position(draw_x(gx), draw_y(gy));
                _ghost_rider->set_position(draw_x(gx + (frame == 1 ? -1 : frame == 2 ? 1 : 0)),
                                           draw_y(gy + RIDER_DY));
            }
        }

        // ---------------------------------------------------------------- presentation
        void place_sled()
        {
            int frame = 0;
            int rider_dx = 0;

            if(_vx < -LEAN_THRESHOLD)
            {
                frame = 1;
                rider_dx = -1;
            }
            else if(_vx > LEAN_THRESHOLD)
            {
                frame = 2;
                rider_dx = 1;
            }

            // Flying: up in an arc, with the shadow staying on the snow.
            int lift = 0;

            if(_air > 0)
            {
                int t = AIR_FRAMES - _air;
                lift = AIR_HEIGHT * 4 * t * _air / (AIR_FRAMES * AIR_FRAMES);
            }

            _shadow.set_visible(_air > 0);
            _shadow.set_position(draw_x(_x), draw_y(SLED_Y + 8));

            int rattle = (_speed > 2 && _air == 0 && (_frames / 3) % 2 == 0) ? 1 : 0;
            _sled.set_tiles(bn::sprite_items::sled_sled.tiles_item(), frame);
            _sled.set_position(draw_x(_x), draw_y(SLED_Y + rattle - lift));
            _rider.set_position(draw_x(_x + rider_dx), draw_y(SLED_Y + RIDER_DY + rattle - lift));
            _sled.set_z_order(Z_SLED);
            _rider.set_z_order(Z_RIDER);

            bool visible = _crash == 0 || (_crash / 4) % 2 == 0;
            _sled.set_visible(visible);
            _rider.set_visible(visible);
        }

        void update_hud()
        {
            int tenths = frames_to_hundredths(_time_frames + _penalty_frames) / 10;

            if(tenths != _shown_tenths)
            {
                _shown_tenths = tenths;
                bn::string<24> text("Time ");
                text.append(time_text(tenths * 10));
                _time_label.set(-114, -71, text, sb::text_color::WHITE);
            }

            int done = _gates_passed + _gates_missed;

            if(done != _shown_gates)
            {
                _shown_gates = done;
                bn::string<24> text("Gates ");
                text.append(sb::to_text(_gates_passed));
                text.append("/");
                text.append(sb::to_text(_total_gates));
                _gates_label.set(114, -71, text, sb::text_color::WHITE, sb::align::RIGHT);
            }
        }
    };

    int g_course_choice = 0;

    bool choose_course()
    {
        sb::label title;
        title.set(0, -60, "Which course?", sb::text_color::WHITE, sb::align::CENTER);
        constexpr bn::string_view items[] = { "Easy", "Medium", "Hard" };
        int choice = sb::menu(11, 5, items, g_course_choice, true);
        sb::panel_clear();

        if(choice < 0)
        {
            return false;
        }

        g_course_choice = choice;
        return true;
    }

    // The result box: your time, and the best time on every course. Returns true to ride again.
    bool show_results(sled_game& game)
    {
        int hundredths = game.result_hundredths();
        bool record = sb::submit_extra(SLED_RECORD_SLOT + g_course_choice, SLED_RECORD_BASE - hundredths,
                                       sb::player_character());

        if(record)
        {
            game.save_ghost();      // the new best run becomes the course's ghost
        }
        sb::play_jingle(record ? bn::music_items::jingle_record : bn::music_items::jingle_over);

        sb::panel(3, 0, 24, 14);
        sb::label title, result, detail, heading, lines[COURSE_COUNT], record_label;
        title.set(0, -70, "Finished!", sb::text_color::BLUE, sb::align::CENTER);

        bn::string<40> text(COURSES[g_course_choice].name);
        text.append(": ");
        text.append(time_text(hundredths));
        text.append(" seconds");
        result.set(0, -56, text, sb::text_color::DARK, sb::align::CENTER);

        bn::string<40> gates(sb::to_text(game.total_gates() - game.gates_missed()));
        gates.append("/");
        gates.append(sb::to_text(game.total_gates()));
        gates.append(" gates");

        if(game.gates_missed() > 0)
        {
            gates.append(" (+");
            gates.append(sb::to_text(game.gates_missed() * 2));
            gates.append("s)");
        }

        detail.set(0, -44, gates, sb::text_color::DARK, sb::align::CENTER);
        heading.set(0, -30, "{ Best times {", sb::text_color::RED, sb::align::CENTER);

        for(int c = 0; c < COURSE_COUNT; ++c)
        {
            bn::string<40> line(COURSES[c].name);
            line.append(":  ");
            int value = sb::extra_score(SLED_RECORD_SLOT + c);

            if(value > 0)
            {
                line.append(time_text(SLED_RECORD_BASE - value));
                line.append("s  ");
                line.append(sb::extra_name(SLED_RECORD_SLOT + c));
            }
            else
            {
                line.append("---");
            }

            lines[c].set(-72, -16 + c * 14, line, c == g_course_choice ? sb::text_color::BLUE : sb::text_color::DARK);
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

        constexpr bn::string_view items[] = { "Ride again", "Back to map" };
        int choice = sb::menu(8, 14, items, 0, true);
        sb::panel_clear();
        return choice == 0;
    }
}

namespace sb
{
    bn::string<12> sled_record_text(int value)
    {
        bn::string<12> text = time_text(SLED_RECORD_BASE - value);
        text.append("s");
        return text;
    }

    const char* sled_course_name(int course)
    {
        return COURSES[course].name;
    }

    int sled_record_slot()
    {
        return SLED_RECORD_SLOT;
    }
}

void sb::sled_run()
{
    // Shown on the start screen (scroll with Up/Down) and in the pause menu. '#' = heading.
    static constexpr bn::string_view howto[] = {
        "#Race to the bottom",
        "A time trial down the",
        "hill. Each course is the",
        "same every time: learn it",
        "and beat your best time!",
        "",
        "#Gates",
        "Steer between the two",
        "flags of every gate.",
        "A missed gate adds",
        "2 seconds.",
        "",
        "#Controls",
        "< > steer (you slide a",
        "little: steer early!)",
        "Hold B: brake and carve",
        "sharper turns.",
        "",
        "#Ghost",
        "Set a record and a see-",
        "through sled replays",
        "that run next time.",
        "Race your own ghost!",
        "",
        "#Ramps and crashes",
        "Hit a snow ramp to fly",
        "over the log behind it.",
        "Trees, rocks, snowmen and",
        "logs slow you right down.",
    };

    bn::bg_palettes::set_transparent_color(bn::color(29, 30, 31));

    {
        sled_game game;
        game.reset(g_course_choice);
        play_music(bn::music_items::sled_song);
        fade_in();

        if(game_intro(GAME_SLED, howto))
        {
            while(choose_course())
            {
                game.reset(g_course_choice);
                bool quit = false;

                while(true)
                {
                    if(game.step())
                    {
                        break;
                    }

                    update();

                    if(bn::keypad::start_pressed())
                    {
                        game.set_hud_visible(false);
                        bool leave = game_pause();
                        game.set_hud_visible(true);

                        if(leave)
                        {
                            quit = true;
                            break;
                        }
                    }
                }

                if(quit)
                {
                    break;
                }

                game.hide_hud();

                if(! show_results(game))
                {
                    break;
                }

                play_music(bn::music_items::sled_song);
            }
        }

        fade_out();
        game.hide_hud();
    }

    panel_clear();
}
