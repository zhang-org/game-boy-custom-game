// Ice Fishing: hold A to cast through the hole in the ice, wait for a fish to bite ("!"),
// press A to hook it, then press A while the marker is in the green zone to reel it in.
// Bigger fish live deeper, are worth more and are harder to reel. 90 seconds on the clock.
//
// Scene layout (screen pixels; the art in tools/art/make_fishing.py uses the same numbers):
// the ice slab is at y 64..71, the fishing hole at x 58..72, the player sits on a stool at x 28.
#include "bn_fixed.h"
#include "bn_keypad.h"
#include "bn_log.h"
#include "bn_math.h"
#include "bn_optional.h"
#include "bn_regular_bg_ptr.h"
#include "bn_sprite_affine_mat_ptr.h"
#include "bn_sprite_item.h"
#include "bn_sprite_ptr.h"
#include "bn_string.h"
#include "bn_string_view.h"
#include "bn_vector.h"

#include "bn_music_items.h"
#include "bn_regular_bg_items_fishing_bg.h"
#include "bn_sound_items.h"
#include "bn_sprite_items_fishing_bang.h"
#include "bn_sprite_items_fishing_bar.h"
#include "bn_sprite_items_fishing_bobber.h"
#include "bn_sprite_items_fishing_boot.h"
#include "bn_sprite_items_fishing_bubble.h"
#include "bn_sprite_items_fishing_carp.h"
#include "bn_sprite_items_fishing_hook.h"
#include "bn_sprite_items_fishing_line.h"
#include "bn_sprite_items_fishing_marker.h"
#include "bn_sprite_items_fishing_meter.h"
#include "bn_sprite_items_fishing_minnow.h"
#include "bn_sprite_items_fishing_perch.h"
#include "bn_sprite_items_fishing_rod.h"
#include "bn_sprite_items_fishing_salmon.h"
#include "bn_sprite_items_fishing_sparkle.h"
#include "bn_sprite_items_fishing_splash.h"
#include "bn_sprite_items_fishing_trout.h"
#include "bn_sprite_items_fishing_zone.h"

#include "sb_audio.h"
#include "sb_characters.h"
#include "sb_core.h"
#include "sb_games.h"
#include "sb_save.h"
#include "sb_text.h"
#include "sb_ui.h"

namespace
{
    // =========================================================================== tuning
    // (60 frames = 1 second)

    constexpr int time_limit_seconds = 90;          // length of a round
    constexpr int countdown_beep_from = 5;          // beep every second for the last N seconds
    constexpr int red_time_from = 10;               // the clock turns yellow for the last N seconds

    // Casting
    constexpr int charge_frames = 70;               // frames for the power meter to go empty -> full (then back down)
    constexpr bn::fixed drop_speed = 3;             // px/frame the hook falls through the air
    constexpr bn::fixed sink_speed = bn::fixed(3) / 2;  // px/frame the hook sinks in the water
    constexpr bn::fixed depth_adjust_speed = bn::fixed(3) / 4;  // px/frame Up/Down moves the hook while waiting

    // Waiting for a bite
    constexpr int wait_min_frames = 30;             // shortest wait before a fish starts swimming to the hook
    constexpr int wait_max_frames = 100;            // longest wait
    constexpr bn::fixed approach_speed = bn::fixed(5) / 4;  // px/frame a fish swims towards the hook
    constexpr int nibble_min_frames = 30;           // a fish nibbles (A is "too soon") for this long...
    constexpr int nibble_max_frames = 70;           // ...up to this long, then bites
    constexpr int bite_window_frames = 50;          // time to press A once the "!" shows (generous)
    constexpr int result_hint_frames = 70;          // how long "Too soon!" / "Too slow!" stays up

    // Reeling (a marker sweeps over a bar; press A while it's in the green zone)
    constexpr int reel_track_width = 112;           // px of bar the marker sweeps over
    constexpr int reel_misses_allowed = 2;          // misses forgiven per fish (the next one lets it go)
    constexpr int reel_timeout_frames = 9 * 60;     // the fish escapes if not landed in this time
    constexpr int reel_tolerance = 2;               // px of slack either side of the green zone

    // Fish in the pond
    constexpr int fish_slots = 8;                   // fish swimming at once
    constexpr int respawn_frames = 80;              // delay before a caught/fled fish is replaced
    constexpr int catch_show_frames = 130;          // how long the "You caught..." box stays up
    constexpr int catch_skip_frames = 30;           // A can close that box after this many frames

    struct fish_type
    {
        const char* name;
        int points;
        const bn::sprite_item* item;
        int mouth;            // x offset of the mouth from the sprite centre (fish face right)
        bn::fixed speed;      // swimming speed, px/frame
        int reel_hits;        // green-zone hits needed to land it
        int zone_width;       // px width of the green zone
        bn::fixed marker_speed;  // px/frame of the reel marker
    };

    enum fish_kind { MINNOW, PERCH, TROUT, SALMON, CARP, BOOT, KIND_COUNT };

    // Points and reel difficulty per fish (bigger = more points, smaller zone, faster marker).
    const fish_type fish_types[KIND_COUNT] = {
        { "Snow Minnow",  10,  &bn::sprite_items::fishing_minnow, 10, bn::fixed(1) / 2,  2, 50, bn::fixed(5) / 4 },
        { "Pine Perch",   20,  &bn::sprite_items::fishing_perch,  11, bn::fixed(2) / 5,  2, 44, bn::fixed(11) / 8 },
        { "Frost Trout",  35,  &bn::sprite_items::fishing_trout,  13, bn::fixed(3) / 5,  3, 40, bn::fixed(3) / 2 },
        { "Berry Salmon", 50,  &bn::sprite_items::fishing_salmon, 14, bn::fixed(7) / 10, 3, 36, bn::fixed(13) / 8 },
        { "Golden Carp",  100, &bn::sprite_items::fishing_carp,   13, bn::fixed(1) / 3,  3, 30, bn::fixed(15) / 8 },
        { "Old Boot",     1,   &bn::sprite_items::fishing_boot,   10, bn::fixed(1) / 8,  1, 60, 1 },
    };

