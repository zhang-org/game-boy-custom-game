// Show Jumping (was Pony Gallop): ride a clear round over a course of numbered jumps at the Stable.
//
// Pick a course (Easy, Medium or Hard). Every course is the same each time, so you can learn it.
//
// The skill is controlling your horse's speed for each jump, like in a real show jumping round:
//   * Hold > to speed up, hold < to slow down (the bar at the bottom left shows your speed).
//   * A takes off. A slow horse jumps high but short, a fast horse long but low:
//       - tall fences and stone walls need a slow, collected approach (fast = you knock a pole down),
//       - wide oxers (two fences) and water need speed (slow = you land on the back pole or in the water),
//       - low fences and hay bales are fine at any speed.
//   * Take off at the right moment: on Easy and Medium a little marker on the ground shows the best
//     takeoff spot for your current speed.
//   * Faults, as in the real sport: a knocked pole = 4, a foot in the water = 4, a refusal (you ran up to a
//     jump without taking off) = 4, and 1 for every second over the time allowed. Fewest faults wins, then
//     the fastest time. A round with no faults is a "Clear round!".
//
// Records: the best round per course (sb_save extra records HORSE_RECORD_SLOT + course, see
// docs/decisions/0004-record-slots.md). The old race (rhythm gallop against a rival) is in git history
// (before this file was rewritten); its records in slots 6-8 are no longer used.
//
// Art: tools/art/make_horse.py -> graphics/horse/ (reused from the race). Music: horse_song.
#include "bn_bg_palettes.h"
#include "bn_color.h"
#include "bn_fixed.h"
#include "bn_keypad.h"
#include "bn_log.h"
#include "bn_math.h"
#include "bn_optional.h"
#include "bn_regular_bg_ptr.h"
#include "bn_sprite_ptr.h"
#include "bn_sprite_text_generator.h"
#include "bn_string.h"
#include "bn_string_view.h"
#include "bn_vector.h"

