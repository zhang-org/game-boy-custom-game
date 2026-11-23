// Ice Fishing: guide your hook up and down through the hole in the ice, hook the fish swimming past and
// bring them up past the drifting ice and the hungry pike.
//
//   * Up/Down (or hold A to reel up) moves the hook. A fish whose mouth touches the hook is caught; bring
//     it up to the hole to land it in the bucket.
//   * Deeper fish are worth more. The big ones (salmon, and the golden carp at the very bottom) ignore a
//     bare hook: they only go for LIVE BAIT. Hook a small fish first and lower it: a bigger fish nearby
//     comes for it and swallows it (bait bonus +50%).
//   * Drifting ice chunks knock your fish off the hook, and the pike steals it (a "!" warns you which
//     depth it's coming at). The old boot on the bottom snags a hook that goes too deep.
//   * Land fish in a row without losing one for a bigger multiplier (x2 after 3, x3 after 6).
//
// Three levels (Easy / Medium / Hard: time, ice, pike), best score per level (sb_save extra records
// FISHING_RECORD_SLOT + level, see docs/decisions/0002-record-slots.md).
//
// The original fishing game (cast / bite / timing bar) is in fishing_classic.cpp, behind SB_FISHING_CLASSIC.
//
// Scene layout (screen pixels; the art in tools/art/make_fishing.py uses the same numbers):
// the ice slab is at y 64..71, the fishing hole at x 58..72, the player sits on a stool at x 28.
#include "bn_bg_palettes.h"
#include "bn_fixed.h"
#include "bn_keypad.h"
#include "bn_log.h"
#include "bn_math.h"
#include "bn_optional.h"
#include "bn_regular_bg_ptr.h"
#include "bn_sprite_item.h"
#include "bn_sprite_ptr.h"
#include "bn_string.h"
#include "bn_string_view.h"
#include "bn_vector.h"

#include "bn_music_items.h"
#include "bn_regular_bg_items_fishing_bg.h"
#include "bn_sound_items.h"
#include "bn_sprite_items_fishing_bang.h"
#include "bn_sprite_items_fishing_bobber.h"
#include "bn_sprite_items_fishing_boot.h"
#include "bn_sprite_items_fishing_bubble.h"
#include "bn_sprite_items_fishing_bucket.h"
#include "bn_sprite_items_fishing_carp.h"
#include "bn_sprite_items_fishing_hook.h"
#include "bn_sprite_items_fishing_ice.h"
#include "bn_sprite_items_fishing_line.h"
#include "bn_sprite_items_fishing_minnow.h"
#include "bn_sprite_items_fishing_perch.h"
#include "bn_sprite_items_fishing_pike.h"
#include "bn_sprite_items_fishing_rod.h"
#include "bn_sprite_items_fishing_salmon.h"
#include "bn_sprite_items_fishing_smoke.h"
#include "bn_sprite_items_fishing_sparkle.h"
#include "bn_sprite_items_fishing_splash.h"
#include "bn_sprite_items_fishing_trout.h"

#include "sb_audio.h"
#include "sb_characters.h"
#include "sb_core.h"
#include "sb_games.h"
#include "sb_save.h"
#include "sb_text.h"
#include "sb_ui.h"

#ifdef SB_FISHING_CLASSIC
namespace sb
{
    void fishing_classic_run();     // fishing_classic.cpp
}
#endif

namespace
{
    // =========================================================================== tuning
    // (60 frames = 1 second; speeds in pixels per frame)

    struct level_def
    {
        const char* name;
        int seconds;            // length of a round
        int ice_chunks;         // drifting ice chunks under the ice
        bn::fixed ice_speed;
        int pikes;              // pikes hunting at once
        bn::fixed pike_speed;
        int pike_rest_min, pike_rest_max;   // frames between a pike's runs
        bn::fixed fish_speed;   // all fish swim this much faster (1 = normal)
    };

    constexpr level_def LEVELS[3] = {
        //  name      time ice speed  pike speed  rest       fish
        { "Easy",     90,  1,  0.3,   1,   1.2,   240, 420,  0.9 },
        { "Medium",   75,  2,  0.4,   1,   1.6,   150, 300,  1 },
        { "Hard",     60,  3,  0.55,  2,   1.9,   120, 240,  1.15 },
    };
    constexpr int LEVEL_COUNT = 3;

    constexpr int countdown_beep_from = 5;          // beep every second for the last N seconds
    constexpr int yellow_time_from = 10;            // the clock turns yellow for the last N seconds

    constexpr bn::fixed HOOK_SPEED = 1.6;           // empty hook, Up/Down
    constexpr int BITE_REACH_X = 4;                 // a fish's mouth this close to the hook bites
    constexpr int BITE_REACH_Y = 5;
    constexpr int LURE_RANGE_Y = 10;                // small fish this close to the hook's depth swim to it
    constexpr int CHASE_RANGE_X = 90;               // a bigger fish this close to live bait comes for it ...
    constexpr int CHASE_RANGE_Y = 26;               // ... if the bait is within this many px of its depth
    constexpr bn::fixed CHASE_SPEED_FACTOR = 1.4;
    constexpr int BAIT_BONUS_PERCENT = 50;
    constexpr int SNAG_FRAMES = 100;                // stuck on the boot
    constexpr int STREAK_FOR_X2 = 3;
    constexpr int STREAK_FOR_X3 = 6;
    constexpr int PIKE_WARNING_FRAMES = 60;         // the "!" shows this long before a pike comes in
    constexpr int LAND_FRAMES = 26;                 // a landed fish flies to the bucket

    struct fish_type
    {
        const char* name;
        int points;
        const bn::sprite_item* item;
        int mouth;            // x of the mouth in the 32 px sprite (fish face right)
        bn::fixed speed;      // swimming speed
        bn::fixed reel_speed; // how fast you can move the hook with this fish on it
        bool needs_bait;      // ignores a bare hook
        int rank;             // a fish only swallows live bait of a lower rank
    };

    enum fish_kind { MINNOW, PERCH, TROUT, SALMON, CARP, KIND_COUNT };