    // Depth lanes (fish centre y, Butano coordinates), shallow to deep.
    constexpr int lane_count = 5;
    constexpr int lane_y[lane_count] = { 2, 16, 30, 44, 58 };

    // Which lane each fish slot swims in (so every depth always has fish).
    constexpr int slot_lane[fish_slots] = { 0, 0, 1, 1, 2, 2, 3, 4 };

    // Chance (weights) of each kind turning up in each lane: minnow, perch, trout, salmon, carp, boot.
    constexpr int lane_weights[lane_count][KIND_COUNT] = {
        { 80, 20,  0,  0,  0,  0 },
        { 30, 60, 10,  0,  0,  0 },
        {  0, 30, 60, 10,  0,  0 },
        {  0,  0, 30, 55,  7,  8 },
        {  0,  0,  0, 45, 25, 30 },
    };

    // =========================================================================== layout
    // Butano coordinates: (0, 0) is the screen centre.
    constexpr int char_x = -84;          // player sprite centre (sits on the stool)
    constexpr int char_y = -36;
    constexpr int rod_x = -68;           // rod sprite centre (hand at its pixel (3, 27))
    constexpr int rod_y = -38;
    constexpr int rod_tip_x[3] = { -55, -75, -55 };   // tip of each rod frame
    constexpr int rod_tip_y[3] = { -50, -54, -42 };
    enum rod_frame { ROD_OUT = 0, ROD_RAISED, ROD_BENT };

    constexpr int water_y = -16;         // water surface in the hole (bobber floats here)
    constexpr int hook_min_y = lane_y[0] - 5;          // shallowest hook (line end) position
    constexpr int hook_max_y = lane_y[lane_count - 1] - 5;  // deepest
    constexpr int hook_hang = 18;        // how far the hook hangs below the rod tip out of the water
    constexpr int reel_top_y = -10;      // hook height when the last reel hit is about to land the fish
    constexpr int land_y = -46;          // hook height when the fish is pulled out of the water
    constexpr int fish_left = -136;      // fish turn around (or leave) past these x positions
    constexpr int fish_right = 136;

    constexpr int bar_x = -34;           // reel bar left edge (4 pieces of 32 px)
    constexpr int bar_y = -52;           // reel bar centre y
    constexpr int track_x0 = bar_x + 8;  // first px of the track the marker sweeps
    constexpr int meter_x = -104;        // power meter centre
    constexpr int meter_y = -38;

    constexpr int trophy_x = -18;        // the caught fish shown (2x) in the catch box
    constexpr int trophy_y = -40;

    constexpr int max_line_pieces = 16;
    constexpr int max_zone_pieces = 8;
    constexpr int bubble_count = 3;

    // =========================================================================== state
    enum class phase { READY, CHARGE, DROP, WAIT, REEL, LAND, SHOW, REEL_IN };
    enum class fish_mode { SWIM, APPROACH, NIBBLE, BITE, HOOKED, FLEE, GONE };

    struct fish
    {
        int kind = MINNOW;
        int lane = 0;
        bn::fixed x, y;
        int dir = 1;              // 1 = swimming right, -1 = left
        fish_mode mode = fish_mode::SWIM;
        int timer = 0;            // nibble countdown / respawn countdown
        int anim = 0;
        bn::optional<bn::sprite_ptr> sprite;
    };

    const char* phase_name(phase p)
    {
        switch(p)
        {
        case phase::READY: return "READY";
        case phase::CHARGE: return "CHARGE";
        case phase::DROP: return "DROP";
        case phase::WAIT: return "WAIT";
        case phase::REEL: return "REEL";
        case phase::LAND: return "LAND";
        case phase::SHOW: return "SHOW";
        default: return "REEL_IN";
        }
    }

    int pick_kind(int lane)
    {
        int total = 0;

        for(int w : lane_weights[lane])
        {
            total += w;
        }

        int r = sb::rand_range(0, total - 1);

        for(int k = 0; k < KIND_COUNT; ++k)
        {
            r -= lane_weights[lane][k];

            if(r < 0)
            {
                return k;
            }
        }

        return MINNOW;
    }