#include "bn_music_items.h"
#include "bn_regular_bg_items_horse_far.h"
#include "bn_regular_bg_items_horse_mid.h"
#include "bn_regular_bg_items_horse_near.h"
#include "bn_sound_items.h"
#include "bn_sprite_items_horse_course.h"
#include "bn_sprite_items_horse_ditch.h"
#include "bn_sprite_items_horse_finish.h"
#include "bn_sprite_items_horse_horse.h"
#include "bn_sprite_items_horse_obstacles.h"
#include "bn_sprite_items_horse_pins.h"
#include "bn_sprite_items_horse_puff.h"
#include "bn_sprite_items_horse_stamina.h"
#include "bn_sprite_items_horse_strip.h"
#include "bn_sprite_palette_items_font_pal_white.h"

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
    // ---- Tuning ------------------------------------------------------------------------------
    // Speeds are in pixels per frame (60 frames per second). Heights in pixels, up is positive.

    // Speed: hold > / < to change it. It stays where you leave it.
    constexpr bn::fixed MIN_SPEED = 1.4;
    constexpr bn::fixed MAX_SPEED = 3.4;
    constexpr bn::fixed START_SPEED = 2.2;
    constexpr bn::fixed SPEED_UP = 0.025;           // per frame while > is held
    constexpr bn::fixed SLOW_DOWN = 0.035;          // per frame while < is held

    // Jumps: a slow horse jumps high and short, a fast one long and low. Takeoff speed up =
    // JUMP_VY_FAST + (MAX_SPEED - speed) * JUMP_VY_PER_SPEED. With these numbers (see the PR for the table):
    //   speed 1.4: 45 px high, 64 px long    speed 2.2: 33 px, 86 px    speed 3.4: 18 px, 100 px
    constexpr bn::fixed JUMP_VY_FAST = 2.5;
    constexpr bn::fixed JUMP_VY_PER_SPEED = 0.7;
    constexpr bn::fixed GRAVITY = 0.17;

    // Hitting things.
    constexpr int HORSE_HALF = 8;                   // half width of the horse's legs (the hitbox)
    constexpr int HIT_TOLERANCE = 3;                // hooves may brush this many px into a pole
    constexpr int WATER_HALF = 30;                  // the water: take off before it, land after it
    constexpr int REFUSE_GAP = 2;                   // on the ground this close to a jump = refusal
    constexpr int FAULTS_PER_MISTAKE = 4;
    constexpr int KNOCK_FRAMES = 30;                // slower for a moment after knocking a pole
    constexpr bn::fixed KNOCK_SPEED_FACTOR = 0.8;
    constexpr int SPLASH_FRAMES = 45;
    constexpr int REFUSAL_FRAMES = 70;              // stop, turn away and come back
    constexpr int REFUSAL_BACK_PX = 90;             // how far back you circle round to

    constexpr int FINISH_AFTER_PX = 280;            // from the last jump to the finish line
    constexpr int TIME_SLACK_FRAMES = 150;          // added to the course's time allowed
    constexpr int FINISH_COAST_FRAMES = 90;

    // Records: sb_save extra record slots HORSE_RECORD_SLOT .. +2 (Easy, Medium, Hard).
    constexpr int HORSE_RECORD_SLOT = 18;
    constexpr int HORSE_RECORD_BASE = 1000000;      // value = BASE - (faults * 10000 + hundredths)

    // ---- The jumps ---------------------------------------------------------------------------
    enum jump_kind
    {
        JK_LOW, JK_HAY, JK_TALL, JK_WALL, JK_OXER, JK_WATER
    };

    struct pole
    {
        int dx;         // from the jump's centre
        int half;       // half width
        int height;
    };

    struct jump_type
    {
        const char* name;       // for the hint at the top
        const char* advice;     // (Easy and Medium only)
        int poles;              // 0 = water
        pole p[2];
        bn::fixed best_speed;   // the speed the autopilot (and the hint) aims for
    };

    constexpr jump_type JUMP_TYPES[] = {
        { "low fence",   "any speed",       1, { { 0, 6, 15 }, {} },             2.8 },
        { "hay bales",   "any speed",       1, { { 0, 7, 12 }, {} },             2.8 },
        { "tall fence",  "slow down!",      1, { { 0, 6, 23 }, {} },             1.8 },
        { "stone wall",  "slow, jump high", 1, { { 0, 9, 26 }, {} },             1.6 },
        { "oxer",        "keep your speed", 2, { { -16, 6, 15 }, { 16, 6, 15 } }, 2.3 },
        { "water",       "go fast!",        0, { {}, {} },                       2.9 },
    };

    struct course_jump
    {
        short gap;      // px from the previous jump (the first: from the start)
        char kind;
    };

    constexpr course_jump EASY_JUMPS[] = {
        { 320, JK_LOW }, { 270, JK_HAY }, { 280, JK_LOW }, { 300, JK_TALL },
        { 280, JK_LOW }, { 300, JK_OXER }, { 260, JK_HAY }, { 300, JK_TALL },
    };

    constexpr course_jump MEDIUM_JUMPS[] = {
        { 320, JK_LOW }, { 280, JK_TALL }, { 300, JK_OXER }, { 240, JK_HAY }, { 300, JK_WALL },
        { 260, JK_LOW }, { 320, JK_WATER }, { 280, JK_TALL }, { 200, JK_OXER }, { 300, JK_WALL },
    };

    constexpr course_jump HARD_JUMPS[] = {
        { 320, JK_TALL }, { 260, JK_OXER }, { 280, JK_WALL }, { 200, JK_LOW }, { 320, JK_WATER },
        { 240, JK_OXER }, { 200, JK_TALL }, { 300, JK_WALL }, { 180, JK_HAY }, { 260, JK_OXER },
        { 320, JK_WATER }, { 220, JK_WALL },
    };

    struct course_def
    {
        const char* name;
        const course_jump* jumps;
        int count;
        bn::fixed time_speed;   // the time allowed = the course ridden at this speed (+ TIME_SLACK_FRAMES)
        bool takeoff_marker;    // show the best takeoff spot
        bool advice;            // say how to ride the next jump
    };

    constexpr course_def COURSES[] = {
        { "Easy",   EASY_JUMPS,   8,  1.9, true,  true },
        { "Medium", MEDIUM_JUMPS, 10, 2.15, true, true },
        { "Hard",   HARD_JUMPS,   12, 2.35, false, false },
    };
    constexpr int COURSE_COUNT = 3;
    constexpr int MAX_JUMPS = 12;

    // ---- Layout (Butano coordinates: (0, 0) is the screen centre) -----------------------------
    constexpr int PLAYER_X = -56;                   // horse sprite centre x
    constexpr int GROUND_Y = 60;                    // hoof line
    constexpr int RIDER_DX = -2;                    // rider sprite centre relative to the horse sprite centre
    constexpr int RIDER_DY = -21;
    constexpr int FINISH_POLE_DX = 13;              // the finish post is near the left edge of its sprite
    constexpr int MARKER_GROUND_Y = 31;
    constexpr int SPEED_BAR_X = -98;
    constexpr int SPEED_BAR_Y = 72;

    // Depth: only a few z values (Butano has 16 sprite sort layers in total).
    constexpr int Z_FRONT = 1;
    constexpr int Z_HORSE = 2;
    constexpr int Z_BACK = 3;

    // Horse sheet frames.
    enum horse_frame
    {
        HF_GALLOP = 0, HF_JUMP = 4, HF_STUMBLE = 5, HF_STAND = 6
    };

    constexpr int frame_bob[] = { -1, 0, 1, 0, -1, 2, 0 };

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

    // The takeoff speed up for a jump at this speed, and how far the jump goes.
    bn::fixed jump_vy(bn::fixed speed)
    {
        return JUMP_VY_FAST + (MAX_SPEED - speed) * JUMP_VY_PER_SPEED;
    }

    bn::fixed jump_length(bn::fixed speed)
    {
        return speed * jump_vy(speed) * 2 / GRAVITY;
    }

    struct jump_obj
    {
        int x;                                  // world x of the centre
        int kind;
        int number;                             // 1, 2, 3 ...
        bn::vector<bn::sprite_ptr, 2> sprites;  // the fences (two for an oxer), or the water
        bn::vector<bn::sprite_ptr, 2> plate;    // the number above it
        bn::vector<int, 2> plate_dx;
        int refusals = 0;                       // at this jump (the second one moves you on past it)
        bool faulted = false;                   // knocked or splashed (counted once)
        bool landed_on = false;                 // a jump came down on it (or just short): not a refusal
        bool skipped = false;
        bool done = false;                      // jumped (or knocked down): the next jump is up
    };

    struct puff
    {
        bn::sprite_ptr sprite;
        int x;
        int timer;
    };

    class show_round
    {
    public:
        show_round() :
            _far(bn::regular_bg_items::horse_far.create_bg(8, 48)),
            _mid(bn::regular_bg_items::horse_mid.create_bg(136, 48)),
            _near(bn::regular_bg_items::horse_near.create_bg(8, 48)),
            _horse(bn::sprite_items::horse_horse.create_sprite(PLAYER_X, GROUND_Y - 15, HF_STAND)),
            _rider(sb::character_sprite(sb::player_character()).create_sprite(0, 0, sb::FRAME_SIDE_SIT)),
            _speed_bar(bn::sprite_items::horse_stamina.create_sprite(SPEED_BAR_X, SPEED_BAR_Y, 0)),
            _marker(bn::sprite_items::horse_pins.create_sprite(0, GROUND_Y + 5, 0)),
            _snow(14, 1)
        {
            bn::bg_palettes::set_transparent_color(bn::color(13, 21, 29));
            _far.set_priority(3);
            _far.set_z_order(2);
            _mid.set_priority(3);
            _mid.set_z_order(1);
            _near.set_priority(2);

            _horse.set_bg_priority(1);
            _horse.set_z_order(Z_HORSE);
            _rider.set_bg_priority(1);
            _rider.set_z_order(Z_FRONT);
            _speed_bar.set_bg_priority(0);
            _speed_bar.set_z_order(Z_HORSE);
            _marker.set_bg_priority(1);
            _marker.set_z_order(Z_BACK);
            _marker.set_visible(false);
            reset(0);
        }

        // Back to the start of a course.
        void reset(int course)
        {
            _course = course;
            _jumps.clear();
            _puffs.clear();
            _finish.reset();
            _finish_strip.reset();
            _distance = 0;
            _speed = 0;
            _target_speed = START_SPEED;
            _height = 0;
            _vy = 0;
            _airborne = false;
            _knock = 0;
            _splash = 0;
            _refusal = 0;
            _refusal_from = 0;
            _anim = 0;
            _t = 0;
            _faults = 0;
            _time_faults = 0;
            _knocks = 0;
            _splashes = 0;
            _refusals = 0;
            _clean_jumps = 0;
            _next_jump = 0;
            _finished = false;
            _finish_frames = 0;
            _coast = 0;
            _banner_timer = 0;
            _banner.clear();
            _shown_tenths = -1;
            _shown_faults = -1;
            _shown_hint = -1;
            _horse_frame = HF_STAND;

            // Lay out the course (all of it: at most 12 jumps).
            int x = 0;
            const course_def& c = def();

            for(int i = 0; i < c.count; ++i)
            {
                x += c.jumps[i].gap;
                add_jump(x, c.jumps[i].kind, i + 1);
            }

            _finish_x = x + FINISH_AFTER_PX;
            _time_allowed = (bn::fixed(_finish_x) / c.time_speed).integer() + TIME_SLACK_FRAMES;
            spawn_finish();

            update_hud();
            draw();
            set_hud_visible(false);     // shown when the countdown starts (not over the intro and menus)
        }

        // 3, 2, 1, GO!
        void countdown()
        {
            set_hud_visible(true);
            sb::label big;
            constexpr bn::string_view counts[] = { "3", "2", "1" };

            for(const bn::string_view& text : counts)
            {
                big.set(0, -30, text, sb::text_color::YELLOW, sb::align::CENTER);
                sb::sfx(bn::sound_items::sfx_countdown);

                for(int i = 0; i < 40; ++i)
                {
                    _snow.update();
                    sb::update();
                }
            }

            big.set(0, -30, "GO!", sb::text_color::YELLOW, sb::align::CENTER);
            sb::sfx(bn::sound_items::sfx_go);

            for(int i = 0; i < 30; ++i)
            {
                step();
                sb::update();
            }
        }

        // One frame. Returns true when the round is over (finished and slowed down).
        bool step()
        {
            if(! _finished)
            {
                ++_t;
                control_speed();
            }
            else
            {
                _speed = bn::max(bn::fixed(0), _speed - bn::fixed(0.04));
                --_coast;
            }

            if(_refusal > 0)
            {
                update_refusal();
            }
            else
            {
                bn::fixed factor = _knock > 0 ? KNOCK_SPEED_FACTOR : bn::fixed(1);
                _distance += _speed * factor;
            }

            bool was_airborne = _airborne;
            jump_physics();

            if(! _finished)
            {
                // After the jump, so an A press at the very last moment still jumps.
                refusal_check(was_airborne && ! _airborne);
                collide();

                if(_distance >= _finish_x)
                {
                    cross_finish();
                }
            }

            update_puffs();

            if(_knock > 0)
            {
                --_knock;
            }

            if(_splash > 0)
            {
                --_splash;
            }

            if(_banner_timer > 0 && --_banner_timer == 0)
            {
                _banner.clear();
            }

            animate();
            _snow.update(-_speed / 3, 0);
            update_hud();
            draw();
            return _finished && _coast <= 0;
        }

        void set_hud_visible(bool visible)
        {
            _speed_bar.set_visible(visible);
            _time_label.set_visible(visible);
            _faults_label.set_visible(visible);
            _hint_label.set_visible(visible);
            _speed_label.set_visible(visible);
            _banner.set_visible(visible);
            _hud_visible = visible;
        }

        void hide_hud()
        {
            set_hud_visible(false);
            _time_label.clear();
            _faults_label.clear();
            _hint_label.clear();
            _speed_label.clear();
            _banner.clear();
            _shown_tenths = -1;
            _shown_faults = -1;
            _shown_hint = -1;
        }

        int course() const
        {
            return _course;
        }

        int faults() const
        {
            return _faults + _time_faults;
        }

        int time_faults() const
        {
            return _time_faults;
        }

        int knocks() const
        {
            return _knocks;
        }

        int splashes() const
        {
            return _splashes;
        }

        int refusals() const
        {
            return _refusals;
        }

        int clean_jumps() const
        {
            return _clean_jumps;
        }

        int jump_count() const
        {
            return def().count;
        }

        int finish_hundredths() const
        {
            return frames_to_hundredths(_finish_frames);
        }

        int allowed_hundredths() const
        {
            return frames_to_hundredths(_time_allowed);
        }

    private:
        bn::regular_bg_ptr _far, _mid, _near;
        bn::sprite_ptr _horse, _rider, _speed_bar, _marker;
        sb::snowfall _snow;
        bn::vector<jump_obj, MAX_JUMPS> _jumps;
        bn::vector<puff, 6> _puffs;
        bn::optional<bn::sprite_ptr> _finish, _finish_strip;
        sb::label _time_label, _faults_label, _hint_label, _speed_label, _banner;

        int _course = 0;
        bn::fixed _distance, _speed, _target_speed, _height, _vy, _anim;
        bool _airborne = false;
        int _knock = 0;
        int _splash = 0;
        int _refusal = 0;
        bn::fixed _refusal_from;
        int _t = 0;
        int _faults = 0;
        int _time_faults = 0;
        int _knocks = 0;
        int _splashes = 0;
        int _refusals = 0;
        int _clean_jumps = 0;
        int _next_jump = 0;
        int _finish_x = 0;
        int _time_allowed = 0;
        bool _finished = false;
        int _finish_frames = 0;
        int _coast = 0;
        int _banner_timer = 0;
        int _shown_tenths = -1;
        int _shown_faults = -1;
        int _shown_hint = -1;
        int _horse_frame = HF_STAND;
        bool _hud_visible = false;

        const course_def& def() const
        {
            return COURSES[_course];
        }

        int screen_x(int world_x) const
        {
            return PLAYER_X + world_x - _distance.integer();
        }

        void show_banner(const bn::string_view& text, sb::text_color color = sb::text_color::YELLOW)
        {
            _banner.set(0, -30, text, color, sb::align::CENTER);
            _banner.set_visible(_hud_visible);
            _banner_timer = 70;
        }

        void add_fault(const bn::string_view& text)
        {
            _faults += FAULTS_PER_MISTAKE;
            show_banner(text, sb::text_color::RED);
        }

        // ------------------------------------------------------------------------- the course
        void add_jump(int x, int kind, int number)
        {
            _jumps.push_back(jump_obj());
            jump_obj& j = _jumps.back();
            j.x = x;
            j.kind = kind;
            j.number = number;
            int sx = screen_x(x);

            auto fence = [&](const bn::sprite_item& item, int dx, int y, int frame) {
                bn::sprite_ptr sprite = item.create_sprite(sx + dx, y, frame);
                sprite.set_bg_priority(1);
                sprite.set_z_order(Z_BACK);
                j.sprites.push_back(sprite);
            };

            switch(kind)
            {
            case JK_LOW:
                fence(bn::sprite_items::horse_obstacles, 0, GROUND_Y - 15, 0);
                break;
            case JK_HAY:
                fence(bn::sprite_items::horse_obstacles, 0, GROUND_Y - 15, 4);
                break;
            case JK_TALL:
                fence(bn::sprite_items::horse_obstacles, 0, GROUND_Y - 15, 1);
                break;
            case JK_WALL:
                fence(bn::sprite_items::horse_course, 0, GROUND_Y - 15, 0);
                break;
            case JK_OXER:
                fence(bn::sprite_items::horse_obstacles, -16, GROUND_Y - 15, 0);
                fence(bn::sprite_items::horse_obstacles, 16, GROUND_Y - 15, 0);
                break;
            default:
                fence(bn::sprite_items::horse_ditch, 0, GROUND_Y - 7, 0);
                break;
            }

            // The number, on a little board above the jump.
            bn::sprite_text_generator& generator = sb::text_generator();
            generator.set_palette_item(bn::sprite_palette_items::font_pal_white);
            generator.set_bg_priority(1);
            generator.set_center_alignment();
            generator.generate(sx, MARKER_GROUND_Y - 8, sb::to_text(number), j.plate);
            generator.set_left_alignment();
            generator.set_bg_priority(0);

            for(bn::sprite_ptr& sprite : j.plate)
            {
                sprite.set_z_order(Z_BACK);
                j.plate_dx.push_back(sprite.x().integer() - sx);
            }
        }

        void spawn_finish()
        {
            int sx = screen_x(_finish_x);
            _finish = bn::sprite_items::horse_finish.create_sprite(sx + FINISH_POLE_DX, MARKER_GROUND_Y - 31, 0);
            _finish->set_bg_priority(2);
            _finish->set_z_order(Z_BACK);
            _finish_strip = bn::sprite_items::horse_strip.create_sprite(sx, GROUND_Y - 6, 0);
            _finish_strip->set_bg_priority(1);
            _finish_strip->set_z_order(Z_BACK);
        }

        jump_obj* next_jump()
        {
            return _next_jump < _jumps.size() ? &_jumps[_next_jump] : nullptr;
        }

        // ------------------------------------------------------------------------- riding
        bool a_pressed() const
        {
#ifdef HORSE_AUTOPILOT
            return autopilot_jump();
#else
            return bn::keypad::a_pressed();
#endif
        }

        void control_speed()
        {
            if(_refusal > 0 || _airborne)
            {
                return;
            }

#ifdef HORSE_AUTOPILOT
            bool faster = false, slower = false;

            if(const jump_obj* j = autopilot_target())
            {
                bn::fixed want = JUMP_TYPES[j->kind].best_speed;
                faster = _target_speed < want - bn::fixed(0.05);
                slower = _target_speed > want + bn::fixed(0.05);
            }
            else
            {
                faster = true;
            }
#else
            bool faster = bn::keypad::right_held();
            bool slower = bn::keypad::left_held();
#endif

            if(faster && ! slower)
            {
                _target_speed = bn::min(MAX_SPEED, _target_speed + SPEED_UP);
            }
            else if(slower && ! faster)
            {
                _target_speed = bn::max(MIN_SPEED, _target_speed - SLOW_DOWN);
            }

            // The horse follows quickly (it starts from a standstill at "GO!").
            if(_speed < _target_speed)
            {
                _speed = bn::min(_target_speed, _speed + bn::fixed(0.06));
            }
            else
            {
                _speed = bn::max(_target_speed, _speed - bn::fixed(0.08));
            }
        }

        void jump_physics()
        {
            if(! _airborne)
            {
                if(! _finished && _refusal == 0 && _t > 1 && a_pressed())
                {
                    _airborne = true;
                    _vy = jump_vy(_speed);
                    sb::sfx(bn::sound_items::sfx_jump);
                }

                return;
            }

            _height += _vy;
            _vy -= GRAVITY;

            if(_height <= 0)
            {
                _height = 0;
                _vy = 0;
                _airborne = false;
                sb::sfx(bn::sound_items::sfx_land);
                add_puff(_distance.integer() - 8);
                add_puff(_distance.integer() + 12);
                land();
            }
        }

        // Landing: in the water? Past the jump?
        void land()
        {
            jump_obj* j = next_jump();

            if(! j)
            {
                return;
            }

            int centre = _distance.integer();

            if(j->kind == JK_WATER)
            {
                if(bn::abs(centre - j->x) < WATER_HALF + HORSE_HALF / 2)
                {
                    splash(*j);
                }
                else if(centre > j->x)
                {
                    pass(*j);
                }
            }
        }

        void splash(jump_obj& j)
        {
            j.faulted = true;
            ++_splashes;
            _splash = SPLASH_FRAMES;
            _knock = SPLASH_FRAMES;
            j.sprites[0].set_tiles(bn::sprite_items::horse_ditch.tiles_item(), 1);
            add_fault("Splash! 4 faults");
            sb::sfx(bn::sound_items::sfx_splash);
            pass(j);
        }

        void knock(jump_obj& j, int pole_index)
        {
            j.faulted = true;
            ++_knocks;
            _knock = KNOCK_FRAMES;
            bn::sprite_ptr& sprite = j.sprites[pole_index];

            switch(j.kind)
            {
            case JK_HAY:
                sprite.set_tiles(bn::sprite_items::horse_obstacles.tiles_item(), 5);
                break;
            case JK_TALL:
                sprite.set_tiles(bn::sprite_items::horse_obstacles.tiles_item(), 3);
                break;
            case JK_WALL:
                sprite.set_tiles(bn::sprite_items::horse_course.tiles_item(), 1);
                break;
            default:
                sprite.set_tiles(bn::sprite_items::horse_obstacles.tiles_item(), 2);
                break;
            }

            add_puff(j.x + JUMP_TYPES[j.kind].p[pole_index].dx);
            add_fault(j.kind == JK_WALL ? "Knocked the wall! 4 faults" : "Pole down! 4 faults");
            sb::sfx(bn::sound_items::sfx_hurt);
        }

        // The jump is behind you: the next one is up.
        void pass(jump_obj& j)
        {
            if(j.done)
            {
                return;
            }

            j.done = true;
            ++_next_jump;

            if(! j.faulted)
            {
                ++_clean_jumps;
                sb::sfx(bn::sound_items::sfx_point);
            }
        }

        // Poles: hooves below a pole's top while over it knock it down (once per jump).
        void collide()
        {
            jump_obj* j = next_jump();

            if(! j)
            {
                return;
            }

            int left = _distance.integer() - HORSE_HALF;
            int right = _distance.integer() + HORSE_HALF;
            const jump_type& type = JUMP_TYPES[j->kind];

            if(j->kind == JK_WATER)
            {
                return;     // judged on landing (and refusals in refusal_check())
            }

            for(int i = 0; i < type.poles; ++i)
            {
                const pole& p = type.p[i];
                int pl = j->x + p.dx - p.half;
                int pr = j->x + p.dx + p.half;

                if(! j->faulted && pl < right && pr > left && _height < p.height - HIT_TOLERANCE)
                {
                    knock(*j, i);
                }
            }

            // Past the last pole (or its knocked remains): next jump.
            const pole& last = type.p[type.poles - 1];

            if(left > j->x + last.dx + last.half)
            {
                pass(*j);
            }
        }

        // Running up to a jump on the ground without taking off: the horse refuses.
        // `just_landed`: the horse came down from a jump this frame.
        void refusal_check(bool just_landed)
        {
            jump_obj* j = next_jump();

            if(! j || _airborne || _refusal > 0)
            {
                return;
            }

            const jump_type& type = JUMP_TYPES[j->kind];
            int front = j->kind == JK_WATER ? j->x - WATER_HALF : j->x + type.p[0].dx - type.p[0].half;

            if(_distance.integer() + HORSE_HALF >= front - REFUSE_GAP)
            {
                // Took off too early and came down on the fence (or right in front of it): that's a knocked
                // pole (collide() counts it), not a refusal. The horse carries on over the knocked remains.
                // (Water is judged on landing: coming down just short of it is still a refusal.)
                if(j->kind != JK_WATER && (just_landed || j->landed_on))
                {
                    j->landed_on = true;
                    return;
                }

                ++_refusals;
                _refusal = REFUSAL_FRAMES;
                _refusal_from = _distance;
                _speed = 0;
                sb::sfx(bn::sound_items::sfx_miss);

                // (In the real sport a second refusal ends your round. Here you just move on to the next jump,
                // so nobody gets stuck.)
                if(++j->refusals >= 2)
                {
                    j->faulted = true;
                    j->skipped = true;      // taken out of your way (it isn't drawn any more)
                    add_fault("Refused again: skip it, 4 faults");
                    pass(*j);
                }
                else
                {
                    add_fault("Refusal! 4 faults");
                }
            }
        }

        // Stop, then circle back to try again (shown as backing away from the jump).
        void update_refusal()
        {
            --_refusal;
            int t = REFUSAL_FRAMES - _refusal;

            if(t > 20)
            {
                // Ease back REFUSAL_BACK_PX over the remaining frames.
                bn::fixed k = bn::fixed(t - 20) / (REFUSAL_FRAMES - 20);
                _distance = _refusal_from - k * (2 - k) * REFUSAL_BACK_PX;
            }

            if(_refusal == 0)
            {
                _target_speed = bn::max(MIN_SPEED, bn::min(_target_speed, START_SPEED));
                _speed = MIN_SPEED;
            }
        }

        void cross_finish()
        {
            _finished = true;
            _finish_frames = _t;
            _coast = FINISH_COAST_FRAMES;

            if(_t > _time_allowed)
            {
                _time_faults = (_t - _time_allowed + 59) / 60;     // 1 for every started second over
            }

            if(faults() == 0)
            {
                show_banner("Clear round!");
                sb::sfx(bn::sound_items::sfx_record);
            }
            else
            {
                bn::string<24> text(sb::to_text(faults()));
                text.append(faults() == 1 ? " fault" : " faults");
                show_banner(text, sb::text_color::WHITE);
                sb::sfx(bn::sound_items::sfx_go);
            }
        }

        // ------------------------------------------------------------------------- autopilot
#ifdef HORSE_AUTOPILOT
        // Test builds only (make USERFLAGS=-DHORSE_AUTOPILOT): rides each jump at its best speed and takes
        // off at the best spot, so the harness can ride whole courses.
        const jump_obj* autopilot_target() const
        {
            return _next_jump < _jumps.size() ? &_jumps[_next_jump] : nullptr;
        }

        bool autopilot_jump() const
        {
            const jump_obj* j = autopilot_target();

            if(! j)
            {
                return false;
            }

            bn::fixed takeoff = j->x - jump_length(_speed) / 2;
            return _distance >= takeoff && _distance < takeoff + _speed + 1;
        }
#endif

        // ------------------------------------------------------------------------- scenery
        void add_puff(int world_x)
        {
            if(_puffs.full())
            {
                _puffs.erase(_puffs.begin());
            }

            bn::sprite_ptr sprite = bn::sprite_items::horse_puff.create_sprite(screen_x(world_x), GROUND_Y - 7, 0);
            sprite.set_bg_priority(1);
            sprite.set_z_order(Z_FRONT);
            _puffs.push_back({ sprite, world_x, 0 });
        }

        void update_puffs()
        {
            for(int i = 0; i < _puffs.size(); )
            {
                puff& p = _puffs[i];
                ++p.timer;

                if(p.timer >= 15)
                {
                    _puffs.erase(_puffs.begin() + i);
                    continue;
                }

                p.sprite.set_tiles(bn::sprite_items::horse_puff.tiles_item(), p.timer / 5);
                p.sprite.set_x(screen_x(p.x));
                ++i;
            }
        }

        void animate()
        {
            if(_refusal > REFUSAL_FRAMES - 24 || _splash > SPLASH_FRAMES - 18 || _knock > KNOCK_FRAMES - 10)
            {
                _horse_frame = HF_STUMBLE;
            }
            else if(_airborne)
            {
                _horse_frame = HF_JUMP;
            }
            else if(_speed < bn::fixed(0.3) && _refusal == 0)
            {
                _horse_frame = HF_STAND;
            }
            else
            {
                // Four gallop poses per stride; a clip-clop on every stride.
                bn::fixed pace = _refusal > 0 ? bn::fixed(1.6) : _speed;
                _anim += pace * bn::fixed(0.085);

                if(_anim >= 4)
                {
                    _anim -= 4;

                    if(! _finished || _speed > 1)
                    {
                        sb::sfx(bn::sound_items::sfx_gallop);
                    }
                }

                _horse_frame = HF_GALLOP + _anim.integer();
            }
        }

        // ----------------------------------------------------------------------------- HUD
        void update_hud()
        {
            int frames = _finished ? _finish_frames : _t;
            int tenths = frames_to_hundredths(frames) / 10;

            if(tenths != _shown_tenths)
            {
                _shown_tenths = tenths;
                bn::string<24> text(time_text(tenths * 10));
                text.append(" / ");
                text.append(sb::to_text(frames_to_hundredths(_time_allowed) / 100));
                _time_label.set(-114, -71, text, frames > _time_allowed ? sb::text_color::RED : sb::text_color::WHITE);
                _time_label.set_visible(_hud_visible);
            }

            if(faults() != _shown_faults)
            {
                _shown_faults = faults();
                bn::string<24> text("Faults ");
                text.append(sb::to_text(faults()));
                _faults_label.set(114, -71, text, faults() ? sb::text_color::RED : sb::text_color::WHITE,
                                  sb::align::RIGHT);
                _faults_label.set_visible(_hud_visible);
            }

            // Which jump is next (and how to ride it, on Easy and Medium).
            int hint = _finished ? -2 : _next_jump;

            if(hint != _shown_hint)
            {
                _shown_hint = hint;

                if(const jump_obj* j = next_jump(); j && ! _finished)
                {
                    const jump_type& type = JUMP_TYPES[j->kind];
                    bn::string<40> text(sb::to_text(j->number));
                    text.append(": ");
                    text.append(type.name);

                    if(def().advice)
                    {
                        text.append(", ");
                        text.append(type.advice);
                    }

                    _hint_label.set(0, -58, text, sb::text_color::YELLOW, sb::align::CENTER);
                }
                else
                {
                    _hint_label.set(0, -58, "To the finish!", sb::text_color::YELLOW, sb::align::CENTER);
                }

                _hint_label.set_visible(_hud_visible);
            }

            if(_speed_label.empty())
            {
                _speed_label.set(SPEED_BAR_X + 18, SPEED_BAR_Y, "Speed", sb::text_color::WHITE);
                _speed_label.set_visible(_hud_visible);
            }

            bn::fixed shown = _refusal > 0 ? bn::fixed(0) : _target_speed;
            int level = ((shown - MIN_SPEED) * 8 / (MAX_SPEED - MIN_SPEED)).round_integer() ;
            _speed_bar.set_tiles(bn::sprite_items::horse_stamina.tiles_item(), bn::clamp(level, 0, 8));
        }

        void draw()
        {
            int d = _distance.integer();
            _far.set_x(8 - (d / 8) % 256);
            _mid.set_x(136 - (d / 3) % 512);
            _near.set_x(8 - d % 256);

            int bob = frame_bob[_horse_frame];
            int hy = GROUND_Y - 15 - _height.integer();
            _horse.set_tiles(bn::sprite_items::horse_horse.tiles_item(), _horse_frame);
            _horse.set_position(PLAYER_X, hy);
            _horse.set_horizontal_flip(_refusal > 20 && _refusal < REFUSAL_FRAMES - 4);   // turning away
            _rider.set_horizontal_flip(_horse.horizontal_flip());
            _rider.set_position(PLAYER_X + (_horse.horizontal_flip() ? -RIDER_DX : RIDER_DX), hy + RIDER_DY + bob);

            for(jump_obj& j : _jumps)
            {
                int sx = screen_x(j.x);
                bool on_screen = sx > -160 && sx < 160 && ! j.skipped;
                const jump_type& type = JUMP_TYPES[j.kind];

                for(int i = 0; i < j.sprites.size(); ++i)
                {
                    j.sprites[i].set_x(sx + (type.poles == 2 ? type.p[i].dx : 0));
                    j.sprites[i].set_visible(on_screen);
                }

                for(int i = 0; i < j.plate.size(); ++i)
                {
                    j.plate[i].set_x(sx + j.plate_dx[i]);
                    j.plate[i].set_visible(on_screen);
                }
            }

            if(_finish)
            {
                int sx = screen_x(_finish_x);
                bool on_screen = sx > -150 && sx < 150;
                _finish->set_x(sx + FINISH_POLE_DX);
                _finish_strip->set_x(sx);
                _finish->set_visible(on_screen);
                _finish_strip->set_visible(on_screen);
            }

            // The best takeoff spot for the next jump at your current speed.
            bool marker = false;

            if(def().takeoff_marker && ! _airborne && _refusal == 0 && ! _finished)
            {
                if(const jump_obj* j = next_jump())
                {
                    bn::fixed takeoff = bn::fixed(j->x) - jump_length(_target_speed) / 2;
                    int sx = PLAYER_X + (takeoff - _distance).integer();

                    if(sx > PLAYER_X - 4 && sx < 130)
                    {
                        _marker.set_x(sx);
                        marker = true;
                    }
                }
            }

            _marker.set_visible(marker && _hud_visible);
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

    int record_value(int faults, int hundredths)
    {
        return HORSE_RECORD_BASE - (bn::min(faults, 99) * 10000 + bn::min(hundredths, 9999));
    }

    // "Clear 41.3s" or "4 faults 45.2s".
    bn::string<16> record_value_text(int value)
    {
        int packed = HORSE_RECORD_BASE - value;
        int faults = packed / 10000;
        bn::string<16> text;

        if(faults == 0)
        {
            text.append("Clear ");
        }
        else
        {
            text.append(sb::to_text(faults));
            text.append("f ");
        }

        text.append(time_text(packed % 10000));
        text.append("s");
        return text;
    }

    // The result box: faults, time, and the best round on every course. Returns true to ride again.
    bool show_results(const show_round& game)
    {
        int hundredths = game.finish_hundredths();
        int faults = game.faults();
        bool record = sb::submit_extra(HORSE_RECORD_SLOT + game.course(), record_value(faults, hundredths),
                                       sb::player_character());
        sb::play_jingle(record || faults == 0 ? bn::music_items::jingle_record : bn::music_items::jingle_over);

        // Everything fits in the box (rows 0..13) so the menu below (rows 14..19) doesn't cover it.
        sb::panel(3, 0, 24, 14);
        sb::label title, result, detail, stats, heading, lines[COURSE_COUNT], record_label;

        bn::string<40> text;

        if(faults == 0)
        {
            text.append("Clear round!");
        }
        else
        {
            text.append(sb::to_text(faults));
            text.append(faults == 1 ? " fault" : " faults");
        }

        title.set(0, -71, text, faults == 0 ? sb::text_color::BLUE : sb::text_color::RED, sb::align::CENTER);

        bn::string<40> line(COURSES[game.course()].name);
        line.append(": ");
        line.append(time_text(hundredths));
        line.append("s (allowed ");
        line.append(sb::to_text(game.allowed_hundredths() / 100));
        line.append(")");
        result.set(0, -59, line, sb::text_color::DARK, sb::align::CENTER);

        bn::string<40> mistakes;

        if(faults == 0)
        {
            mistakes.append("Not a pole touched!");
        }
        else
        {
            auto add = [&](int count, const char* what) {
                if(count > 0)
                {
                    if(! mistakes.empty())
                    {
                        mistakes.append(", ");
                    }

                    mistakes.append(sb::to_text(count));
                    mistakes.append(" ");
                    mistakes.append(what);
                }
            };

            add(game.knocks(), game.knocks() == 1 ? "pole" : "poles");
            add(game.splashes(), "water");
            add(game.refusals(), game.refusals() == 1 ? "refusal" : "refusals");
            add(game.time_faults(), "time");
        }

        detail.set(0, -48, mistakes, sb::text_color::DARK, sb::align::CENTER);

        bn::string<40> jumps("Clean jumps ");
        jumps.append(sb::to_text(game.clean_jumps()));
        jumps.append("/");
        jumps.append(sb::to_text(game.jump_count()));
        stats.set(0, -37, jumps, sb::text_color::DARK, sb::align::CENTER);
        heading.set(0, -25, "{ Best rounds {", sb::text_color::RED, sb::align::CENTER);

        for(int c = 0; c < COURSE_COUNT; ++c)
        {
            bn::string<40> best(COURSES[c].name);
            best.append(":  ");
            int value = sb::extra_score(HORSE_RECORD_SLOT + c);

            if(value > 0)
            {
                best.append(record_value_text(value));
                best.append("  ");
                best.append(sb::extra_name(HORSE_RECORD_SLOT + c));
            }
            else
            {
                best.append("---");
            }

            lines[c].set(-80, -13 + c * 11, best, c == game.course() ? sb::text_color::BLUE : sb::text_color::DARK);
        }

        for(int t = 0; t < 50; ++t)
        {
            if(record && t % 20 == 0)
            {
                record_label.set(0, 21, "{ NEW RECORD! {", sb::text_color::YELLOW, sb::align::CENTER);
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

// The best record shown on the map, the intro and the high score list (sb_games.h: game_info::best_text):
// the hardest course with a round, e.g. "Hard Clear 41.3s by Ella".
bn::string<40> sb::horse_best_text(bool compact)
{
    static constexpr const char* names[] = { "Easy", "Medium", "Hard" };
    return level_record_text(HORSE_RECORD_SLOT, COURSE_COUNT, names, record_value_text, compact);
}

void sb::horse_run()
{
    // Shown on the start screen (scroll with Up/Down) and in the pause menu. '#' = heading.
    static constexpr bn::string_view howto[] = {
        "#Show jumping",
        "Ride a clear round over",
        "the numbered jumps, in",
        "order, then through the",
        "finish. Fewest faults",
        "wins, then the fastest",
        "time.",
        "",
        "#Speed",
        "Hold > to go faster and",
        "< to slow down. The bar",
        "at the bottom shows your",
        "speed.",
        "",
        "#Jump",
        "Press A to take off.",
        "Slow = a high, short jump",
        "for tall fences and",
        "stone walls.",
        "Fast = a long, low jump",
        "for wide oxers (two",
        "fences) and water.",
        "On Easy and Medium a",
        "marker on the ground",
        "shows the best spot to",
        "take off.",
        "",
        "#Faults",
        "Pole down: 4",
        "Foot in the water: 4",
        "Refusal (no jump): 4",
        "(refuse twice and you",
        "skip that jump)",
        "Each second over the",
        "time allowed: 1",
    };

    {
        show_round game;
        play_music(bn::music_items::horse_song);

        if(bn::bg_palettes::fade_intensity() > 0)
        {
            fade_in();
        }

        if(game_intro(GAME_HORSE, howto))
        {
            while(choose_course())
            {
                game.reset(g_course_choice);
                game.countdown();
                bool quit = false;

                while(true)
                {
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

                    bool done = game.step();
                    update();

                    if(done)
                    {
                        break;
                    }
                }

                if(quit)
                {
                    break;
                }

                BN_LOG("Show jumping course ", game.course(), ": ", game.faults(), " faults, ",
                       game.finish_hundredths(), " hundredths (allowed ", game.allowed_hundredths(), "), knocks ",
                       game.knocks(), " splashes ", game.splashes(), " refusals ", game.refusals());
                game.hide_hud();

                if(! show_results(game))
                {
                    break;
                }

                play_music(bn::music_items::horse_song);
            }
        }

        game.hide_hud();
        fade_out();
    }

    panel_clear();
}