    const fish_type fish_types[KIND_COUNT] = {
        { "Snow Minnow",  10,  &bn::sprite_items::fishing_minnow, 26, 0.7,  1.6,  false, 0 },
        { "Pine Perch",   20,  &bn::sprite_items::fishing_perch,  27, 0.55, 1.45, false, 1 },
        { "Frost Trout",  40,  &bn::sprite_items::fishing_trout,  29, 0.75, 1.3,  false, 2 },
        { "Berry Salmon", 80,  &bn::sprite_items::fishing_salmon, 30, 0.9,  1.15, true,  3 },
        { "Golden Carp",  250, &bn::sprite_items::fishing_carp,   29, 0.6,  0.95, true,  4 },
    };

    // Depth lanes (fish centre y, Butano coordinates), shallow to deep.
    constexpr int lane_count = 5;
    constexpr int lane_y[lane_count] = { 2, 15, 28, 40, 51 };
    constexpr int fish_slots = 9;
    constexpr int slot_lane[fish_slots] = { 0, 0, 1, 1, 2, 2, 3, 3, 4 };

    // Which kinds turn up in each lane (weights): minnow, perch, trout, salmon, carp.
    constexpr int lane_weights[lane_count][KIND_COUNT] = {
        { 85, 15,  0,  0,  0 },
        { 30, 60, 10,  0,  0 },
        {  0, 25, 70,  5,  0 },
        {  0,  0, 25, 75,  0 },
        {  0,  0,  0, 45, 55 },
    };

    // =========================================================================== layout
    constexpr int char_x = -84;          // player sprite centre (sits on the stool)
    constexpr int char_y = -36;
    constexpr int rod_x = -68;           // rod sprite centre
    constexpr int rod_y = -38;
    constexpr int rod_tip_x[3] = { -55, -75, -55 };
    constexpr int rod_tip_y[3] = { -50, -54, -42 };
    enum rod_frame { ROD_OUT = 0, ROD_RAISED, ROD_BENT };

    constexpr int HOOK_X = -55;          // the line hangs straight down the hole here
    constexpr int water_y = -16;         // water surface in the hole (the bobber floats here)
    constexpr int HOOK_TOP = -13;        // highest hook position (a fish brought up here is landed)
    constexpr int HOOK_BOTTOM = 62;      // deepest (on the boot)
    constexpr int SNAG_Y = 57;           // the hook touches the boot below this
    constexpr int BOOT_X = HOOK_X + 2;
    constexpr int BOOT_Y = 63;
    constexpr int ICE_Y = -3;            // drifting ice chunks (centre y)
    constexpr int fish_left = -140;      // fish turn around / leave past these x positions
    constexpr int fish_right = 140;

    constexpr int bucket_x = -106;       // the catch piling up in the bucket (fishing_bucket)
    constexpr int bucket_y = -30;
    constexpr int smoke_x = 65;          // chimney top (fishing_smoke)
    constexpr int smoke_y = -56;

    constexpr int max_line_pieces = 16;
    constexpr int bubble_count = 3;
    constexpr int smoke_count = 4;

    // Depth: only a few z values (Butano has 16 sprite sort layers in total).
    constexpr int Z_FRONT = -2;
    constexpr int Z_TACKLE = 1;
    constexpr int Z_FISH = 2;
    constexpr int Z_BACK = 3;

    // Records: sb_save extra record slots FISHING_RECORD_SLOT .. +2 (Easy, Medium, Hard).
    constexpr int FISHING_RECORD_SLOT = 9;

    // =========================================================================== state
    enum class fish_mode { SWIM, CHASE, HOOKED, FLEE, GONE };

    struct fish
    {
        int kind = MINNOW;
        int lane = 0;
        bn::fixed x, y;
        int dir = 1;              // 1 = swimming right, -1 = left
        fish_mode mode = fish_mode::SWIM;
        int timer = 0;            // respawn countdown / flee time
        int anim = 0;
        bool bait_catch = false;  // swallowed live bait (bonus)
        bn::optional<bn::sprite_ptr> sprite;
    };

    struct pike
    {
        bn::sprite_ptr sprite;
        bn::fixed x;
        int y = 0;
        int dir = 1;
        int rest = 0;             // frames until the next run (the warning shows at the end of the rest)
        bool swimming = false;
    };

    struct ice_chunk
    {
        bn::sprite_ptr sprite;
        bn::fixed x;
        int dir = 1;
    };

    struct puff
    {
        bn::sprite_ptr sprite;
        bn::fixed x, y;
        int age = 0;
    };

    // A fish's mouth position (the sprite is 32 px wide, centred on x).
    int mouth_x(const fish& f)
    {
        return f.x.integer() + f.dir * (fish_types[f.kind].mouth - 16);
    }

    bn::sprite_ptr make_sprite(const bn::sprite_item& item, int x, int y, int priority, int z, int frame = 0)
    {
        bn::sprite_ptr sprite = item.create_sprite(x, y, frame);
        sprite.set_bg_priority(priority);
        sprite.set_z_order(z);
        return sprite;
    }

    class fishing_game
    {
    public:
        fishing_game() :
            _bg(bn::regular_bg_items::fishing_bg.create_bg(8, 48)),
            _player(sb::character_sprite(sb::player_character()).create_sprite(char_x, char_y, sb::FRAME_SIDE_SIT)),
            _rod(make_sprite(bn::sprite_items::fishing_rod, rod_x, rod_y, 2, -1, ROD_OUT)),
            _bobber(make_sprite(bn::sprite_items::fishing_bobber, 0, 0, 2, Z_TACKLE)),
            _hook(make_sprite(bn::sprite_items::fishing_hook, 0, 0, 2, Z_TACKLE)),
            _boot(make_sprite(bn::sprite_items::fishing_boot, BOOT_X, BOOT_Y, 2, Z_BACK)),
            _bucket(make_sprite(bn::sprite_items::fishing_bucket, bucket_x, bucket_y, 2, Z_TACKLE)),
            _splash(make_sprite(bn::sprite_items::fishing_splash, HOOK_X + 1, water_y - 6, 2, Z_FRONT)),
            _sparkle(make_sprite(bn::sprite_items::fishing_sparkle, 0, 0, 1, Z_FRONT)),
            _warning(make_sprite(bn::sprite_items::fishing_bang, 0, 0, 1, Z_FRONT)),
            _flyer(make_sprite(bn::sprite_items::fishing_minnow, 0, 0, 1, Z_FRONT))
        {
            _bg.set_priority(3);
            _player.set_bg_priority(2);
            _splash.set_visible(false);
            _sparkle.set_visible(false);
            _warning.set_visible(false);
            _flyer.set_visible(false);
            _boot.set_horizontal_flip(true);

            for(int i = 0; i < max_line_pieces; ++i)
            {
                _line.push_back(make_sprite(bn::sprite_items::fishing_line, 0, 0, 2, 2));
            }

            for(int i = 0; i < bubble_count; ++i)
            {
                _bubbles.push_back(make_sprite(bn::sprite_items::fishing_bubble, 0, 0, 2, Z_BACK));
                _bubble_x[i] = sb::rand_range(-110, 110);
                _bubble_y[i] = sb::rand_range(-6, 70);
            }

            for(int i = 0; i < smoke_count; ++i)
            {
                bn::sprite_ptr sprite = make_sprite(bn::sprite_items::fishing_smoke, smoke_x, smoke_y, 3, Z_BACK);
                sprite.set_visible(false);
                _smoke.push_back({ sprite, smoke_x, smoke_y, -i * 30 });
            }

            for(int i = 0; i < fish_slots; ++i)
            {
                _fish.emplace_back();
            }

            reset(0);
        }