    class fishing_game
    {
    public:
        fishing_game() :
            _bg(bn::regular_bg_items::fishing_bg.create_bg(8, 48)),
            _player(sb::character_sprite(sb::player_character()).create_sprite(char_x, char_y, sb::FRAME_SIDE_SIT)),
            _rod(bn::sprite_items::fishing_rod.create_sprite(rod_x, rod_y, ROD_OUT)),
            _bobber(bn::sprite_items::fishing_bobber.create_sprite(0, 0)),
            _hook(bn::sprite_items::fishing_hook.create_sprite(0, 0)),
            _bang(bn::sprite_items::fishing_bang.create_sprite(char_x + 14, char_y - 24)),
            _meter(bn::sprite_items::fishing_meter.create_sprite(meter_x, meter_y)),
            _marker(bn::sprite_items::fishing_marker.create_sprite(0, bar_y)),
            _splash(bn::sprite_items::fishing_splash.create_sprite(0, water_y - 6)),
            _sparkle(bn::sprite_items::fishing_sparkle.create_sprite(0, 0)),
            _trophy(bn::sprite_items::fishing_minnow.create_sprite(trophy_x, trophy_y))
        {
            _bg.set_priority(3);
            _player.set_bg_priority(2);
            _rod.set_bg_priority(2);
            _rod.set_z_order(-1);
            setup(_bobber, 2, 1);
            setup(_hook, 2, 1);
            setup(_bang, 1, -2);
            setup(_meter, 1, 0);
            setup(_marker, 1, -3);
            setup(_splash, 2, -1);
            setup(_sparkle, 1, -2);
            _trophy.set_bg_priority(0);
            _trophy.set_scale(2);
            _trophy.set_visible(false);

            for(int i = 0; i < max_line_pieces; ++i)
            {
                bn::sprite_ptr piece = bn::sprite_items::fishing_line.create_sprite(0, 0);
                setup(piece, 2, 2);
                _line.push_back(piece);
            }

            for(int i = 0; i < 4; ++i)
            {
                bn::sprite_ptr piece = bn::sprite_items::fishing_bar.create_sprite(
                        bar_x + 16 + i * 32, bar_y, i == 0 ? 0 : i == 3 ? 2 : 1);
                setup(piece, 1, 0);
                _bar.push_back(piece);
            }

            for(int i = 0; i < max_zone_pieces; ++i)
            {
                bn::sprite_ptr piece = bn::sprite_items::fishing_zone.create_sprite(0, bar_y);
                setup(piece, 1, -1);
                _zone.push_back(piece);
            }

            for(int i = 0; i < bubble_count; ++i)
            {
                bn::sprite_ptr bubble = bn::sprite_items::fishing_bubble.create_sprite(0, 0);
                setup(bubble, 2, 1);
                bubble.set_visible(true);
                _bubbles.push_back(bubble);
                _bubble_x[i] = sb::rand_range(-110, 110);
                _bubble_y[i] = sb::rand_range(-8, 72);
            }

            for(int i = 0; i < fish_slots; ++i)
            {
                _fish.emplace_back();
            }

            reset();
        }

        // Sets up a new round (also used for "Play again").
        void reset()
        {
            _score = 0;
            _catches = 0;
            _best_kind = -1;
            _frames_left = time_limit_seconds * 60;
            _time_up = false;
            _shown_seconds = -1;
            _shown_score = -1;
            _active = -1;
            _hint_timer = 0;
            _hint_text = "";
            _player.set_tiles(sb::character_sprite(sb::player_character()).tiles_item(), sb::FRAME_SIDE_SIT);
            _player.set_visible(true);
            _rod.set_visible(true);
            _trophy.set_visible(false);
            _trophy_sparkle = false;
            sb::panel_clear();
            _title_label.clear();
            _points_label.clear();
            _big_label.clear();

            for(int i = 0; i < fish_slots; ++i)
            {
                spawn_fish(i, false);
            }

            enter(phase::READY);
            update_hud();
            draw();
        }

        // Plays one frame. Returns false when the round is over.
        bool frame()
        {
            ++_t;

            if(_phase != phase::SHOW && ! _time_up)
            {
                --_frames_left;

                if(_frames_left <= 0)
                {
                    _frames_left = 0;
                    _time_up = true;
                    BN_LOG("fishing: time up");
                }
                else if(_frames_left % 60 == 0 && _frames_left / 60 <= countdown_beep_from)
                {
                    sb::sfx(bn::sound_items::sfx_countdown);
                }
            }

            update_phase();
            update_fish();
            update_bubbles();

            if(_hint_timer > 0)
            {
                --_hint_timer;

                if(_hint_timer == 0)
                {
                    _hint_text = "";
                }
            }

            update_hud();
            draw();

            // When time is up, finish the fish that's on the line (bite, reel or landing) first.
            if(_time_up && ! busy())
            {
                return false;
            }

            return true;
        }

        void show_time_up()
        {
            _hint_label.clear();
            _shown_hint = "";
            _big_label.set(0, -8, "Time's up!", sb::text_color::YELLOW, sb::align::CENTER);
            sb::sfx(bn::sound_items::sfx_whoosh);
            _bang.set_visible(false);
            _meter.set_visible(false);
            sb::wait_frames(70);
            _big_label.clear();
        }

        // Hides the HUD text while the intro / game over boxes are up.
        void set_hud_visible(bool visible)
        {
            _score_label.set_visible(visible);
            _time_label.set_visible(visible);
            _hint_label.set_visible(visible);
        }

        int score() const
        {
            return _score;
        }

        bn::string<40> detail() const
        {
            bn::string<40> text;

            if(_catches == 0)
            {
                text = "No bites this time!";
            }
            else
            {
                text.append("Caught ");
                text.append(sb::to_text(_catches));
                text.append("! Best: ");
                text.append(fish_types[_best_kind].name);
            }

            return text;
        }

    private:
        bn::regular_bg_ptr _bg;
        bn::sprite_ptr _player;
        bn::sprite_ptr _rod;
        bn::sprite_ptr _bobber;
        bn::sprite_ptr _hook;
        bn::sprite_ptr _bang;
        bn::sprite_ptr _meter;
        bn::sprite_ptr _marker;
        bn::sprite_ptr _splash;
        bn::sprite_ptr _sparkle;
        bn::sprite_ptr _trophy;
        bn::vector<bn::sprite_ptr, max_line_pieces> _line;
        bn::vector<bn::sprite_ptr, 4> _bar;
        bn::vector<bn::sprite_ptr, max_zone_pieces> _zone;
        bn::vector<bn::sprite_ptr, bubble_count> _bubbles;
        int _bubble_x[bubble_count] = {};
        bn::fixed _bubble_y[bubble_count];
        bn::vector<fish, fish_slots> _fish;

