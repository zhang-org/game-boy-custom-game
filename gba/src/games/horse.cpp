// Pony Gallop: a horse race across the snowy farm, against another family member.
//
// Pick a race (Easy, Medium or Hard). It's a set distance to the finish post, and the rival on the grey
// horse in the back lane always stays close, so it's a real race to the line.
//
// How you go fast (the skill to learn):
//   * Rhythm: horseshoes slide along the lane at the bottom, one per hoofbeat. Tap B (or Right) as each
//     one reaches the ring, in time with the clip-clop. On-beat taps build up "drive" (speed). Off-beat
//     taps and mashing tire the horse (the stamina bar), and an empty bar means "Tired!": slow for a while.
//   * Jumps: A jumps. Tap A for low fences, hay bales and snow drifts. Hold A for a big jump over stone
//     walls, tall fences, double fences and water ditches (take off right at the edge). Big jumps cost a
//     little stamina, so don't hold A for everything.
//   * Hitting something costs speed and drive; falling into a ditch costs a lot.
//
// Every race has the same obstacles every time (fixed seed), so you can learn it. Records: the best time
// per race (sb_save extra records HORSE_RECORD_SLOT + level, see docs/decisions/0002-record-slots.md).
//
// Art: tools/art/make_horse.py -> graphics/horse/. Sounds: tools/music/sfx_horse.py. Music: horse_song.
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
#include "bn_sprite_items_horse_bar.h"
#include "bn_sprite_items_horse_course.h"
#include "bn_sprite_items_horse_ditch.h"
#include "bn_sprite_items_horse_drift.h"
#include "bn_sprite_items_horse_finish.h"
#include "bn_sprite_items_horse_horse.h"
#include "bn_sprite_items_horse_notes.h"
#include "bn_sprite_items_horse_obstacles.h"
#include "bn_sprite_items_horse_pins.h"
#include "bn_sprite_items_horse_puff.h"
#include "bn_sprite_items_horse_stamina.h"
#include "bn_sprite_items_horse_strip.h"
#include "bn_sprite_palette_items_font_pal_white.h"
#include "bn_sprite_palette_items_horse_rival_pal.h"

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
    // Speeds are in pixels per frame (60 frames per second). 8 pixels = 1 metre.

    // The three races. Everything about how hard a race is lives in one row.
    struct level_def
    {
        const char* name;
        int length_m;               // distance to the finish post
        bn::fixed rival_pace;       // the rival's average speed (yours: BASE_SPEED .. BASE_SPEED + DRIVE_SPEED)
        int beat_frames;            // frames between hoofbeats (the rhythm)
        int perfect_window;         // a tap this many frames from the beat (or closer) is PERFECT ...
        int good_window;            // ... and this close is GOOD
        int gap_min, gap_max;       // pixels between obstacles
        // Obstacle mix in percent (adds up to 100).
        int low, hay, drift, tall, wall, ditch, doubles;
        unsigned seed;              // the obstacle layout (same every time)
    };

    constexpr level_def LEVELS[3] = {
        //  name      len  rival  beat perf good  gaps      low hay drift tall wall ditch dbl  seed
        { "Easy",    600, 2.55,  26,  4,   8,   300, 470,  40, 25, 15,   10,  5,   5,    0,  311 },
        { "Medium",  800, 2.95,  24,  3,   7,   260, 410,  28, 14, 10,   14, 10,  14,   10,  5821 },
        { "Hard",   1000, 3.2,   22,  3,   6,   220, 360,  20, 10,  5,   14, 15,  16,   20,  9047 },
    };
    constexpr int LEVEL_COUNT = 3;
    constexpr int PIXELS_PER_METRE = 8;

    // Your speed: BASE_SPEED with no rhythm at all, up to BASE_SPEED + DRIVE_SPEED with full drive.
    constexpr bn::fixed BASE_SPEED = 2.2;
    constexpr bn::fixed DRIVE_SPEED = 1.4;
    constexpr bn::fixed SPEED_EASE = 0.06;          // how quickly the speed follows the drive
    constexpr bn::fixed TIRED_SPEED_FACTOR = 0.8;   // speed while "Tired!"

    // Rhythm: drive (0..1) and stamina (0..100). Every tap moves the drive part of the way (DRIVE_STEP)
    // towards a target: PERFECT taps towards 1, GOOD taps only towards DRIVE_GOOD_TARGET. So a rider who
    // only gets GOODs settles at about half speed-up, and only accurate riders reach full speed.
    constexpr bn::fixed DRIVE_STEP = 0.35;
    constexpr bn::fixed DRIVE_PERFECT_TARGET = 1;
    constexpr bn::fixed DRIVE_GOOD_TARGET = 0.55;
    constexpr bn::fixed DRIVE_OFF = 0.1;            // lost for an off-beat tap
    constexpr bn::fixed DRIVE_MASH = 0.15;          // lost for mashing (a second tap on the same beat)
    constexpr bn::fixed DRIVE_DECAY = 0.0015;       // lost every frame on the ground (keep tapping!)
    constexpr int STAMINA_MAX = 100;
    constexpr bn::fixed STAMINA_REGEN = 0.1;        // per frame
    constexpr int STAMINA_GOOD = 2;                 // a GOOD tap costs a little ...
    constexpr int STAMINA_OFF = 12;                 // ... an off-beat tap more ...
    constexpr int STAMINA_MASH = 16;                // ... and mashing the most
    constexpr int STAMINA_BIG_JUMP = 8;             // a held (big) jump
    constexpr int STAMINA_COMBO_BONUS = 10;         // every COMBO_CHEER perfect beats in a row
    constexpr int COMBO_CHEER = 8;
    constexpr int TIRED_FRAMES = 150;
    constexpr int TIRED_RECOVER_STAMINA = 45;

    // Jumps (heights in pixels, up is positive). With these numbers a tap peaks at 21 px (clears the
    // low obstacles, not the walls) and a held jump at 41 px, in the air 32 / 47 frames.
    constexpr bn::fixed JUMP_SPEED = 2.6;
    constexpr bn::fixed GRAVITY = 0.17;
    constexpr bn::fixed GRAVITY_HOLD = 0.06;        // lighter pull while A is held at the start of a jump
    constexpr int JUMP_HOLD_FRAMES = 14;            // how long holding A keeps the jump floaty
    constexpr int BIG_JUMP_FRAMES = 6;              // held this long = a big jump (costs stamina)
    constexpr int JUMP_BUFFER_FRAMES = 8;           // A pressed this soon before landing still jumps

    // Collisions.
    constexpr int HORSE_HIT_HALF_WIDTH = 10;        // half width of the horse's hitbox (legs only: forgiving)
    constexpr int HIT_TOLERANCE = 3;                // hooves may dip this many px into an obstacle's top
    constexpr int DITCH_HALF = 26;                  // landing this close to a ditch's middle = splash
    constexpr int STUMBLE_FRAMES = 45;              // slowed down for this long after a hit
    constexpr bn::fixed STUMBLE_SPEED_FACTOR = 0.4; // speed multiplier right after a hit (recovers to 1)
    constexpr int SPLASH_FRAMES = 70;
    constexpr bn::fixed SPLASH_SPEED_FACTOR = 0.25;
    constexpr bn::fixed HIT_DRIVE_KEPT = 0.4;       // drive kept after hitting something
    constexpr int INVULNERABLE_FRAMES = 40;         // blinking, can't be hit again
    constexpr int STREAK_CHEER = 5;                 // "Great riding!" every this many clean jumps

    // Course.
    constexpr int FIRST_OBSTACLE_PX = 330;          // open track after "GO!"
    constexpr int CLEAR_BEFORE_FINISH_PX = 260;     // no obstacles this close to the finish
    constexpr int DOUBLE_FENCE_SPACING = 32;        // px between the two fences of a double
    constexpr int AFTER_BIG_EXTRA_PX = 60;          // extra room after walls, ditches and doubles

    // Rival: a slow wobble around its pace, and a rubber band that keeps it close to you.
    constexpr bn::fixed RIVAL_WOBBLE = 0.12;
    constexpr int RIVAL_WOBBLE_PERIOD = 420;
    constexpr int RIVAL_RUBBER_FULL_PX = 80;        // with this much lead (px) the band is at full strength:
    constexpr bn::fixed RIVAL_RUBBER_MAX = 1.0;     // the rival is this much faster (or slower when ahead)
    constexpr int RIVAL_RUBBER_FADE_PERCENT = 75;   // after this much of the race the band is weaker ...
    constexpr bn::fixed RIVAL_RUBBER_LATE = 0.15;   // ... (x this), so the finish is decided by you

    constexpr int FINISH_COAST_FRAMES = 100;        // gliding to a stop after the finish post

    // Records: sb_save extra record slots HORSE_RECORD_SLOT .. +2 (Easy, Medium, Hard).
    constexpr int HORSE_RECORD_SLOT = 6;
    constexpr int HORSE_RECORD_BASE = 1000000;      // record value = this - time in hundredths (higher = better)

    // ---- Layout (Butano coordinates: (0, 0) is the screen centre) -----------------------------
    constexpr int PLAYER_X = -64;                   // horse sprite centre x
    constexpr int GROUND_Y = 60;                    // player's hoof line
    constexpr int RIVAL_GROUND_Y = 24;              // rival's hoof line (back lane)
    constexpr int MARKER_GROUND_Y = 31;             // flags stand on the snow bank between lanes
    constexpr int RIDER_DX = -2;                    // rider sprite centre relative to the horse sprite centre
    constexpr int RIDER_DY = -21;
    constexpr int MARKER_EVERY_M = 100;
    constexpr int FINISH_POLE_DX = 13;              // the finish post is near the left edge of its sprite

    // HUD.
    constexpr int LANE_Y = 70;                      // the rhythm lane (bottom of the screen)
    constexpr int RING_X = -76;                     // where the beat lands
    constexpr int STAMINA_X = -106;
    constexpr bn::fixed NOTE_SPEED = 2.5;           // px per frame the horseshoes slide
    constexpr int NOTE_POOL = 6;
    constexpr int BAR_Y = -72;                      // the progress bar (top of the screen)
    constexpr int BAR_LEFT = -48;
    constexpr int BAR_RIGHT = 42;                   // where the finish square is

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

    // Obstacle kinds.
    enum obstacle_kind
    {
        OK_LOW, OK_TALL, OK_HAY, OK_DRIFT, OK_WALL, OK_DITCH
    };

    enum obstacle_sheet
    {
        SHEET_FENCES, SHEET_DRIFT, SHEET_WALL, SHEET_DITCH
    };

    struct obstacle_type
    {
        int height;         // hit area (px)
        int width;
        int sheet;
        int frame;
        int knocked_frame;
        int y;              // sprite centre y
        bool big;           // needs a held jump (for the autopilot and the hint)
    };

    constexpr obstacle_type obstacle_types[] = {
        { 15, 12, SHEET_FENCES, 0, 2, GROUND_Y - 15, false },  // low fence
        { 23, 12, SHEET_FENCES, 1, 3, GROUND_Y - 15, true },   // tall fence
        { 12, 14, SHEET_FENCES, 4, 5, GROUND_Y - 15, false },  // hay bale
        { 9, 28, SHEET_DRIFT, 0, 1, GROUND_Y - 7, false },     // snow drift
        { 26, 18, SHEET_WALL, 0, 1, GROUND_Y - 15, true },     // stone wall
        { 0, 52, SHEET_DITCH, 0, 1, GROUND_Y - 7, true },      // water ditch (judged on landing)
    };

    // A tiny random generator just for laying out the obstacles, so each race is the same every time.
    struct course_random
    {
        unsigned state;

        int range(int lo, int hi)
        {
            state = state * 1103515245u + 12345u;
            return lo + int((state >> 16) % unsigned(hi - lo + 1));
        }
    };

    struct obstacle
    {
        bn::sprite_ptr sprite;
        int x;            // world x (pixels)
        int kind;
        bool hit;
        bool passed;
    };

    struct puff
    {
        bn::sprite_ptr sprite;
        int x;            // world x
        int timer;
    };

    struct marker
    {
        bn::sprite_ptr flag;
        bn::vector<bn::sprite_ptr, 4> text;
        bn::vector<int, 4> text_dx;
        int x;            // world x
    };

    struct note
    {
        bn::sprite_ptr sprite;
        int beat = 0;
        bool active = false;
    };

    enum judgement
    {
        J_NONE, J_PERFECT, J_GOOD, J_OFF, J_MASH
    };

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

    bn::sprite_ptr hud_sprite(const bn::sprite_item& item, int x, int y, int frame)
    {
        bn::sprite_ptr sprite = item.create_sprite(x, y, frame);
        sprite.set_bg_priority(0);
        sprite.set_z_order(Z_HORSE);
        return sprite;
    }

    class race
    {
    public:
        race() :
            _far(bn::regular_bg_items::horse_far.create_bg(8, 48)),
            _mid(bn::regular_bg_items::horse_mid.create_bg(136, 48)),
            _near(bn::regular_bg_items::horse_near.create_bg(8, 48)),
            _horse(bn::sprite_items::horse_horse.create_sprite(PLAYER_X, GROUND_Y - 15, HF_STAND)),
            _rider(sb::character_sprite(sb::player_character()).create_sprite(0, 0, sb::FRAME_SIDE_SIT)),
            _rival_horse(bn::sprite_items::horse_horse.create_sprite(PLAYER_X, RIVAL_GROUND_Y - 15, HF_STAND)),
            _rival_rider(sb::character_sprite(0).create_sprite(0, 0, sb::FRAME_SIDE_SIT)),
            _ring(hud_sprite(bn::sprite_items::horse_notes, RING_X, LANE_Y, 2)),
            _stamina_bar(hud_sprite(bn::sprite_items::horse_stamina, STAMINA_X, LANE_Y, 8)),
            _you_pin(hud_sprite(bn::sprite_items::horse_pins, BAR_LEFT, BAR_Y - 3, 0)),
            _rival_pin(hud_sprite(bn::sprite_items::horse_pins, BAR_LEFT, BAR_Y - 3, 1)),
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
            _rival_horse.set_palette(bn::sprite_palette_items::horse_rival_pal);
            _rival_horse.set_bg_priority(2);
            _rival_horse.set_z_order(Z_HORSE);
            _rival_rider.set_bg_priority(2);
            _rival_rider.set_z_order(Z_FRONT);
            _you_pin.set_z_order(Z_FRONT);

            for(int i = 0; i < 3; ++i)
            {
                _bar.push_back(hud_sprite(bn::sprite_items::horse_bar, BAR_LEFT + 16 + i * 32, BAR_Y, i == 2 ? 1 : 0));
            }

            for(int i = 0; i < NOTE_POOL; ++i)
            {
                note n = { hud_sprite(bn::sprite_items::horse_notes, 0, LANE_Y, 0), 0, false };
                n.sprite.set_visible(false);
                _notes.push_back(n);
            }

            // The rival is someone else from the family.
            int player = sb::player_character();
            _rival = sb::rand_range(0, sb::character_count - 2);

            if(_rival >= player)
            {
                ++_rival;
            }

            _rival_rider.set_item(sb::character_sprite(_rival), sb::FRAME_SIDE_SIT);
            reset(0);
        }

        // Puts everything back at the start line.
        void reset(int level)
        {
            _level = level;
            _rnd = { def().seed };
            _finish_x = def().length_m * PIXELS_PER_METRE;
            _obstacles.clear();
            _puffs.clear();
            _markers.clear();
            _finish.reset();
            _finish_front.reset();
            _finish_back.reset();
            _distance = 0;
            _rival_x = 0;
            _speed = 0;
            _rival_speed = 0;
            _height = 0;
            _vy = 0;
            _airborne = false;
            _hold_frames = 0;
            _holding = false;
            _big_jump = false;
            _buffer = 0;
            _stumble = 0;
            _stumble_total = STUMBLE_FRAMES;
            _stumble_factor = STUMBLE_SPEED_FACTOR;
            _invulnerable = 0;
            _streak = 0;
            _anim = 0;
            _rival_anim = 0;
            _t = 0;
            _drive = 0;
            _stamina = STAMINA_MAX;
            _tired = 0;
            _combo = 0;
            _tapped_beat = 0;
            _counted_beat = 0;
            _last_tap = -100;
            _ring_flash = 0;
            _judge_timer = 0;
            _perfects = 0;
            _beats = 0;
            _jumps_clean = 0;
            _jumps_total = 0;
            _finished = false;
            _finish_frames = 0;
            _rival_finish_frames = 0;
            _coast = 0;
            _next_obstacle_x = FIRST_OBSTACLE_PX;
            _next_marker_m = MARKER_EVERY_M;
            _banner_timer = 0;
            _banner.clear();
            _judge_label.clear();
            _shown_tenths = -1;
            _shown_place = 0;
            _shown_rival_hint = 0;
            _shown_hint_m = -1;
            _rival_hint.clear();
            _horse_frame = HF_STAND;
            _rival_frame = HF_STAND;

            for(note& n : _notes)
            {
                n.active = false;
                n.sprite.set_visible(false);
            }

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
                    update_notes();
                    sb::update();
                    ++_pre_t;
                }
            }

            big.set(0, -30, "GO!", sb::text_color::YELLOW, sb::align::CENTER);
            sb::sfx(bn::sound_items::sfx_go);
            _pre_t = 0;

            for(int i = 0; i < 30; ++i)
            {
                step();
                sb::update();
            }
        }

        // One frame of the race. Returns true when the race is over (you finished and slowed down).
        bool step()
        {
            ++_t;

            if(! _finished)
            {
                rhythm();
            }

            update_speed();
            _distance += _speed;
            update_rival();
            jump_physics();

            if(! _finished)
            {
                spawn();
                collide();

                if(_distance >= _finish_x)
                {
                    cross_finish();
                }
            }
            else
            {
                --_coast;
            }

            update_markers();
            update_puffs();
            update_notes();

            if(_invulnerable > 0)
            {
                --_invulnerable;
            }

            if(_banner_timer > 0 && --_banner_timer == 0)
            {
                _banner.clear();
            }

            if(_judge_timer > 0 && --_judge_timer == 0)
            {
                _judge_label.clear();
            }

            animate();
            _snow.update(-_speed / 3, 0);
            update_hud();
            draw();
            return _finished && _coast <= 0;
        }

        void set_hud_visible(bool visible)
        {
            _ring.set_visible(visible);
            _stamina_bar.set_visible(visible);
            _you_pin.set_visible(visible);
            _rival_pin.set_visible(visible);

            for(bn::sprite_ptr& sprite : _bar)
            {
                sprite.set_visible(visible);
            }

            for(note& n : _notes)
            {
                n.sprite.set_visible(visible && n.active);
            }

            _time_label.set_visible(visible);
            _place_label.set_visible(visible);
            _banner.set_visible(visible);
            _judge_label.set_visible(visible);
            _rival_hint.set_visible(visible);
        }

        void hide_hud()
        {
            set_hud_visible(false);
            _time_label.clear();
            _place_label.clear();
            _banner.clear();
            _judge_label.clear();
            _rival_hint.clear();
            _shown_tenths = -1;
            _shown_place = 0;
            _shown_rival_hint = 0;
        }

        int level() const
        {
            return _level;
        }

        int finish_hundredths() const
        {
            return frames_to_hundredths(_finish_frames);
        }

        int rival_hundredths() const
        {
            return frames_to_hundredths(_rival_finish_frames);
        }

        bool won() const
        {
            return _finish_frames <= _rival_finish_frames;
        }

        int rival() const
        {
            return _rival;
        }

        int perfects() const
        {
            return _perfects;
        }

        int beats() const
        {
            return _beats;
        }

        int jumps_clean() const
        {
            return _jumps_clean;
        }

        int jumps_total() const
        {
            return _jumps_total;
        }

    private:
        bn::regular_bg_ptr _far, _mid, _near;
        bn::sprite_ptr _horse, _rider, _rival_horse, _rival_rider;
        bn::sprite_ptr _ring, _stamina_bar, _you_pin, _rival_pin;
        bn::vector<bn::sprite_ptr, 3> _bar;
        bn::vector<note, NOTE_POOL> _notes;
        bn::optional<bn::sprite_ptr> _finish, _finish_front, _finish_back;
        sb::snowfall _snow;
        bn::vector<obstacle, 12> _obstacles;
        bn::vector<puff, 6> _puffs;
        bn::vector<marker, 2> _markers;
        sb::label _time_label, _place_label, _banner, _rival_hint, _judge_label;
        course_random _rnd = { 1 };

        int _level = 0;
        int _rival = 0;
        int _finish_x = 0;
        bn::fixed _distance;     // player's world x (pixels travelled)
        bn::fixed _rival_x;
        bn::fixed _speed;
        bn::fixed _rival_speed;
        bn::fixed _height;       // hooves above the ground
        bn::fixed _vy;           // upward speed
        bool _airborne = false;
        bool _holding = false;
        bool _big_jump = false;
        int _hold_frames = 0;
        int _buffer = 0;
        int _stumble = 0;
        int _stumble_total = STUMBLE_FRAMES;
        bn::fixed _stumble_factor = STUMBLE_SPEED_FACTOR;
        int _invulnerable = 0;
        int _streak = 0;
        bn::fixed _anim, _rival_anim;
        int _t = 0;              // frames since GO
        int _pre_t = 0;          // frames into the countdown
        bn::fixed _drive;
        bn::fixed _stamina;
        int _tired = 0;
        int _combo = 0;
        int _tapped_beat = 0;    // the last beat that was tapped (each beat counts once)
        int _counted_beat = 0;   // beats up to this one are in the stats (tapped or missed)
        int _last_tap = -100;
        int _ring_flash = 0;
        int _judge_timer = 0;
        int _perfects = 0;
        int _beats = 0;
        int _jumps_clean = 0;
        int _jumps_total = 0;
        bool _finished = false;
        int _finish_frames = 0;
        int _rival_finish_frames = 0;
        int _coast = 0;
        int _next_obstacle_x = 0;
        int _next_marker_m = MARKER_EVERY_M;
        int _banner_timer = 0;
        int _shown_tenths = -1;
        int _shown_place = 0;
        int _shown_rival_hint = 0;
        int _shown_hint_m = -1;
        int _horse_frame = HF_STAND;
        int _rival_frame = HF_STAND;

        const level_def& def() const
        {
            return LEVELS[_level];
        }

        int screen_x(int world_x) const
        {
            return PLAYER_X + world_x - _distance.integer();
        }

        void show_banner(const bn::string_view& text, sb::text_color color = sb::text_color::YELLOW)
        {
            _banner.set(0, -44, text, color, sb::align::CENTER);
            _banner_timer = 70;
        }

        // ---------------------------------------------------------------------------- input