        // Sets up a new round.
        void reset(int level)
        {
            _level = level;
            _score = 0;
            _caught = 0;
            _streak = 0;
            _best_streak = 0;
            _lost = 0;
            _bait_catches = 0;

            for(int& n : _caught_kind)
            {
                n = 0;
            }

            _frames_left = def().seconds * 60;
            _hook_y = 10;
            _hooked = -1;
            _snag = 0;
            _land = 0;
            _land_points = 0;
            _t = 0;
            _shown_seconds = -1;
            _shown_score = -1;
            _shown_mult = -1;
            _hint_timer = 0;
            _popup_timer = 0;
            _bait_tip_shown = false;
            _hint_label.clear();
            _popup.clear();
            _rod_frame = ROD_OUT;
            _bucket.set_tiles(bn::sprite_items::fishing_bucket.tiles_item(), 0);
            _flyer.set_visible(false);
            _splash.set_visible(false);
            _sparkle.set_visible(false);
            _warning.set_visible(false);

            for(int i = 0; i < fish_slots; ++i)
            {
                spawn_fish(i, true);
            }

            _pikes.clear();

            for(int i = 0; i < def().pikes; ++i)
            {
                bn::sprite_ptr sprite = make_sprite(bn::sprite_items::fishing_pike, 200, 0, 2, Z_FISH);
                sprite.set_visible(false);
                _pikes.push_back({ sprite, 200, 0, 1, def().pike_rest_min + i * 150, false });
            }

            _ice.clear();

            for(int i = 0; i < def().ice_chunks; ++i)
            {
                bn::sprite_ptr sprite = make_sprite(bn::sprite_items::fishing_ice, 0, ICE_Y, 2, Z_FISH, i % 2);
                int dir = i % 2 ? -1 : 1;
                _ice.push_back({ sprite, -120 + i * (240 / def().ice_chunks) + 30, dir });
            }

            draw();     // (the HUD appears on the first frame of play, not over the intro and menus)
        }

        void set_hud_visible(bool visible)
        {
            _score_label.set_visible(visible);
            _time_label.set_visible(visible);
            _mult_label.set_visible(visible);
            _hint_label.set_visible(visible);
            _popup.set_visible(visible);
        }

        void hide_hud()
        {
            _score_label.clear();
            _time_label.clear();
            _mult_label.clear();
            _hint_label.clear();
            _popup.clear();
            _shown_seconds = -1;
            _shown_score = -1;
            _shown_mult = -1;
        }

        // One frame. Returns false when time is up.
        bool step()
        {
            ++_t;

            if(_frames_left > 0)
            {
                --_frames_left;

                int seconds = (_frames_left + 59) / 60;

                if(_frames_left % 60 == 0 && seconds > 0 && seconds <= countdown_beep_from)
                {
                    sb::sfx(bn::sound_items::sfx_countdown);
                }
            }

            move_hook();
            update_fish();
            update_pikes();
            update_ice();
            check_hazards();
            update_landing();
            update_scenery();
            draw();
            update_hud();
            return _frames_left > 0 || _land > 0;
        }

        int score() const
        {
            return _score;
        }

        int level() const
        {
            return _level;
        }

        int caught() const
        {
            return _caught;
        }

        int caught_kind(int kind) const
        {
            return _caught_kind[kind];
        }

        int best_streak() const
        {
            return _best_streak;
        }

        int bait_catches() const
        {
            return _bait_catches;
        }

        int lost() const
        {
            return _lost;
        }

    private:
        bn::regular_bg_ptr _bg;
        bn::sprite_ptr _player, _rod, _bobber, _hook, _boot, _bucket, _splash, _sparkle, _warning, _flyer;
        bn::vector<bn::sprite_ptr, max_line_pieces> _line;
        bn::vector<bn::sprite_ptr, bubble_count> _bubbles;
        bn::vector<puff, smoke_count> _smoke;
        bn::vector<fish, fish_slots> _fish;
        bn::vector<pike, 2> _pikes;
        bn::vector<ice_chunk, 4> _ice;
        sb::label _score_label, _time_label, _mult_label, _hint_label, _popup;

        int _level = 0;
        int _score = 0;
        int _caught = 0;
        int _caught_kind[KIND_COUNT] = {};
        int _streak = 0;
        int _best_streak = 0;
        int _lost = 0;
        int _bait_catches = 0;
        int _frames_left = 0;
        bn::fixed _hook_y;
        int _hooked = -1;         // index of the fish on the hook
        int _snag = 0;
        int _land = 0;            // frames left of a landed fish flying to the bucket
        int _land_points = 0;
        int _land_kind = 0;
        int _t = 0;
        int _rod_frame = ROD_OUT;
        int _bubble_x[bubble_count] = {};
        int _bubble_y[bubble_count] = {};
        int _shown_seconds = -1;
        int _shown_score = -1;
        int _shown_mult = -1;
        int _hint_timer = 0;
        int _popup_timer = 0;
        bool _bait_tip_shown = false;