        sb::label _score_label, _time_label, _hint_label, _title_label, _points_label, _big_label;
        bn::string<32> _shown_hint;
        sb::text_color _shown_hint_color = sb::text_color::WHITE;

        phase _phase = phase::READY;
        int _phase_t = 0;          // frames in the current phase
        int _t = 0;
        int _score = 0;
        int _catches = 0;
        int _best_kind = -1;
        int _frames_left = 0;
        bool _time_up = false;
        int _shown_seconds = -1;
        int _shown_score = -1;

        // Line and hook
        int _rod_frame = ROD_OUT;
        bn::fixed _hook_y;
        bn::fixed _hook_target;
        int _charge = 0;           // 0 .. charge_frames * 2 (ping-pong)
        int _wait_timer = 0;
        int _splash_t = -1;
        bool _hook_wet = false;

        // The fish that's interested / on the hook (-1 if none)
        int _active = -1;
        int _bite_timer = 0;

        // Reeling
        int _hits = 0;
        int _misses = 0;
        bn::fixed _marker_pos;     // 0 .. reel_track_width
        int _marker_dir = 1;
        int _zone_pos = 0;         // left edge of the zone, 0 .. track width
        bn::fixed _reel_start_y;

        // Messages
        const char* _hint_text = "";
        int _hint_timer = 0;
        bool _trophy_sparkle = false;

        static void setup(bn::sprite_ptr& sprite, int priority, int z)
        {
            sprite.set_bg_priority(priority);
            sprite.set_z_order(z);
            sprite.set_visible(false);
        }

        // ------------------------------------------------------------------ helpers
        int line_x() const
        {
            return rod_tip_x[_rod_frame];
        }

        int tip_y() const
        {
            return rod_tip_y[_rod_frame];
        }

        bool busy() const
        {
            if(_phase == phase::REEL || _phase == phase::LAND || _phase == phase::SHOW)
            {
                return true;
            }

            return _active >= 0 && _fish[_active].mode == fish_mode::BITE;
        }

        void enter(phase p)
        {
            _phase = p;
            _phase_t = 0;
            BN_LOG("fishing: phase ", phase_name(p), " f=", sb::frame_count());

            switch(p)
            {
            case phase::READY:
                _rod_frame = ROD_OUT;
                _hook_y = tip_y() + hook_hang;
                _hook_wet = false;
                break;

            case phase::CHARGE:
                _charge = 0;
                _rod_frame = ROD_RAISED;
                _hook_y = tip_y() + hook_hang - 4;
                break;

            case phase::WAIT:
                _wait_timer = sb::rand_range(wait_min_frames, wait_max_frames);
                break;

            default:
                break;
            }
        }

        void hint(const char* text, int frames)
        {
            _hint_text = text;
            _hint_timer = frames;
        }

        void spawn_fish(int slot, bool from_edge)
        {
            fish& f = _fish[slot];
            f.lane = slot_lane[slot];
            f.kind = pick_kind(f.lane);
            f.mode = fish_mode::SWIM;
            f.dir = sb::rand_range(0, 1) ? 1 : -1;

            if(from_edge)
            {
                f.x = f.dir > 0 ? fish_left + 8 : fish_right - 8;
            }
            else
            {
                f.x = sb::rand_range(-100, 100);
            }

            f.y = lane_y[f.lane] + sb::rand_range(-3, 3);
            f.anim = sb::rand_range(0, 63);
            const fish_type& type = fish_types[f.kind];

            if(f.sprite)
            {
                f.sprite->set_item(*type.item, 0);
            }
            else
            {
                f.sprite = type.item->create_sprite(0, 0);
                f.sprite->set_bg_priority(2);
            }

            f.sprite->remove_affine_mat();
            f.sprite->set_visible(true);
        }

        bn::fixed mouth_target_x(const fish& f) const
        {
            return line_x() + 1 - f.dir * (fish_types[f.kind].mouth + 1);
        }

        bn::fixed mouth_target_y() const
        {
            return _hook_y + 5;
        }

        void fish_flee(int index)
        {
            fish& f = _fish[index];
            f.mode = fish_mode::FLEE;
            f.dir = f.x < line_x() ? -1 : 1;

            if(f.sprite->affine_mat())
            {
                f.sprite->remove_affine_mat();
            }

            if(index == _active)
            {
                _active = -1;
            }
        }