#ifdef HORSE_AUTOPILOT
        // Test builds only (make USERFLAGS=-DHORSE_AUTOPILOT=1, 2 or 3): jumps by itself, a held jump for the
        // big obstacles. =2 also taps every beat perfectly, =3 taps like a decent player, =1 never taps.
        bool _auto_hold = false;

        const obstacle* next_obstacle() const
        {
            const obstacle* best = nullptr;

            for(const obstacle& o : _obstacles)
            {
                int half = obstacle_types[o.kind].width / 2;

                if(! o.passed && o.x + half > _distance.integer() - HORSE_HIT_HALF_WIDTH && (! best || o.x < best->x))
                {
                    best = &o;
                }
            }

            return best;
        }

        bool a_pressed()
        {
            const obstacle* o = next_obstacle();

            if(_airborne || ! o)
            {
                return false;
            }

            bool big = obstacle_types[o->kind].big;
            int centre = o->x;

            for(const obstacle& other : _obstacles)
            {
                if(&other != o && other.x > o->x && other.x - o->x <= DOUBLE_FENCE_SPACING)
                {
                    big = true;
                    centre = o->x + DOUBLE_FENCE_SPACING / 2;
                }
            }

            int frames_to_peak = big ? 24 : 16;

            if(o->kind == OK_DITCH)
            {
                centre = o->x + 6;
            }

            if(centre - _distance.integer() <= (_speed * frames_to_peak).integer())
            {
                _auto_hold = big;
                return true;
            }

            return false;
        }

        bool a_held() const
        {
            return _auto_hold;
        }

        bool tap_pressed() const
        {
            int beat_frames = def().beat_frames;

            if(HORSE_AUTOPILOT == 3)
            {
                // A decent player: up to 6 frames off the beat, and misses every 7th beat.
                int beat = (_t + beat_frames / 2) / beat_frames;
                int off = (beat * 37) % 13 - 6;
                return beat % 7 != 0 && _t == beat * beat_frames + off;
            }

            return HORSE_AUTOPILOT == 2 && _t % beat_frames == 0;
        }