        const level_def& def() const
        {
            return LEVELS[_level];
        }

        int multiplier() const
        {
            return _streak >= STREAK_FOR_X3 ? 3 : _streak >= STREAK_FOR_X2 ? 2 : 1;
        }

        // ---------------------------------------------------------------------- input
#ifdef FISHING_AUTOPILOT
        // Test builds only (make USERFLAGS=-DFISHING_AUTOPILOT): goes for the nearest fish it can catch,
        // uses small fish as bait when a bigger fish is around, and waits for ice before landing.
        int autopilot_direction() const
        {
            int hook = _hook_y.integer();

            if(_hooked >= 0)
            {
                const fish& bait = _fish[_hooked];

                // A bigger fish chasing (or close to) our bait: hold still / go to its depth.
                for(const fish& f : _fish)
                {
                    if(f.mode == fish_mode::CHASE)
                    {
                        return 0;
                    }
                }

                if(fish_types[bait.kind].rank <= 1)
                {
                    for(const fish& f : _fish)
                    {
                        if(f.mode == fish_mode::SWIM && fish_types[f.kind].rank > fish_types[bait.kind].rank &&
                           bn::abs(f.x.integer() - HOOK_X) < 110 && (HOOK_X - f.x.integer()) * f.dir > 0)
                        {
                            int target = lane_y[f.lane];
                            return target > hook + 2 ? 1 : target < hook - 2 ? -1 : 0;
                        }
                    }
                }

                // Landing: wait below the ice if a chunk is about to cross the hole.
                if(hook < 14)
                {
                    for(const ice_chunk& c : _ice)
                    {
                        int dx = (HOOK_X - c.x.integer()) * c.dir;

                        if(dx > -18 && dx < 40)
                        {
                            return hook < 10 ? 1 : 0;
                        }
                    }
                }

                return -1;
            }

            // Bare hook: the nearest catchable fish swimming towards the hole.
            int best_target = 10;
            int best_dist = 1000;

            for(const fish& f : _fish)
            {
                if(f.mode != fish_mode::SWIM || fish_types[f.kind].needs_bait)
                {
                    continue;
                }

                int dx = (HOOK_X - mouth_x(f)) * f.dir;

                if(dx > 0 && dx < best_dist)
                {
                    best_dist = dx;
                    best_target = lane_y[f.lane];
                }
            }

            return best_target > hook + 1 ? 1 : best_target < hook - 1 ? -1 : 0;
        }
#endif

        int hook_direction() const
        {
#ifdef FISHING_AUTOPILOT
            return autopilot_direction();
#else
            if(bn::keypad::up_held() || bn::keypad::a_held())
            {
                return -1;
            }

            return bn::keypad::down_held() ? 1 : 0;
#endif
        }

        // ---------------------------------------------------------------------- the hook
        void move_hook()
        {
            if(_snag > 0)
            {
                --_snag;

                if(_snag == 0)
                {
                    _hook_y = SNAG_Y - 6;
                    hint("Free again!", sb::text_color::WHITE);
                }

                _rod_frame = (_t / 4) % 2 ? ROD_BENT : ROD_OUT;
                return;
            }

            if(_frames_left <= 0)
            {
                return;
            }

            bn::fixed speed = _hooked >= 0 ? fish_types[_fish[_hooked].kind].reel_speed : HOOK_SPEED;
            _hook_y = bn::clamp(_hook_y + speed * hook_direction(), bn::fixed(HOOK_TOP), bn::fixed(HOOK_BOTTOM));
            _rod_frame = _hooked >= 0 ? ROD_BENT : ROD_OUT;

            // A fish brought up to the hole is landed.
            if(_hooked >= 0 && _hook_y <= HOOK_TOP)
            {
                land_fish();
            }

            // Too deep: snagged on the old boot.
            if(_hook_y >= SNAG_Y)
            {
                _snag = SNAG_FRAMES;
                _hook_y = SNAG_Y + 2;
                sb::sfx(bn::sound_items::sfx_bump);
                show_popup(HOOK_X + 30, SNAG_Y - 14, "Snag!", sb::text_color::RED);
                hint("Snagged on the old boot!", sb::text_color::RED);

                if(_hooked >= 0)
                {
                    lose_fish(true);
                }
            }
        }

        // ---------------------------------------------------------------------- fish
        int pick_kind(int lane)
        {
            int total = 0;

            for(int k = 0; k < KIND_COUNT; ++k)
            {
                total += lane_weights[lane][k];
            }

            int roll = sb::rand_range(0, total - 1);

            for(int k = 0; k < KIND_COUNT; ++k)
            {
                roll -= lane_weights[lane][k];

                if(roll < 0)
                {
                    return k;
                }
            }

            return MINNOW;
        }

        void spawn_fish(int slot, bool anywhere)
        {
            fish& f = _fish[slot];
            f.lane = slot_lane[slot];
            f.kind = pick_kind(f.lane);
            f.dir = sb::rand_range(0, 1) ? 1 : -1;
            f.x = anywhere ? sb::rand_range(-100, 120) : (f.dir > 0 ? fish_left + 4 : fish_right - 4);
            f.y = lane_y[f.lane];
            f.mode = fish_mode::SWIM;
            f.timer = 0;
            f.anim = sb::rand_range(0, 63);
            f.bait_catch = false;

            if(! f.sprite)
            {
                f.sprite = make_sprite(*fish_types[f.kind].item, 0, 0, 2, Z_FISH);
            }
            else
            {
                f.sprite->set_item(*fish_types[f.kind].item);
            }

            f.sprite->set_visible(true);
        }

        bool on_hook_is_bait_for(const fish& f) const
        {
            return _hooked >= 0 && fish_types[f.kind].rank > fish_types[_fish[_hooked].kind].rank;
        }