        // ------------------------------------------------------------------ phases
        void update_phase()
        {
            ++_phase_t;

            if(bn::keypad::start_pressed())
            {
                return;   // handled by the caller (pause)
            }

            switch(_phase)
            {
            case phase::READY:
                if(! _time_up)
                {
                    if(_hint_timer == 0)
                    {
                        _hint_text = "Hold A to cast!";
                    }

                    if(bn::keypad::a_pressed())
                    {
                        enter(phase::CHARGE);
                    }
                }
                break;

            case phase::CHARGE:
                ++_charge;

                if(_charge >= charge_frames * 2)
                {
                    _charge = 0;
                }

                if(_hint_timer == 0)
                {
                    _hint_text = "Let go to cast!";
                }

                if(bn::keypad::b_pressed())
                {
                    enter(phase::READY);
                }
                else if(! bn::keypad::a_held())
                {
                    int power = _charge <= charge_frames ? _charge : charge_frames * 2 - _charge;
                    _hook_target = hook_min_y + bn::fixed(hook_max_y - hook_min_y) * power / charge_frames;
                    sb::sfx(bn::sound_items::sfx_cast);
                    BN_LOG("fishing: cast power ", power, "/", charge_frames, " depth ", _hook_target.integer());
                    enter(phase::DROP);
                    _rod_frame = ROD_OUT;
                    _hook_y = tip_y() + 4;
                    _hint_text = "";
                }
                break;

            case phase::DROP:
                _hook_y += _hook_wet ? sink_speed : drop_speed;

                if(! _hook_wet && _hook_y >= water_y)
                {
                    _hook_wet = true;
                    _splash_t = 0;
                    sb::sfx(bn::sound_items::sfx_splash);
                }

                if(_hook_y >= _hook_target)
                {
                    _hook_y = _hook_target;
                    enter(phase::WAIT);
                }
                break;

            case phase::WAIT:
                update_wait();
                break;

            case phase::REEL:
                update_reel();
                break;

            case phase::LAND:
                _hook_y -= 4;

                if(_hook_y <= land_y)
                {
                    _hook_y = land_y;
                    land_fish();
                }
                break;

            case phase::SHOW:
                if((_phase_t > catch_skip_frames && bn::keypad::a_pressed()) || _phase_t >= catch_show_frames)
                {
                    sb::panel_clear();
                    _title_label.clear();
                    _points_label.clear();
                    _trophy.set_visible(false);
                    _trophy.remove_affine_mat();
                    _trophy_sparkle = false;
                    _player.set_tiles(sb::character_sprite(sb::player_character()).tiles_item(), sb::FRAME_SIDE_SIT);
                    _rod.set_visible(true);
                    enter(phase::READY);
                }
                break;

            case phase::REEL_IN:
                _hook_y -= 3;

                if(_hook_y <= water_y)
                {
                    _hook_wet = false;
                }

                if(_hook_y <= tip_y() + hook_hang)
                {
                    enter(phase::READY);
                }
                break;

            default:
                break;
            }
        }

        void update_wait()
        {
            fish* f = _active >= 0 ? &_fish[_active] : nullptr;
            bool engaged = f && (f->mode == fish_mode::NIBBLE || f->mode == fish_mode::BITE);

            if(! engaged && _hint_timer == 0)
            {
                _hint_text = "Wait for the !   ^_ depth";
            }

            // Move the hook up and down while nothing is nibbling.
            if(! engaged)
            {
                if(bn::keypad::up_held())
                {
                    _hook_y = bn::max(_hook_y - depth_adjust_speed, bn::fixed(hook_min_y));
                }
                else if(bn::keypad::down_held())
                {
                    _hook_y = bn::min(_hook_y + depth_adjust_speed, bn::fixed(hook_max_y));
                }
            }

            if(bn::keypad::b_pressed() && ! engaged)
            {
                // Reel the line back in to cast again.
                if(f)
                {
                    fish_flee(_active);
                }

                enter(phase::REEL_IN);
                return;
            }

            if(! f)
            {
                if(! _time_up && --_wait_timer <= 0)
                {
                    choose_fish();
                }

                return;
            }

            switch(f->mode)
            {
            case fish_mode::NIBBLE:
                _hint_text = "Nibble... wait!";

                if(bn::keypad::a_pressed())
                {
                    BN_LOG("fishing: too soon");
                    sb::sfx(bn::sound_items::sfx_miss);
                    hint("Too soon! Wait for the !", result_hint_frames);
                    fish_flee(_active);
                    enter(phase::WAIT);
                }
                else if(--f->timer <= 0)
                {
                    f->mode = fish_mode::BITE;
                    _bite_timer = bite_window_frames;
                    sb::sfx(bn::sound_items::sfx_bite);
                    BN_LOG("fishing: BITE f=", sb::frame_count());
                }
                break;

            case fish_mode::BITE:
                _hint_text = "NOW! Press A!";

                if(bn::keypad::a_pressed())
                {
                    BN_LOG("fishing: hooked ", fish_types[f->kind].name);
                    sb::sfx(bn::sound_items::sfx_catch);
                    start_reel();
                }
                else if(--_bite_timer <= 0)
                {
                    BN_LOG("fishing: too slow");
                    sb::sfx(bn::sound_items::sfx_miss);
                    hint("Too slow! It swam off.", result_hint_frames);
                    fish_flee(_active);
                    enter(phase::WAIT);
                }
                break;

            default:
                break;
            }
        }

        void choose_fish()
        {
            int best = -1;
            int best_score = 0;
            bn::fixed target = mouth_target_y();

            for(int i = 0; i < _fish.size(); ++i)
            {
                const fish& f = _fish[i];

                if(f.mode != fish_mode::SWIM || bn::abs(f.x) > 110)
                {
                    continue;
                }

                // Fish at the hook's depth come first; among those, closer ones (and a bit of luck).
                int score = bn::abs(lane_y[f.lane] - target).right_shift_integer() * 2 +
                        bn::abs(f.x - line_x()).right_shift_integer() / 8 + sb::rand_range(0, 8);

                if(best < 0 || score < best_score)
                {
                    best = i;
                    best_score = score;
                }
            }

            if(best < 0)
            {
                _wait_timer = 20;
                return;
            }

            fish& f = _fish[best];
            f.mode = fish_mode::APPROACH;
            f.dir = f.x < line_x() ? 1 : -1;
            _active = best;
            BN_LOG("fishing: ", fish_types[f.kind].name, " approaches");
        }