#else
        static bool a_pressed()
        {
            return bn::keypad::a_pressed();
        }

        static bool a_held()
        {
            return bn::keypad::a_held();
        }

        static bool tap_pressed()
        {
            return bn::keypad::b_pressed() || bn::keypad::right_pressed();
        }
#endif

        // --------------------------------------------------------------------------- rhythm
        void rhythm()
        {
            int beat_frames = def().beat_frames;

            // The hoofbeat you hear (and the beat the horseshoes mark).
            if(_t % beat_frames == 0 && ! _airborne)
            {
                sb::sfx(bn::sound_items::sfx_gallop);
            }

            // A beat that went by untapped breaks the combo (unless you were in the air, stumbling or tired:
            // those beats don't count).
            int closed_beat = _t > def().good_window ? (_t - def().good_window - 1) / beat_frames : 0;
            bool excused = _airborne || _tired > 0 || _stumble > 0;

            while(_counted_beat < closed_beat)
            {
                ++_counted_beat;

                if(_counted_beat > _tapped_beat && ! excused)
                {
                    _combo = 0;
                    ++_beats;
                }
            }

            if(_tired > 0)
            {
                if(--_tired == 0)
                {
                    _stamina = TIRED_RECOVER_STAMINA;
                    _ring.set_tiles(bn::sprite_items::horse_notes.tiles_item(), 2);
                    show_banner("Back in stride!", sb::text_color::WHITE);
                }
            }
            else
            {
                _stamina = bn::min(_stamina + STAMINA_REGEN, bn::fixed(STAMINA_MAX));
            }

            if(! _airborne)
            {
                _drive = bn::max(_drive - DRIVE_DECAY, bn::fixed(0));
            }

            if(_ring_flash > 0 && --_ring_flash == 0 && _tired == 0)
            {
                _ring.set_tiles(bn::sprite_items::horse_notes.tiles_item(), 2);
            }

            if(! tap_pressed() || _tired > 0 || _airborne || _stumble > _stumble_total - 12)
            {
                return;
            }

            // Which beat is nearest, and how far off was the tap?
            int beat = (_t + beat_frames / 2) / beat_frames;
            int off = bn::abs(_t - beat * beat_frames);
            judgement j;

            if(beat <= _tapped_beat && beat > 0)
            {
                j = J_MASH;
            }
            else if(_t - _last_tap < beat_frames / 3)
            {
                j = J_MASH;
            }
            else if(beat == 0)
            {
                j = J_OFF;
            }
            else if(off <= def().perfect_window)
            {
                j = J_PERFECT;
            }
            else if(off <= def().good_window)
            {
                j = J_GOOD;
            }
            else
            {
                j = J_OFF;
            }

            _last_tap = _t;

            if(j != J_MASH && beat > 0)
            {
                _tapped_beat = beat;

                if(beat > _counted_beat)
                {
                    _counted_beat = beat;
                    ++_beats;
                }
            }

            switch(j)
            {
            case J_PERFECT:
                _drive += (DRIVE_PERFECT_TARGET - _drive) * DRIVE_STEP;
                ++_perfects;
                ++_combo;
                flash_ring(3);
                judge("Perfect", sb::text_color::YELLOW);
                hide_note(beat);
                sb::sfx(bn::sound_items::sfx_hb_perfect);

                if(_combo % COMBO_CHEER == 0)
                {
                    _stamina = bn::min(_stamina + STAMINA_COMBO_BONUS, bn::fixed(STAMINA_MAX));
                    bn::string<24> text("Rhythm x");
                    text.append(sb::to_text(_combo));
                    text.append("!");
                    show_banner(text);
                    sb::sfx(bn::sound_items::sfx_point);
                }
                break;

            case J_GOOD:
                if(_drive < DRIVE_GOOD_TARGET)
                {
                    _drive += (DRIVE_GOOD_TARGET - _drive) * DRIVE_STEP;
                }
                else
                {
                    _drive -= (_drive - DRIVE_GOOD_TARGET) * DRIVE_STEP / 2;   // a GOOD pulls a fast horse back a bit
                }

                _stamina -= STAMINA_GOOD;
                _combo = 0;
                flash_ring(4);
                judge("Good", sb::text_color::WHITE);
                hide_note(beat);
                sb::sfx(bn::sound_items::sfx_hb_good);
                break;

            case J_OFF:
                _drive = bn::max(_drive - DRIVE_OFF, bn::fixed(0));
                _stamina -= STAMINA_OFF;
                _combo = 0;
                judge("Off beat", sb::text_color::RED);
                break;

            default:
                _drive = bn::max(_drive - DRIVE_MASH, bn::fixed(0));
                _stamina -= STAMINA_MASH;
                _combo = 0;
                judge("Too fast!", sb::text_color::RED);
                break;
            }

            if(_stamina <= 0)
            {
                get_tired();
            }
        }

        void get_tired()
        {
            _stamina = 0;
            _tired = TIRED_FRAMES;
            _drive = 0;
            _combo = 0;
            _ring.set_tiles(bn::sprite_items::horse_notes.tiles_item(), 5);
            show_banner("Tired! Slow down...", sb::text_color::RED);
            sb::sfx(bn::sound_items::sfx_hb_tired);
        }

        void flash_ring(int frame)
        {
            _ring.set_tiles(bn::sprite_items::horse_notes.tiles_item(), frame);
            _ring_flash = 8;
        }

        void judge(const bn::string_view& text, sb::text_color color)
        {
            _judge_label.set(RING_X, LANE_Y - 15, text, color, sb::align::CENTER);
            _judge_timer = 24;
        }

        void hide_note(int beat)
        {
            for(note& n : _notes)
            {
                if(n.active && n.beat == beat)
                {
                    n.active = false;
                    n.sprite.set_visible(false);
                }
            }
        }

        // The horseshoes in the lane: one per beat, sliding left to land on the ring exactly on the beat.
        void update_notes()
        {
            int beat_frames = def().beat_frames;
            int now = _t > 0 || _finished ? _t : _pre_t - 120;     // during the countdown: GO = 0
            int last_visible = (now + (136 - RING_X) * 2 / 5) / beat_frames;   // NOTE_SPEED = 2.5

            for(int beat = bn::max(1, (now - 20) / beat_frames); beat <= last_visible && ! _finished; ++beat)
            {
                bool shown = beat <= _tapped_beat && _t > 0;

                for(const note& n : _notes)
                {
                    shown |= n.active && n.beat == beat;
                }

                if(! shown && beat * beat_frames > now)
                {
                    for(note& n : _notes)
                    {
                        if(! n.active)
                        {
                            n.active = true;
                            n.beat = beat;
                            n.sprite.set_tiles(bn::sprite_items::horse_notes.tiles_item(), 0);
                            n.sprite.set_visible(true);
                            break;
                        }
                    }
                }
            }

            for(note& n : _notes)
            {
                if(! n.active)
                {
                    continue;
                }

                bn::fixed x = RING_X + NOTE_SPEED * (n.beat * beat_frames - now);

                if(x < RING_X - 6)
                {
                    n.sprite.set_tiles(bn::sprite_items::horse_notes.tiles_item(), 1);   // missed: grey
                }

                if(x < RING_X - 30 || _finished)
                {
                    n.active = false;
                    n.sprite.set_visible(false);
                    continue;
                }

                n.sprite.set_x(x);
            }
        }

        // ---------------------------------------------------------------------------- speed
        void update_speed()
        {
            if(_finished)
            {
                _speed = bn::max(_speed - bn::fixed(0.04), bn::fixed(0));
                return;
            }

            if(_t < 1)
            {
                return;
            }

            bn::fixed target = BASE_SPEED + DRIVE_SPEED * _drive;

            if(_tired > 0)
            {
                target = BASE_SPEED * TIRED_SPEED_FACTOR;
            }

            if(_stumble > 0)
            {
                bn::fixed recovered = bn::fixed(_stumble_total - _stumble) / _stumble_total;
                target *= _stumble_factor + (1 - _stumble_factor) * recovered;
                --_stumble;
                _speed = bn::min(_speed, target);
            }

            if(! _airborne)
            {
                _speed += (target - _speed) * SPEED_EASE;

                if(_t < 40)
                {
                    _speed = bn::max(_speed, BASE_SPEED * _t / 40);   // a quick start off the line
                }
            }
        }

        void update_rival()
        {
            if(_t < 1)
            {
                return;
            }

            if(_rival_finish_frames > 0 && _rival_x > _finish_x + 400)
            {
                _rival_speed = bn::max(_rival_speed - bn::fixed(0.04), bn::fixed(0));
            }
            else
            {
                bn::fixed phase = bn::fixed(_t % RIVAL_WOBBLE_PERIOD) / RIVAL_WOBBLE_PERIOD;
                bn::fixed lead = _rival_x - _distance;
                // (bn::fixed * bn::fixed only keeps 6 bits after the point, so divide by an int instead of
                // multiplying by a tiny constant, which would round to 0.)
                bn::fixed pull = bn::clamp(-lead / RIVAL_RUBBER_FULL_PX, bn::fixed(-1), bn::fixed(1));
                bn::fixed band = pull * RIVAL_RUBBER_MAX;

                // (int maths: a bn::fixed this big times 100 would overflow)
                if(_finished || _rival_x.integer() > _finish_x / 100 * RIVAL_RUBBER_FADE_PERCENT)
                {
                    band *= RIVAL_RUBBER_LATE;
                }

                if(_finished)
                {
                    band = 0;
                }

                bn::fixed target = def().rival_pace + RIVAL_WOBBLE * bn::sin(phase) + band;

                if(_t < 40)
                {
                    target = bn::min(target, BASE_SPEED * _t / 40 + bn::fixed(0.1));
                }

                _rival_speed += (target - _rival_speed) / 8;
            }

            _rival_x += _rival_speed;

            if(_rival_finish_frames == 0 && _rival_x >= _finish_x)
            {
                _rival_finish_frames = _t;

                if(! _finished)
                {
                    bn::string<32> text(sb::character_name(_rival));
                    text.append(" finished!");
                    show_banner(text, sb::text_color::WHITE);
                }
            }
        }

        void cross_finish()
        {
            _finished = true;
            _finish_frames = _t;
            _coast = FINISH_COAST_FRAMES;
            _judge_label.clear();
            bool first = _rival_finish_frames == 0;
            show_banner(first ? "You win!" : "Finished!", first ? sb::text_color::YELLOW : sb::text_color::WHITE);
            sb::sfx(bn::sound_items::sfx_record);

            if(first)
            {
                // Where the rival would finish at its pace (the race result needs a time for both).
                bn::fixed left = bn::fixed(_finish_x) - _rival_x;
                bn::fixed pace = bn::max(_rival_speed, bn::fixed(1));
                _rival_finish_frames = _t + (left / pace).ceil_integer();
            }

            BN_LOG("Pony race ", _level, ": you ", _finish_frames, " frames, rival ", _rival_finish_frames,
                   ", perfect beats ", _perfects, "/", _beats, ", clean jumps ", _jumps_clean, "/", _jumps_total);
        }

        // ---------------------------------------------------------------------------- jumps
        void jump_physics()
        {
            if(! _finished && _t > 0 && a_pressed())
            {
                _buffer = JUMP_BUFFER_FRAMES;
            }
            else if(_buffer > 0)
            {
                --_buffer;
            }

            if(! _airborne)
            {
                if(_buffer > 0 && _stumble < _stumble_total - 15)
                {
                    _buffer = 0;
                    _airborne = true;
                    _vy = JUMP_SPEED;
                    _hold_frames = 0;
                    _holding = a_held();
                    _big_jump = false;
                    sb::sfx(bn::sound_items::sfx_jump);
                }

                return;
            }

            _height += _vy;

            if(_holding && a_held() && _hold_frames < JUMP_HOLD_FRAMES && _vy > 0)
            {
                _vy -= GRAVITY_HOLD;
                ++_hold_frames;

                if(_hold_frames == BIG_JUMP_FRAMES && ! _big_jump)
                {
                    _big_jump = true;
                    _stamina -= STAMINA_BIG_JUMP;

                    if(_stamina <= 0 && _tired == 0)
                    {
                        _stamina = 1;   // never tired in mid-air: it happens on the next off-beat tap
                    }
                }
            }
            else
            {
                _holding = false;
                _vy -= GRAVITY;
            }

            if(_height <= 0)
            {
                _height = 0;
                _vy = 0;
                _airborne = false;
                sb::sfx(bn::sound_items::sfx_land);
                add_puff(_distance.integer() - 8);
                add_puff(_distance.integer() + 12);
            }
        }

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

        void add_obstacle(int world_x, int kind)
        {
            if(_obstacles.full())
            {
                return;
            }

            const obstacle_type& type = obstacle_types[kind];
            bn::optional<bn::sprite_ptr> sprite;

            switch(type.sheet)
            {
            case SHEET_DRIFT:
                sprite = bn::sprite_items::horse_drift.create_sprite(screen_x(world_x), type.y, type.frame);
                break;
            case SHEET_WALL:
                sprite = bn::sprite_items::horse_course.create_sprite(screen_x(world_x), type.y, type.frame);
                break;
            case SHEET_DITCH:
                sprite = bn::sprite_items::horse_ditch.create_sprite(screen_x(world_x), type.y, type.frame);
                break;
            default:
                sprite = bn::sprite_items::horse_obstacles.create_sprite(screen_x(world_x), type.y, type.frame);
                break;
            }

            sprite->set_bg_priority(1);
            sprite->set_z_order(Z_BACK);
            _obstacles.push_back({ *sprite, world_x, kind, false, false });
        }

        // Lays out the next obstacle once it's about to scroll in (the same layout every race).
        void spawn()
        {
            if(_finish_front)
            {
                return;
            }

            if(_next_obstacle_x >= _finish_x - CLEAR_BEFORE_FINISH_PX)
            {
                if(screen_x(_finish_x) <= 150)
                {
                    spawn_finish();
                }

                return;
            }

            if(screen_x(_next_obstacle_x) > 150 || _obstacles.size() > _obstacles.max_size() - 2)
            {
                return;
            }

            const level_def& d = def();
            int roll = _rnd.range(0, 99);
            int extra = 0;
            int x = _next_obstacle_x;

            if((roll -= d.low) < 0)
            {
                add_obstacle(x, OK_LOW);
            }
            else if((roll -= d.hay) < 0)
            {
                add_obstacle(x, OK_HAY);
            }
            else if((roll -= d.drift) < 0)
            {
                add_obstacle(x, OK_DRIFT);
                extra = 8;
            }
            else if((roll -= d.tall) < 0)
            {
                add_obstacle(x, OK_TALL);
                extra = AFTER_BIG_EXTRA_PX;
            }
            else if((roll -= d.wall) < 0)
            {
                add_obstacle(x, OK_WALL);
                extra = AFTER_BIG_EXTRA_PX;
            }
            else if((roll -= d.ditch) < 0)
            {
                add_obstacle(x, OK_DITCH);
                extra = AFTER_BIG_EXTRA_PX + 30;
            }
            else
            {
                add_obstacle(x, OK_LOW);
                add_obstacle(x + DOUBLE_FENCE_SPACING, OK_LOW);
                extra = AFTER_BIG_EXTRA_PX + DOUBLE_FENCE_SPACING;
            }

            _next_obstacle_x += _rnd.range(d.gap_min, d.gap_max) + extra;
        }

        void spawn_finish()
        {
            int sx = screen_x(_finish_x);
            _finish = bn::sprite_items::horse_finish.create_sprite(sx + FINISH_POLE_DX, MARKER_GROUND_Y - 31, 0);
            _finish->set_bg_priority(2);
            _finish->set_z_order(Z_BACK);
            _finish_front = bn::sprite_items::horse_strip.create_sprite(sx, GROUND_Y - 6, 0);
            _finish_front->set_bg_priority(1);
            _finish_front->set_z_order(Z_BACK);
            _finish_back = bn::sprite_items::horse_strip.create_sprite(sx, RIVAL_GROUND_Y - 6, 0);
            _finish_back->set_bg_priority(2);
            _finish_back->set_z_order(Z_BACK);
        }

        void collide()
        {
            int left = _distance.integer() - HORSE_HIT_HALF_WIDTH;
            int right = _distance.integer() + HORSE_HIT_HALF_WIDTH;
            int centre = _distance.integer();

            for(obstacle& o : _obstacles)
            {
                const obstacle_type& type = obstacle_types[o.kind];
                int o_left = o.x - type.width / 2;
                int o_right = o.x + type.width / 2;

                if(! o.hit && ! o.passed)
                {
                    bool hit;

                    if(o.kind == OK_DITCH)
                    {
                        hit = ! _airborne && bn::abs(centre - o.x) < DITCH_HALF;
                    }
                    else
                    {
                        hit = _invulnerable == 0 && o_left < right && o_right > left &&
                              _height < type.height - HIT_TOLERANCE;
                    }

                    if(hit)
                    {
                        hit_obstacle(o);
                    }
                }

                if(! o.passed && o_right < left)
                {
                    o.passed = true;
                    ++_jumps_total;

                    if(! o.hit)
                    {
                        ++_jumps_clean;
                        ++_streak;

                        if(_streak % STREAK_CHEER == 0)
                        {
                            show_banner(_streak >= STREAK_CHEER * 2 ? "Amazing riding!" : "Great riding!");
                            sb::sfx(bn::sound_items::sfx_point);
                        }
                    }
                }
            }

            // Recycle obstacles that have left the screen.
            for(int i = 0; i < _obstacles.size(); )
            {
                if(screen_x(_obstacles[i].x) < -150)
                {
                    _obstacles.erase(_obstacles.begin() + i);
                }
                else
                {
                    ++i;
                }
            }
        }

        void hit_obstacle(obstacle& o)
        {
            const obstacle_type& type = obstacle_types[o.kind];
            o.hit = true;
            _streak = 0;
            _drive *= HIT_DRIVE_KEPT;

            switch(type.sheet)
            {
            case SHEET_DRIFT:
                o.sprite.set_tiles(bn::sprite_items::horse_drift.tiles_item(), type.knocked_frame);
                break;
            case SHEET_WALL:
                o.sprite.set_tiles(bn::sprite_items::horse_course.tiles_item(), type.knocked_frame);
                break;
            case SHEET_DITCH:
                o.sprite.set_tiles(bn::sprite_items::horse_ditch.tiles_item(), type.knocked_frame);
                break;
            default:
                o.sprite.set_tiles(bn::sprite_items::horse_obstacles.tiles_item(), type.knocked_frame);
                break;
            }

            if(o.kind == OK_DITCH)
            {
                _stumble = SPLASH_FRAMES;
                _stumble_total = SPLASH_FRAMES;
                _stumble_factor = SPLASH_SPEED_FACTOR;
                _drive = 0;
                show_banner("Splash!", sb::text_color::RED);
                sb::sfx(bn::sound_items::sfx_splash);
            }
            else
            {
                _stumble = STUMBLE_FRAMES;
                _stumble_total = STUMBLE_FRAMES;
                _stumble_factor = STUMBLE_SPEED_FACTOR;
                _invulnerable = INVULNERABLE_FRAMES;
                add_puff(o.x);
                sb::sfx(bn::sound_items::sfx_hurt);

                if(type.big && _height < 4)
                {
                    show_banner("Hold A for big jumps!", sb::text_color::WHITE);
                }
            }

#ifdef HORSE_AUTOPILOT
            BN_LOG("HIT kind ", o.kind, " at m ", _distance.integer() / PIXELS_PER_METRE, " speed ", _speed,
                   " height ", _height);
#endif
        }

        // ------------------------------------------------------------------------- scenery
        void update_markers()
        {
            // Spawn the next "100 m" flag when it's about to scroll in.
            int marker_x = _next_marker_m * PIXELS_PER_METRE;

            if(! _markers.full() && screen_x(marker_x) < 150 && marker_x < _finish_x)
            {
                bn::sprite_ptr flag = bn::sprite_items::horse_obstacles.create_sprite(
                            screen_x(marker_x), MARKER_GROUND_Y - 15, 6);
                flag.set_bg_priority(2);
                flag.set_z_order(Z_BACK);
                _markers.push_back({ flag, {}, {}, marker_x });
                marker& m = _markers.back();

                bn::string<16> text = sb::to_text(_next_marker_m);
                text.append("m");
                bn::sprite_text_generator& generator = sb::text_generator();
                generator.set_palette_item(bn::sprite_palette_items::font_pal_white);
                generator.set_bg_priority(2);
                generator.set_center_alignment();
                generator.generate(screen_x(marker_x) + 1, MARKER_GROUND_Y - 40, text, m.text);
                generator.set_left_alignment();
                generator.set_bg_priority(0);

                for(bn::sprite_ptr& sprite : m.text)
                {
                    sprite.set_z_order(Z_BACK);
                    m.text_dx.push_back(sprite.x().integer() - screen_x(marker_x));
                }

                _next_marker_m += MARKER_EVERY_M;
            }

            for(int i = 0; i < _markers.size(); )
            {
                marker& m = _markers[i];
                int sx = screen_x(m.x);
                m.flag.set_x(sx);

                for(int k = 0; k < m.text.size(); ++k)
                {
                    m.text[k].set_x(sx + m.text_dx[k]);
                }

                if(sx < -150)
                {
                    _markers.erase(_markers.begin() + i);
                }
                else
                {
                    ++i;
                }
            }
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
            // Four gallop poses per beat, so the legs move with the clip-clop.
            int beat_frames = def().beat_frames;

            if(_stumble > _stumble_total - 18)
            {
                _horse_frame = HF_STUMBLE;
            }
            else if(_airborne)
            {
                _horse_frame = HF_JUMP;
            }
            else if(_speed < bn::fixed(0.3))
            {
                _horse_frame = HF_STAND;
            }
            else if(_finished)
            {
                _anim += _speed * bn::fixed(0.085);

                if(_anim >= 4)
                {
                    _anim -= 4;
                }

                _horse_frame = HF_GALLOP + _anim.integer();
            }
            else
            {
                _horse_frame = HF_GALLOP + (_t % beat_frames) * 4 / beat_frames;
            }

            _rival_anim += bn::max(_rival_speed, bn::fixed(0)) * bn::fixed(0.085);

            if(_rival_anim >= 4)
            {
                _rival_anim -= 4;
            }

            _rival_frame = _rival_speed < bn::fixed(0.3) ? int(HF_STAND) : HF_GALLOP + _rival_anim.integer();
        }

        // ----------------------------------------------------------------------------- HUD
        void update_hud()
        {
            int frames = _finished ? _finish_frames : _t;
            int tenths = frames_to_hundredths(frames) / 10;

            if(tenths != _shown_tenths)
            {
                _shown_tenths = tenths;
                _time_label.set(-114, -71, time_text(tenths * 10), sb::text_color::WHITE);
            }

            // 1st or 2nd.
            int place = _finished ? (won() ? 1 : 2) : (_distance >= _rival_x ? 1 : 2);

            if(place != _shown_place)
            {
                _shown_place = place;
                _place_label.set(114, -71, place == 1 ? "1st" : "2nd",
                                 place == 1 ? sb::text_color::YELLOW : sb::text_color::WHITE, sb::align::RIGHT);
            }

            // Progress bar: where you and the rival are between the start and the finish.
            auto bar_x = [&](bn::fixed x) {
                bn::fixed t = bn::clamp(x / _finish_x, bn::fixed(0), bn::fixed(1));
                return BAR_LEFT + (t * (BAR_RIGHT - BAR_LEFT)).integer();
            };

            _you_pin.set_x(bar_x(_distance));
            _rival_pin.set_x(bar_x(_rival_x));

            // Stamina.
            int level = bn::clamp((_stamina * 8 / STAMINA_MAX).ceil_integer(), 0, 8);
            _stamina_bar.set_tiles(bn::sprite_items::horse_stamina.tiles_item(), level);

            _stamina_bar.set_visible(_tired == 0 || (_tired / 8) % 2 == 0);

            // Arrow, name and gap when the rival is off screen (ahead or behind).
            int rival_sx = PLAYER_X + (_rival_x - _distance).integer();
            int hint = rival_sx > 140 ? 1 : rival_sx < -140 ? -1 : 0;
            int gap_m = bn::abs((_rival_x - _distance).integer()) / PIXELS_PER_METRE;

            if(hint != _shown_rival_hint || (hint != 0 && gap_m != _shown_hint_m))
            {
                _shown_rival_hint = hint;
                _shown_hint_m = gap_m;

                if(hint == 0)
                {
                    _rival_hint.clear();
                }
                else
                {
                    bn::string<24> text;

                    if(hint > 0)
                    {
                        text.append(sb::character_name(_rival));
                        text.append(" +");
                        text.append(sb::to_text(gap_m));
                        text.append("m >");
                        _rival_hint.set(114, -56, text, sb::text_color::WHITE, sb::align::RIGHT);
                    }
                    else
                    {
                        text.append("< ");
                        text.append(sb::character_name(_rival));
                        text.append(" -");
                        text.append(sb::to_text(gap_m));
                        text.append("m");
                        _rival_hint.set(-114, -56, text, sb::text_color::WHITE);
                    }
                }
            }
        }

        void draw()
        {
            int d = _distance.integer();
            _far.set_x(8 - (d / 8) % 256);
            _mid.set_x(136 - (d / 3) % 512);
            _near.set_x(8 - d % 256);

            // Player.
            int bob = frame_bob[_horse_frame];
            int hy = GROUND_Y - 15 - _height.integer();
            _horse.set_tiles(bn::sprite_items::horse_horse.tiles_item(), _horse_frame);
            _horse.set_position(PLAYER_X, hy);
            _rider.set_position(PLAYER_X + RIDER_DX, hy + RIDER_DY + bob);
            bool visible = _invulnerable == 0 || (_invulnerable / 4) % 2 == 0;
            _horse.set_visible(visible);
            _rider.set_visible(visible);

            for(obstacle& o : _obstacles)
            {
                o.sprite.set_x(screen_x(o.x));
            }

            if(_finish)
            {
                int sx = screen_x(_finish_x);
                bool on_screen = sx > -150 && sx < 150;
                _finish->set_x(sx + FINISH_POLE_DX);
                _finish_front->set_x(sx);
                _finish_back->set_x(sx);
                _finish->set_visible(on_screen);
                _finish_front->set_visible(on_screen);
                _finish_back->set_visible(on_screen);
            }

            // Rival.
            int rx = PLAYER_X + (_rival_x - _distance).integer();
            bool rival_visible = rx > -150 && rx < 150;
            _rival_horse.set_visible(rival_visible);
            _rival_rider.set_visible(rival_visible);

            if(rival_visible)
            {
                int ry = RIVAL_GROUND_Y - 15;
                _rival_horse.set_tiles(bn::sprite_items::horse_horse.tiles_item(), _rival_frame);
                _rival_horse.set_position(rx, ry);
                _rival_rider.set_position(rx + RIDER_DX, ry + RIDER_DY + frame_bob[_rival_frame]);
            }
        }
    };

    int g_level_choice = 0;

    bool choose_level()
    {
        sb::label title;
        title.set(0, -60, "Which race?", sb::text_color::WHITE, sb::align::CENTER);
        constexpr bn::string_view items[] = { "Easy", "Medium", "Hard" };
        int choice = sb::menu(11, 5, items, g_level_choice, true);
        sb::panel_clear();

        if(choice < 0)
        {
            return false;
        }

        g_level_choice = choice;
        return true;
    }

    // The result box: who won, your time, and the best time in every race. Returns true to race again.
    bool show_results(const race& game)
    {
        int hundredths = game.finish_hundredths();
        bool record = sb::submit_extra(HORSE_RECORD_SLOT + game.level(), HORSE_RECORD_BASE - hundredths,
                                       sb::player_character());
        sb::play_jingle(record || game.won() ? bn::music_items::jingle_record : bn::music_items::jingle_over);

        // Everything fits in the box (rows 0..13) so the menu below (rows 14..19) doesn't cover it.
        sb::panel(3, 0, 24, 14);
        sb::label title, result, detail, stats, heading, lines[LEVEL_COUNT], record_label;

        bn::string<40> text;

        if(game.won())
        {
            text.append("1st! You beat ");
            text.append(sb::character_name(game.rival()));
        }
        else
        {
            text.append("2nd. ");
            text.append(sb::character_name(game.rival()));
            text.append(" won");
        }

        title.set(0, -71, text, game.won() ? sb::text_color::BLUE : sb::text_color::RED, sb::align::CENTER);

        bn::string<40> line(LEVELS[game.level()].name);
        line.append(": ");
        line.append(time_text(hundredths));
        line.append("s");
        int gap = bn::abs(game.rival_hundredths() - hundredths);
        line.append(game.won() ? "  (by " : "  (");
        line.append(time_text(gap));
        line.append(game.won() ? "s)" : "s behind)");
        result.set(0, -59, line, sb::text_color::DARK, sb::align::CENTER);

        bn::string<40> rhythm("Perfect beats ");
        rhythm.append(sb::to_text(game.perfects()));
        rhythm.append("/");
        rhythm.append(sb::to_text(game.beats()));
        detail.set(0, -48, rhythm, sb::text_color::DARK, sb::align::CENTER);

        bn::string<40> jumps("Clean jumps ");
        jumps.append(sb::to_text(game.jumps_clean()));
        jumps.append("/");
        jumps.append(sb::to_text(game.jumps_total()));
        stats.set(0, -37, jumps, sb::text_color::DARK, sb::align::CENTER);
        heading.set(0, -25, "{ Best times {", sb::text_color::RED, sb::align::CENTER);

        for(int l = 0; l < LEVEL_COUNT; ++l)
        {
            bn::string<40> best(LEVELS[l].name);
            best.append(":  ");
            int value = sb::extra_score(HORSE_RECORD_SLOT + l);

            if(value > 0)
            {
                best.append(time_text(HORSE_RECORD_BASE - value));
                best.append("s  ");
                best.append(sb::extra_name(HORSE_RECORD_SLOT + l));
            }
            else
            {
                best.append("---");
            }

            lines[l].set(-72, -13 + l * 11, best, l == game.level() ? sb::text_color::BLUE : sb::text_color::DARK);
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

        constexpr bn::string_view items[] = { "Race again", "Back to map" };
        int choice = sb::menu(8, 14, items, 0, true);
        sb::panel_clear();
        return choice == 0;
    }

    bn::string<16> record_value_text(int value)
    {
        bn::string<16> text = time_text(HORSE_RECORD_BASE - value);
        text.append("s");
        return text;
    }
}