        void update_fish()
        {
            int hook = _hook_y.integer();
            int chaser = -1;
            int chaser_dist = 1000;

            // Only the nearest bigger fish chases live bait.
            if(_hooked >= 0 && _snag == 0)
            {
                for(int i = 0; i < fish_slots; ++i)
                {
                    fish& f = _fish[i];

                    if((f.mode == fish_mode::SWIM || f.mode == fish_mode::CHASE) && on_hook_is_bait_for(f))
                    {
                        int dx = bn::abs(f.x.integer() - HOOK_X);
                        int dy = bn::abs(lane_y[f.lane] - hook);

                        if(dx < CHASE_RANGE_X && dy < CHASE_RANGE_Y && dx < chaser_dist)
                        {
                            chaser = i;
                            chaser_dist = dx;
                        }
                    }
                }
            }

            for(int i = 0; i < fish_slots; ++i)
            {
                fish& f = _fish[i];
                const fish_type& type = fish_types[f.kind];
                ++f.anim;

                switch(f.mode)
                {
                case fish_mode::GONE:
                    if(--f.timer <= 0)
                    {
                        spawn_fish(i, false);
                    }
                    break;

                case fish_mode::HOOKED:
                    // Hangs from the hook, wriggling.
                    if(f.anim % 10 == 0)
                    {
                        f.dir = -f.dir;
                    }

                    f.x = HOOK_X - f.dir * (type.mouth - 16) + ((f.anim / 3) % 2);
                    f.y = _hook_y + 1;
                    break;

                case fish_mode::FLEE:
                    f.x += f.dir * type.speed * 3;
                    f.y += f.y < lane_y[f.lane] ? 1 : f.y > lane_y[f.lane] ? -1 : 0;

                    if(f.x < fish_left - 10 || f.x > fish_right + 10)
                    {
                        f.mode = fish_mode::GONE;
                        f.timer = sb::rand_range(40, 120);
                        f.sprite->set_visible(false);
                    }
                    break;

                case fish_mode::CHASE:
                    if(i != chaser)
                    {
                        f.mode = fish_mode::SWIM;   // lost interest
                        break;
                    }
                    [[fallthrough]];

                case fish_mode::SWIM:
                default:
                {
                    bn::fixed speed = type.speed * def().fish_speed;

                    if(i == chaser)
                    {
                        // Swim at the bait: turn towards it and go to its depth.
                        if(f.mode != fish_mode::CHASE)
                        {
                            f.mode = fish_mode::CHASE;
                            sb::sfx(bn::sound_items::sfx_bite);
                        }

                        f.dir = HOOK_X > f.x.integer() ? 1 : -1;
                        f.x += f.dir * speed * CHASE_SPEED_FACTOR;
                        f.y += f.y < _hook_y ? bn::fixed(0.75) : f.y > _hook_y + 1 ? bn::fixed(-0.75) : bn::fixed(0);
                    }
                    else
                    {
                        f.x += f.dir * speed;

                        // Small fish near the hook's depth nose up or down to it as they pass.
                        bn::fixed target = lane_y[f.lane];
                        int ahead = (HOOK_X - mouth_x(f)) * f.dir;

                        if(_hooked < 0 && _snag == 0 && ! type.needs_bait && ahead > -2 && ahead < 40 &&
                           bn::abs(lane_y[f.lane] - hook) <= LURE_RANGE_Y)
                        {
                            target = _hook_y;
                        }

                        target += ((f.anim / 32) % 4 == 1) ? 1 : ((f.anim / 32) % 4 == 3) ? -1 : 0;
                        f.y += f.y < target ? bn::fixed(0.5) : f.y > target ? bn::fixed(-0.5) : bn::fixed(0);
                    }

                    if(f.x < fish_left || f.x > fish_right)
                    {
                        // Swam off: sometimes turn back, otherwise a new fish comes later.
                        if(sb::rand_range(0, 2) == 0)
                        {
                            f.dir = -f.dir;
                            f.x = f.x < 0 ? bn::fixed(fish_left) : bn::fixed(fish_right);
                        }
                        else
                        {
                            f.mode = fish_mode::GONE;
                            f.timer = sb::rand_range(30, 100);
                            f.sprite->set_visible(false);
                            break;
                        }
                    }

                    try_bite(i);
                    break;
                }
                }
            }
        }

        void try_bite(int i)
        {
            fish& f = _fish[i];
            const fish_type& type = fish_types[f.kind];
            int mx = mouth_x(f);
            int my = f.y.integer();
            int hook = _hook_y.integer();

            if(_snag > 0 || _frames_left <= 0 || _land > 0)
            {
                return;
            }

            if(_hooked < 0)
            {
                // A bare hook: only fish that don't need bait bite.
                if(! type.needs_bait && bn::abs(mx - HOOK_X) <= BITE_REACH_X && bn::abs(my - hook) <= BITE_REACH_Y)
                {
                    hook_fish(i, false);
                }

                return;
            }

            // Live bait: a bigger fish that reaches the hooked fish swallows it.
            fish& bait = _fish[_hooked];

            if(type.rank > fish_types[bait.kind].rank && bn::abs(mx - bait.x.integer()) <= 12 &&
               bn::abs(my - bait.y.integer()) <= 7)
            {
                bait.mode = fish_mode::GONE;
                bait.timer = sb::rand_range(60, 140);
                bait.sprite->set_visible(false);
                show_popup(HOOK_X + 26, hook - 12, "Gulp!", sb::text_color::YELLOW);
                hook_fish(i, true);
            }
        }

        void hook_fish(int i, bool bait_catch)
        {
            fish& f = _fish[i];
            f.mode = fish_mode::HOOKED;
            f.bait_catch = bait_catch;
            _hooked = i;
            sb::sfx(bn::sound_items::sfx_bite);

            if(! bait_catch && fish_types[f.kind].rank <= 1 && ! _bait_tip_shown)
            {
                _bait_tip_shown = true;
                hint("Bring it up, or lower it as bait!", sb::text_color::WHITE);
            }
            else if(bait_catch)
            {
                hint("A big one took the bait!", sb::text_color::YELLOW);
            }
        }

        // The fish comes off the hook. eaten = gone for good (the pike), otherwise it swims away.
        void lose_fish(bool knocked)
        {
            if(_hooked < 0)
            {
                return;
            }

            fish& f = _fish[_hooked];

            if(knocked)
            {
                f.mode = fish_mode::FLEE;
                f.dir = sb::rand_range(0, 1) ? 1 : -1;
            }
            else
            {
                f.mode = fish_mode::GONE;
                f.timer = sb::rand_range(60, 140);
                f.sprite->set_visible(false);
            }

            _hooked = -1;
            _streak = 0;
            ++_lost;
        }