        void start_reel()
        {
            fish& f = _fish[_active];
            f.mode = fish_mode::HOOKED;
            f.sprite->set_horizontal_flip(false);
            _hits = 0;
            _misses = 0;
            _marker_pos = 0;
            _marker_dir = 1;
            _reel_start_y = _hook_y;
            new_zone();
            enter(phase::REEL);
            _rod_frame = ROD_BENT;
        }

        void new_zone()
        {
            int width = fish_types[_fish[_active].kind].zone_width;
            int marker = _marker_pos.integer();

            // Pick a spot that isn't right under the marker, so every hit is a fresh aim.
            for(int tries = 0; tries < 8; ++tries)
            {
                _zone_pos = sb::rand_range(4, reel_track_width - width - 4);

                if(marker < _zone_pos - 12 || marker > _zone_pos + width + 12)
                {
                    break;
                }
            }

            BN_LOG("fishing: zone ", _zone_pos, "..", _zone_pos + width, " marker ", marker, " f=", sb::frame_count());
        }

        void update_reel()
        {
            fish& f = _fish[_active];
            const fish_type& type = fish_types[f.kind];
            _hint_text = _misses ? "Careful! A in the green!" : "Press A in the green!";

            _marker_pos += type.marker_speed * _marker_dir;

            if(_marker_pos >= reel_track_width)
            {
                _marker_pos = reel_track_width;
                _marker_dir = -1;
            }
            else if(_marker_pos <= 0)
            {
                _marker_pos = 0;
                _marker_dir = 1;
            }

            // Pull the fish up a step for every hit.
            bn::fixed target = _reel_start_y + (bn::fixed(reel_top_y) - _reel_start_y) * _hits / type.reel_hits;

            if(_hook_y > target)
            {
                _hook_y = bn::max(_hook_y - 2, target);
            }
            else if(_hook_y < target)
            {
                _hook_y = bn::min(_hook_y + 1, target);
            }

            if(bn::keypad::a_pressed())
            {
                int m = _marker_pos.round_integer();

                if(m >= _zone_pos - reel_tolerance && m <= _zone_pos + type.zone_width + reel_tolerance)
                {
                    ++_hits;
                    BN_LOG("fishing: reel hit ", _hits, "/", type.reel_hits);

                    if(_hits >= type.reel_hits)
                    {
                        sb::sfx(bn::sound_items::sfx_splash);
                        enter(phase::LAND);
                        _rod_frame = ROD_OUT;
                        _hint_text = "";
                        return;
                    }

                    sb::sfx(bn::sound_items::sfx_reel);
                    new_zone();
                }
                else
                {
                    ++_misses;
                    BN_LOG("fishing: reel miss ", _misses, " marker ", m, " f=", sb::frame_count());

                    if(_misses > reel_misses_allowed)
                    {
                        escape();
                        return;
                    }

                    sb::sfx(bn::sound_items::sfx_bump);
                    _hook_y += 6;   // the fish tugs back down a little
                }
            }

            if(_phase_t >= reel_timeout_frames)
            {
                escape();
            }
        }

        void escape()
        {
            BN_LOG("fishing: escaped");
            sb::sfx(bn::sound_items::sfx_miss);
            hint("Oh no, it got away!", result_hint_frames);
            fish_flee(_active);
            _rod_frame = ROD_OUT;
            enter(phase::REEL_IN);
        }

        void land_fish()
        {
            fish& f = _fish[_active];
            const fish_type& type = fish_types[f.kind];
            _score += type.points;
            ++_catches;

            if(_best_kind < 0 || type.points > fish_types[_best_kind].points)
            {
                _best_kind = f.kind;
            }

            BN_LOG("fishing: caught ", type.name, " score ", _score);
            sb::sfx(bn::sound_items::sfx_point);

            // The fish leaves the pond; a new one turns up later.
            f.mode = fish_mode::GONE;
            f.timer = respawn_frames;
            f.sprite->remove_affine_mat();
            f.sprite->set_visible(false);
            _active = -1;

            // Show it off.
            enter(phase::SHOW);
            _hint_text = "";
            _rod.set_visible(false);
            _player.set_tiles(sb::character_sprite(sb::player_character()).tiles_item(), sb::FRAME_CHEER);
            sb::panel(8, 2, 21, 6);
            _trophy.set_item(*type.item, 0);
            _trophy.set_scale(2);
            _trophy.set_position(trophy_x, trophy_y);
            _trophy.set_visible(true);
            _trophy_sparkle = true;
            bn::string<24> title(type.name);
            title.append("!");
            _title_label.set(trophy_x + 36, -48, title, sb::text_color::BLUE);
            bn::string<16> points("+");
            points.append(sb::to_text(type.points));
            _points_label.set(trophy_x + 36, -32, points, sb::text_color::GREEN);
        }