// The best record shown on the map, the intro and the high score list (sb_games.h: game_info::best_text):
// the hardest race with a time, e.g. "Hard 41.3s by Adam".
bn::string<40> sb::horse_best_text(bool compact)
{
    static constexpr const char* names[] = { "Easy", "Medium", "Hard" };
    return level_record_text(HORSE_RECORD_SLOT, LEVEL_COUNT, names, record_value_text, compact);
}

void sb::horse_run()
{
    // Shown on the start screen (scroll with Up/Down) and in the pause menu. '#' = heading.
    static constexpr bn::string_view howto[] = {
        "#Race to the finish",
        "Beat your rival to the",
        "finish post! The bar at",
        "the top shows where you",
        "both are.",
        "",
        "#Gallop in rhythm",
        "Horseshoes slide to the",
        "ring at the bottom, one",
        "per hoofbeat. Tap B (or",
        ">) just as each one hits",
        "the ring: Perfect taps",
        "make you faster.",
        "Tapping off the beat or",
        "mashing tires your",
        "horse. Watch the green",
        "bar: empty = Tired!",
        "",
        "#Jump",
        "Tap A: small jump for",
        "low fences, hay and",
        "snow drifts.",
        "Hold A: big jump for",
        "stone walls, tall and",
        "double fences, and",
        "water (take off right at",
        "the edge). Big jumps",
        "cost a little stamina.",
        "",
        "#Records",
        "Each race has the same",
        "jumps every time. Learn",
        "it and beat your time!",
    };

    {
        race game;
        play_music(bn::music_items::horse_song);

        if(bn::bg_palettes::fade_intensity() > 0)
        {
            fade_in();
        }

        if(game_intro(GAME_HORSE, howto))
        {
            while(choose_level())
            {
                game.reset(g_level_choice);
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