        void land_fish()
        {
            fish& f = _fish[_hooked];
            const fish_type& type = fish_types[f.kind];
            int points = type.points;

            if(f.bait_catch)
            {
                points += points * BAIT_BONUS_PERCENT / 100;
                ++_bait_catches;
            }

            ++_streak;
            _best_streak = bn::max(_best_streak, _streak);
            points *= multiplier();
            _score += points;
            ++_caught;
            ++_caught_kind[f.kind];
            _land = LAND_FRAMES;
            _land_points = points;
            _land_kind = f.kind;

            // The fish flies out of the hole into the bucket (the slot gets a new fish later).
            f.mode = fish_mode::GONE;
            f.timer = sb::rand_range(60, 140);
            f.sprite->set_visible(false);
            _hooked = -1;
            _flyer.set_item(*type.item);
            _flyer.set_visible(true);
            _splash.set_visible(true);
            _splash.set_tiles(bn::sprite_items::fishing_splash.tiles_item(), 0);
            sb::sfx(f.kind == CARP ? bn::sound_items::sfx_record : bn::sound_items::sfx_catch);

            bn::string<24> text("+");
            text.append(sb::to_text(points));
            show_popup(HOOK_X + 4, water_y - 22, text, f.kind == CARP ? sb::text_color::YELLOW : sb::text_color::WHITE);

            if(f.bait_catch)
            {
                hint("Bait bonus! +50%", sb::text_color::YELLOW);
            }
            else if(_streak == STREAK_FOR_X2 || _streak == STREAK_FOR_X3)
            {
                hint(_streak == STREAK_FOR_X3 ? "Hot streak! x3" : "Streak! x2", sb::text_color::YELLOW);
            }
        }

        void update_landing()
        {
            if(_land <= 0)
            {
                return;
            }

            --_land;
            int t = LAND_FRAMES - _land;    // 0 .. LAND_FRAMES
            int x0 = HOOK_X, y0 = water_y - 6;
            int x = x0 + (bucket_x - x0) * t / LAND_FRAMES;
            int arc = 4 * 30 * t * (LAND_FRAMES - t) / (LAND_FRAMES * LAND_FRAMES);
            int y = y0 + (bucket_y + 2 - y0) * t / LAND_FRAMES - arc;
            _flyer.set_position(x, y);
            _flyer.set_horizontal_flip(true);
            _flyer.set_tiles(fish_types[_land_kind].item->tiles_item(), (t / 4) % 2);
            _splash.set_tiles(bn::sprite_items::fishing_splash.tiles_item(), bn::min(t / 5, 2));
            _splash.set_visible(t < 15);
            _rod_frame = t < LAND_FRAMES / 2 ? ROD_RAISED : ROD_OUT;

            if(_land_kind == CARP)
            {
                _sparkle.set_visible(true);
                _sparkle.set_position(x + ((t * 5) % 11) - 5, y - 6 + ((t * 3) % 7));
                _sparkle.set_tiles(bn::sprite_items::fishing_sparkle.tiles_item(), (t / 3) % 2);
            }

            if(_land == 0)
            {
                _flyer.set_visible(false);
                _sparkle.set_visible(false);
                _bucket.set_tiles(bn::sprite_items::fishing_bucket.tiles_item(), bn::min(_caught, 5));
                sb::sfx(bn::sound_items::sfx_point);
            }
        }

        // ---------------------------------------------------------------------- hazards
        void update_pikes()
        {
            for(pike& p : _pikes)
            {
                if(! p.swimming)
                {
                    if(_frames_left <= 0)
                    {
                        continue;
                    }

                    --p.rest;

                    if(p.rest == PIKE_WARNING_FRAMES)
                    {
                        // Choose where it comes from and show the warning there.
                        p.dir = sb::rand_range(0, 1) ? 1 : -1;
                        p.y = lane_y[sb::rand_range(1, 3)] + sb::rand_range(-3, 3);
                        _warning.set_position(p.dir > 0 ? -112 : 112, p.y - 4);
                        _warning.set_visible(true);
                        sb::sfx(bn::sound_items::sfx_countdown);
                    }
                    else if(p.rest < PIKE_WARNING_FRAMES && p.rest > 0)
                    {
                        _warning.set_tiles(bn::sprite_items::fishing_bang.tiles_item(), (p.rest / 6) % 2);
                    }
                    else if(p.rest <= 0)
                    {
                        p.swimming = true;
                        p.x = p.dir > 0 ? -150 : 150;
                        p.sprite.set_visible(true);
                        p.sprite.set_horizontal_flip(p.dir < 0);
                        _warning.set_visible(false);
                    }

                    continue;
                }

                p.x += p.dir * def().pike_speed;
                p.sprite.set_position(p.x, p.y);
                p.sprite.set_tiles(bn::sprite_items::fishing_pike.tiles_item(), (_t / 6) % 2);

                if(p.x < -160 || p.x > 160)
                {
                    p.swimming = false;
                    p.sprite.set_visible(false);
                    p.rest = sb::rand_range(def().pike_rest_min, def().pike_rest_max) + PIKE_WARNING_FRAMES;
                }
            }
        }

        void update_ice()
        {
            for(ice_chunk& c : _ice)
            {
                c.x += c.dir * def().ice_speed;

                if(c.x < -150)
                {
                    c.x = 150;
                }
                else if(c.x > 150)
                {
                    c.x = -150;
                }

                c.sprite.set_position(c.x, ICE_Y + ((_t / 40) % 2));
            }
        }

        // Ice and pikes knock the fish off your hook (a bare hook passes them safely).
        void check_hazards()
        {
            if(_hooked < 0)
            {
                return;
            }

            const fish& f = _fish[_hooked];
            int fx = f.x.integer();
            int fy = f.y.integer();

            for(const ice_chunk& c : _ice)
            {
                if(bn::abs(c.x.integer() - fx) < 20 && bn::abs(ICE_Y - fy) < 8)
                {
                    sb::sfx(bn::sound_items::sfx_hurt);
                    show_popup(fx + 20, fy - 10, "Bonk!", sb::text_color::RED);
                    hint("The ice knocked it off!", sb::text_color::RED);
                    lose_fish(true);
                    return;
                }
            }

            for(const pike& p : _pikes)
            {
                if(p.swimming && bn::abs(p.x.integer() - fx) < 26 && bn::abs(p.y - fy) < 8)
                {
                    sb::sfx(bn::sound_items::sfx_crash);
                    show_popup(fx + 20, fy - 10, "Chomp!", sb::text_color::RED);
                    hint("The pike stole your fish!", sb::text_color::RED);
                    lose_fish(false);
                    return;
                }
            }
        }