        // ------------------------------------------------------------------ fish
        void update_fish()
        {
            for(int i = 0; i < _fish.size(); ++i)
            {
                fish& f = _fish[i];
                ++f.anim;

                switch(f.mode)
                {
                case fish_mode::SWIM:
                    f.x += fish_types[f.kind].speed * f.dir;

                    if((f.x > fish_right - 16 && f.dir > 0) || (f.x < fish_left + 16 && f.dir < 0) ||
                       sb::rand_range(0, 599) == 0)
                    {
                        f.dir = -f.dir;
                    }

                    f.y = lane_y[f.lane] + (((f.anim / 32) % 4 == 1) ? 1 : ((f.anim / 32) % 4 == 3) ? -1 : 0);
                    break;

                case fish_mode::APPROACH:
                {
                    bn::fixed tx = mouth_target_x(f);
                    bn::fixed ty = mouth_target_y();
                    bn::fixed dx = tx - f.x;
                    bn::fixed dy = ty - f.y;
                    bn::fixed step = approach_speed;
                    f.x += bn::max(-step, bn::min(step, dx));
                    f.y += bn::max(-step, bn::min(step, dy));

                    if(bn::abs(dx) <= 1 && bn::abs(dy) <= 1)
                    {
                        f.x = tx;
                        f.y = ty;
                        f.mode = fish_mode::NIBBLE;
                        f.timer = sb::rand_range(nibble_min_frames, nibble_max_frames);
                        BN_LOG("fishing: nibble f=", sb::frame_count());
                    }
                    break;
                }

                case fish_mode::NIBBLE:
                case fish_mode::BITE:
                    // Stay on the hook, darting in and out a little.
                    f.y = mouth_target_y();
                    f.x = mouth_target_x(f) - ((f.anim / 6) % 2) * f.dir * (f.mode == fish_mode::BITE ? 2 : 1);
                    break;

                case fish_mode::HOOKED:
                {
                    // Hanging from the hook, mouth up, wriggling.
                    f.x = line_x() + 1;
                    f.y = _hook_y + 5 + fish_types[f.kind].mouth;
                    int wiggle = (f.anim / 5) % 4;
                    f.sprite->set_rotation_angle(wiggle == 0 ? 80 : wiggle == 2 ? 100 : 90);
                    break;
                }

                case fish_mode::FLEE:
                    f.x += 3 * f.dir;

                    if(f.y > lane_y[f.lane])
                    {
                        f.y -= 1;
                    }
                    else if(f.y < lane_y[f.lane])
                    {
                        f.y += 1;
                    }

                    if(f.x < fish_left - 16 || f.x > fish_right + 16)
                    {
                        f.mode = fish_mode::GONE;
                        f.timer = respawn_frames;
                        f.sprite->set_visible(false);
                    }
                    break;

                case fish_mode::GONE:
                    if(--f.timer <= 0)
                    {
                        spawn_fish(i, true);
                    }
                    break;

                default:
                    break;
                }

                if(f.mode != fish_mode::GONE)
                {
                    int speed = f.mode == fish_mode::FLEE || f.mode == fish_mode::HOOKED ? 4 : 12;
                    f.sprite->set_tiles(fish_types[f.kind].item->tiles_item(), (f.anim / speed) % 2);
                    f.sprite->set_position(f.x, f.y);

                    if(f.mode != fish_mode::HOOKED)
                    {
                        f.sprite->set_horizontal_flip(f.dir < 0);
                    }
                }
            }
        }

        void update_bubbles()
        {
            for(int i = 0; i < bubble_count; ++i)
            {
                _bubble_y[i] -= bn::fixed(1) / 2;

                if(_bubble_y[i] < -6)
                {
                    _bubble_x[i] = sb::rand_range(-110, 110);
                    _bubble_y[i] = 70;
                }

                int wobble = ((_t + i * 20) / 20) % 2;
                _bubbles[i].set_position(_bubble_x[i] + wobble, _bubble_y[i]);
            }
        }

        // ------------------------------------------------------------------ drawing
        void update_hud()
        {
            if(_shown_score != _score)
            {
                _shown_score = _score;
                bn::string<24> text("Score ");
                text.append(sb::to_text(_score));
                _score_label.set(-112, -72, text, sb::text_color::WHITE);
            }

            int seconds = (_frames_left + 59) / 60;

            if(_shown_seconds != seconds)
            {
                _shown_seconds = seconds;
                bn::string<24> text("Time ");
                text.append(sb::to_text(seconds));
                _time_label.set(0, -72, text, seconds <= red_time_from ? sb::text_color::YELLOW : sb::text_color::WHITE,
                                sb::align::CENTER);
            }

            bool bite = _active >= 0 && _fish[_active].mode == fish_mode::BITE;
            sb::text_color color = bite ? sb::text_color::YELLOW : sb::text_color::WHITE;
            bn::string_view text(_hint_text);

            if(text != _shown_hint || color != _shown_hint_color)
            {
                _shown_hint = text;
                _shown_hint_color = color;

                if(text.empty())
                {
                    _hint_label.clear();
                }
                else
                {
                    _hint_label.set(0, 70, text, color, sb::align::CENTER);
                }
            }
        }