        // ---------------------------------------------------------------------- scenery
        void update_scenery()
        {
            // Bubbles rise and wobble.
            for(int i = 0; i < bubble_count; ++i)
            {
                if(_t % 3 == 0)
                {
                    --_bubble_y[i];
                }

                if(_bubble_y[i] < -6)
                {
                    _bubble_y[i] = 70;
                    _bubble_x[i] = sb::rand_range(-110, 110);
                }

                _bubbles[i].set_position(_bubble_x[i] + ((_t / 20 + i) % 3) - 1, _bubble_y[i]);
            }

            // Chimney smoke: a puff every 30 frames, rising and drifting with the wind.
            for(puff& p : _smoke)
            {
                ++p.age;

                if(p.age == 0)
                {
                    p.x = smoke_x;
                    p.y = smoke_y;
                }

                if(p.age < 0)
                {
                    continue;
                }

                p.x += bn::fixed(0.2);
                p.y -= bn::fixed(0.3);
                p.sprite.set_visible(p.age < 110);
                p.sprite.set_position(p.x, p.y);
                p.sprite.set_tiles(bn::sprite_items::fishing_smoke.tiles_item(), bn::min(p.age / 36, 2));

                if(p.age >= 120)
                {
                    p.age = 0;
                    p.x = smoke_x;
                    p.y = smoke_y;
                }
            }

            if(_popup_timer > 0 && --_popup_timer == 0)
            {
                _popup.clear();
            }

            if(_hint_timer > 0 && --_hint_timer == 0)
            {
                _hint_label.clear();
            }
        }

        void show_popup(int x, int y, const bn::string_view& text, sb::text_color color)
        {
            _popup.set(x, y, text, color, sb::align::CENTER);
            _popup_timer = 45;
        }

        void hint(const bn::string_view& text, sb::text_color color)
        {
            _hint_label.set(0, 72, text, color, sb::align::CENTER);
            _hint_timer = 100;
        }

        // ---------------------------------------------------------------------- drawing
        void draw()
        {
            int hook = _hook_y.integer();
            int top = rod_tip_y[_rod_frame] + 1;
            int lx = rod_tip_x[_rod_frame];
            bool show_line = _land <= 0 || _land < LAND_FRAMES / 2;

            _rod.set_tiles(bn::sprite_items::fishing_rod.tiles_item(), _rod_frame);

            // The line: 8 px pieces from the rod tip down to the hook.
            for(int i = 0; i < max_line_pieces; ++i)
            {
                int y = top + i * 8;
                bool visible = show_line && y < hook;

                if(visible)
                {
                    _line[i].set_position(lx + 4, bn::min(y, hook - 8) + 4);
                }

                _line[i].set_visible(visible);
            }

            int shake = _snag > 0 ? ((_t / 2) % 3) - 1 : 0;
            _hook.set_visible(show_line);
            _hook.set_position(HOOK_X + 1 + shake, hook);

            // The bobber floats in the hole once the hook is under the water.
            int bob_y = bn::max(bn::min(hook - 10, water_y), top + 3);

            if(bob_y == water_y)
            {
                bob_y += _hooked >= 0 || _snag > 0 ? 2 + ((_t / 4) % 2) : (_t / 30) % 2;
            }

            _bobber.set_visible(show_line);
            _bobber.set_position(HOOK_X + 1, bob_y);
            _bobber.set_tiles(bn::sprite_items::fishing_bobber.tiles_item(), _hooked >= 0 ? 1 : 0);

            for(fish& f : _fish)
            {
                if(f.mode == fish_mode::GONE)
                {
                    continue;
                }

                const fish_type& type = fish_types[f.kind];
                int speed = f.mode == fish_mode::HOOKED || f.mode == fish_mode::FLEE ? 4 :
                            f.mode == fish_mode::CHASE ? 6 : 10;
                f.sprite->set_position(f.x, f.y);
                f.sprite->set_horizontal_flip(f.dir < 0);
                f.sprite->set_tiles(type.item->tiles_item(), (f.anim / speed) % 2);
                f.sprite->set_z_order(f.mode == fish_mode::HOOKED ? Z_TACKLE : Z_FISH);
            }

            // The golden carp twinkles.
            if(_land <= 0)
            {
                const fish* golden = nullptr;

                for(const fish& f : _fish)
                {
                    if(f.kind == CARP && f.mode != fish_mode::GONE)
                    {
                        golden = &f;
                    }
                }

                _sparkle.set_visible(golden && (_t / 20) % 3 == 0);

                if(golden)
                {
                    _sparkle.set_position(golden->x + ((_t / 20) % 2 ? 6 : -6), golden->y - 5);
                    _sparkle.set_tiles(bn::sprite_items::fishing_sparkle.tiles_item(), (_t / 10) % 2);
                }
            }
        }

        void update_hud()
        {
            if(_score != _shown_score)
            {
                _shown_score = _score;
                bn::string<24> text("Score ");
                text.append(sb::to_text(_score));
                _score_label.set(-114, -72, text, sb::text_color::WHITE);
            }

            int seconds = (_frames_left + 59) / 60;

            if(seconds != _shown_seconds)
            {
                _shown_seconds = seconds;
                bn::string<24> text("Time ");
                text.append(sb::to_text(seconds));
                _time_label.set(10, -72, text, seconds <= yellow_time_from ? sb::text_color::YELLOW :
                                                                        sb::text_color::WHITE, sb::align::CENTER);
            }

            int mult = multiplier();

            if(mult != _shown_mult)
            {
                _shown_mult = mult;

                if(mult == 1)
                {
                    _mult_label.clear();
                }
                else
                {
                    _mult_label.set(114, -72, mult == 3 ? "x3" : "x2", sb::text_color::YELLOW, sb::align::RIGHT);
                }
            }
        }
    };