        void draw()
        {
            bool show_line = _phase != phase::SHOW;
            int lx = line_x();
            int top = tip_y() + 1;
            int bottom = _hook_y.integer();

            _rod.set_tiles(bn::sprite_items::fishing_rod.tiles_item(), _rod_frame);

            // Fishing line: 8 px pieces from the rod tip down to the hook.
            for(int i = 0; i < max_line_pieces; ++i)
            {
                int y = top + i * 8;
                bool visible = show_line && y < bottom;

                if(visible)
                {
                    _line[i].set_position(lx + 4, bn::min(y, bottom - 8) + 4);
                }

                _line[i].set_visible(visible);
            }

            // Hook (with bait) at the end of the line.
            _hook.set_visible(show_line);
            _hook.set_position(lx + 1, bottom + 4);

            // Bobber: rides the line above the hook until it floats on the water in the hole.
            fish* f = _active >= 0 ? &_fish[_active] : nullptr;
            bool nibble = f && f->mode == fish_mode::NIBBLE;
            bool bite = f && f->mode == fish_mode::BITE;
            int bob_y = bn::max(bn::min(bottom - 10, water_y), top + 3);
            int bob_frame = 0;

            if(bob_y == water_y)
            {
                if(bite || _phase == phase::REEL)
                {
                    bob_y += 3 + ((_t / 4) % 2);
                    bob_frame = 1;
                }
                else if(nibble)
                {
                    bob_y += (_t % 12) < 4 ? 1 : 0;
                }
                else
                {
                    bob_y += (_t / 30) % 2;
                }
            }

            _bobber.set_visible(show_line);
            _bobber.set_position(lx + 1, bob_y);
            _bobber.set_tiles(bn::sprite_items::fishing_bobber.tiles_item(), bob_frame);

            // "!" bubble over the player during a bite.
            _bang.set_visible(bite);
            _bang.set_tiles(bn::sprite_items::fishing_bang.tiles_item(), (_t / 6) % 2);

            // Cast power meter.
            _meter.set_visible(_phase == phase::CHARGE);

            if(_phase == phase::CHARGE)
            {
                int power = _charge <= charge_frames ? _charge : charge_frames * 2 - _charge;
                _meter.set_tiles(bn::sprite_items::fishing_meter.tiles_item(), power * 12 / charge_frames);
            }

            // Splash where the hook went in.
            if(_splash_t >= 0)
            {
                int fr = _splash_t / 5;

                if(fr >= 3)
                {
                    _splash_t = -1;
                    _splash.set_visible(false);
                }
                else
                {
                    _splash.set_visible(true);
                    _splash.set_position(lx + 1, water_y - 6);
                    _splash.set_tiles(bn::sprite_items::fishing_splash.tiles_item(), fr);
                    ++_splash_t;
                }
            }

            // Reel bar.
            bool reel = _phase == phase::REEL;

            for(bn::sprite_ptr& piece : _bar)
            {
                piece.set_visible(reel);
            }

            _marker.set_visible(reel);

            if(reel)
            {
                const fish_type& type = fish_types[_fish[_active].kind];
                int width = type.zone_width;
                int m = _marker_pos.round_integer();
                bool inside = m >= _zone_pos - reel_tolerance && m <= _zone_pos + width + reel_tolerance;
                int pieces = (width + 7) / 8;

                for(int i = 0; i < max_zone_pieces; ++i)
                {
                    bool visible = i < pieces;
                    _zone[i].set_visible(visible);

                    if(visible)
                    {
                        int x = track_x0 + _zone_pos + bn::min(i * 8, width - 8);
                        _zone[i].set_position(x + 4, bar_y);
                        _zone[i].set_tiles(bn::sprite_items::fishing_zone.tiles_item(), inside ? 1 : 0);
                    }
                }

                _marker.set_position(track_x0 + m - 3 + 4 - 1, bar_y);
            }
            else
            {
                for(bn::sprite_ptr& piece : _zone)
                {
                    piece.set_visible(false);
                }
            }

            // Sparkles: around the first golden carp in the pond, or the fish in the catch box.
            bool sparkle = false;

            _sparkle.set_bg_priority(_trophy_sparkle ? 0 : 1);

            if(_trophy_sparkle)
            {
                int k = (_t / 10) % 4;
                _sparkle.set_position(trophy_x + (k == 0 ? -26 : k == 1 ? 22 : k == 2 ? 10 : -12),
                                      trophy_y + (k == 0 ? -10 : k == 1 ? -6 : k == 2 ? 12 : 10));
                sparkle = true;
            }
            else
            {
                for(const fish& fi : _fish)
                {
                    if(fi.kind == CARP && fi.mode != fish_mode::GONE)
                    {
                        int k = (_t / 16) % 3;
                        _sparkle.set_position(fi.x + (k == 0 ? -6 : k == 1 ? 8 : 0), fi.y + (k == 0 ? -6 : k == 1 ? -2 : 5));
                        sparkle = true;
                        break;
                    }
                }
            }

            _sparkle.set_visible(sparkle);
            _sparkle.set_tiles(bn::sprite_items::fishing_sparkle.tiles_item(), ((_t / 5) % 2));
        }
    };
}

// The best record shown on the map, the intro and the high score list (sb_games.h: game_info::best_text).
bn::string<40> sb::fishing_best_text(bool compact)
{
    return score_record_text(GAME_FISHING, compact);
}

void sb::fishing_run()
{
    // Shown on the start screen (scroll with Up/Down) and in the pause menu. '#' = heading.
    static constexpr bn::string_view howto[] = {
        "#Cast",
        "Hold A, then let go to",
        "cast. Longer = deeper.",
        "",
        "#Bite",
        "Wait for the !, then press",
        "A. Pressing too early",
        "scares the fish away.",
        "",
        "#Reel",
        "Press A when the marker",
        "is in the green.",
        "Deep fish are worth more!",
        "You have 90 seconds.",
    };

    bool leave = false;

    {
        fishing_game game;
        play_music(bn::music_items::fishing_song);
        fade_in();

        game.set_hud_visible(false);

        if(! game_intro(GAME_FISHING, howto))
        {
            leave = true;
        }

        game.set_hud_visible(true);

        while(! leave)
        {
            // Swallow the A press that started the game so it doesn't start a cast.
            update();
            sfx(bn::sound_items::sfx_go);

            while(true)
            {
                if(bn::keypad::start_pressed())
                {
                    if(game_pause())
                    {
                        leave = true;
                        break;
                    }
                }

                if(! game.frame())
                {
                    break;
                }

                update();
            }

            if(leave)
            {
                break;
            }

            game.show_time_up();
            bn::string<40> detail = game.detail();
            game.set_hud_visible(false);
            bool again = game_over(GAME_FISHING, game.score(), detail);
            game.set_hud_visible(true);

            if(again)
            {
                play_music(bn::music_items::fishing_song);
                game.reset();
            }
            else
            {
                leave = true;
            }
        }

        fade_out();
        panel_clear();
    }
}