    int g_level_choice = 0;

    bool choose_level()
    {
        sb::label title;
        title.set(0, -56, "Which pond?", sb::text_color::WHITE, sb::align::CENTER);
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

    // The result box: score, the catch, and the best score at every level. Returns true to fish again.
    bool show_results(const fishing_game& game)
    {
        bool record = sb::submit_extra(FISHING_RECORD_SLOT + game.level(), game.score(), sb::player_character());
        sb::play_jingle(record ? bn::music_items::jingle_record : bn::music_items::jingle_over);

        // Everything fits in the box (rows 0..13) so the menu below (rows 14..19) doesn't cover it.
        sb::panel(3, 0, 24, 14);
        sb::label title, result, kinds1, kinds2, extra, heading, lines[LEVEL_COUNT], record_label;
        title.set(0, -71, "Time's up!", sb::text_color::BLUE, sb::align::CENTER);

        bn::string<40> text(LEVELS[game.level()].name);
        text.append(": ");
        text.append(sb::to_text(game.score()));
        text.append(" points, ");
        text.append(sb::to_text(game.caught()));
        text.append(" fish");
        result.set(0, -60, text, sb::text_color::DARK, sb::align::CENTER);

        // The catch by kind, in two lines.
        constexpr const char* short_names[KIND_COUNT] = { "Minnow", "Perch", "Trout", "Salmon", "Golden" };
        bn::string<40> line1, line2;

        for(int k = 0; k < KIND_COUNT; ++k)
        {
            bn::string<40>& line = k < 3 ? line1 : line2;

            if(! line.empty())
            {
                line.append("  ");
            }

            line.append(short_names[k]);
            line.append(" ");
            line.append(sb::to_text(game.caught_kind(k)));
        }

        kinds1.set(0, -50, line1, sb::text_color::DARK, sb::align::CENTER);
        kinds2.set(0, -40, line2, sb::text_color::DARK, sb::align::CENTER);

        bn::string<40> extras("Bait catches ");
        extras.append(sb::to_text(game.bait_catches()));
        extras.append("  Best streak ");
        extras.append(sb::to_text(game.best_streak()));
        extra.set(0, -30, extras, sb::text_color::DARK, sb::align::CENTER);
        heading.set(0, -18, "{ Best scores {", sb::text_color::RED, sb::align::CENTER);

        for(int l = 0; l < LEVEL_COUNT; ++l)
        {
            bn::string<40> best(LEVELS[l].name);
            best.append(":  ");
            int value = sb::extra_score(FISHING_RECORD_SLOT + l);

            if(value > 0)
            {
                best.append(sb::to_text(value));
                best.append("  ");
                best.append(sb::extra_name(FISHING_RECORD_SLOT + l));
            }
            else
            {
                best.append("---");
            }

            lines[l].set(-72, -7 + l * 10, best, l == game.level() ? sb::text_color::BLUE : sb::text_color::DARK);
        }

        for(int t = 0; t < 50; ++t)
        {
            if(record && t % 20 == 0)
            {
                record_label.set(0, 23, "{ NEW RECORD! {", sb::text_color::YELLOW, sb::align::CENTER);
            }

            sb::update();
        }

        if(record)
        {
            sb::sfx(bn::sound_items::sfx_record);
        }

        constexpr bn::string_view items[] = { "Fish again", "Back to map" };
        int choice = sb::menu(8, 14, items, 0, true);
        sb::panel_clear();
        return choice == 0;
    }

    bn::string<16> record_value_text(int value)
    {
        bn::string<16> text = sb::to_text(value);
        return text;
    }
}

// The best record shown on the map, the intro and the high score list (sb_games.h: game_info::best_text):
// the hardest level with a score, e.g. "Hard 420 by Adam".
bn::string<40> sb::fishing_best_text(bool compact)
{
    static constexpr const char* names[] = { "Easy", "Medium", "Hard" };
    return level_record_text(FISHING_RECORD_SLOT, LEVEL_COUNT, names, record_value_text, compact);
}

void sb::fishing_run()
{
#ifdef SB_FISHING_CLASSIC
    fishing_classic_run();
    return;
#endif

    // Shown on the start screen (scroll with Up/Down) and in the pause menu. '#' = heading.
    static constexpr bn::string_view howto[] = {
        "#Catch fish",
        "Up / Down moves your hook",
        "(hold A to reel up fast).",
        "A fish that swims into",
        "the hook is caught. Bring",
        "it up to the hole to put",
        "it in your bucket.",
        "",
        "#Deeper = bigger",
        "Minnow 10, Perch 20,",
        "Trout 40, Salmon 80, and",
        "the Golden Carp at the",
        "very bottom: 250!",
        "",
        "#Live bait",
        "Salmon and the Golden Carp",
        "ignore a bare hook. Hook a",
        "small fish, then lower it:",
        "a bigger fish swallows it",
        "and you get it, +50%!",
        "",
        "#Watch out",
        "Ice chunks under the ice",
        "knock your fish off.",
        "A ! shows where the pike",
        "will swim: it steals your",
        "fish. Don't go too deep:",
        "the old boot snags you.",
        "",
        "#Streaks",
        "3 fish in a row: x2.",
        "6 in a row: x3!",
    };

    {
        fishing_game game;
        play_music(bn::music_items::fishing_song);

        if(bn::bg_palettes::fade_intensity() > 0)
        {
            fade_in();
        }

        if(game_intro(GAME_FISHING, howto))
        {
            while(choose_level())
            {
                game.reset(g_level_choice);
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

                    bool playing = game.step();
                    update();

                    if(! playing)
                    {
                        break;
                    }
                }

                if(quit)
                {
                    break;
                }

                BN_LOG("Fishing level ", game.level(), ": score ", game.score(), ", fish ", game.caught(),
                       ", bait catches ", game.bait_catches(), ", best streak ", game.best_streak(), ", lost ",
                       game.lost());
                game.hide_hud();
                wait_frames(30);

                if(! show_results(game))
                {
                    break;
                }

                play_music(bn::music_items::fishing_song);
            }
        }

        game.hide_hud();
        fade_out();
    }

    panel_clear();
}
