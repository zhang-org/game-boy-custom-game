// Gift Catch: Santa's sleigh flies over the toy workshop and drops presents. Walk left and right
// with the basket on your head to catch them, but watch out for the grumpy lumps of coal!
//
// Art: tools/art/make_catch.py -> graphics/catch/. Music: catch_song.
#include "bn_bg_palettes.h"
#include "bn_color.h"
#include "bn_fixed.h"
#include "bn_keypad.h"
#include "bn_math.h"
#include "bn_regular_bg_ptr.h"
#include "bn_sprite_ptr.h"
#include "bn_string.h"
#include "bn_string_view.h"
#include "bn_vector.h"

#include "bn_music_items.h"
#include "bn_regular_bg_items_catch_bg.h"
#include "bn_sound_items.h"
#include "bn_sprite_items_catch_basket.h"
#include "bn_sprite_items_catch_items.h"
#include "bn_sprite_items_catch_popup.h"
#include "bn_sprite_items_catch_sleigh.h"

#include "sb_audio.h"
#include "sb_characters.h"
#include "sb_core.h"
#include "sb_games.h"
#include "sb_snow.h"
#include "sb_text.h"
#include "sb_ui.h"

namespace
{
    // ============================================================================ tuning
    // Player
    constexpr bn::fixed PLAYER_SPEED = 1.75;          // walking speed (pixels per frame)
    constexpr bn::fixed PLAYER_DASH_SPEED = 2.75;     // speed while holding A or B
    constexpr int PLAYER_Y = 58;                      // centre of the 16x32 character sprite (feet at ~74)
    constexpr int PLAYER_LIMIT_X = 106;               // how far left/right the player can walk
    constexpr int CATCH_HALF_WIDTH = 15;              // an item is caught if it's this close (px) to the basket centre

    // Falling items
    constexpr bn::fixed FALL_SPEED_START = 0.55;      // fall speed at the start (pixels per frame)
    constexpr bn::fixed FALL_SPEED_PER_SECOND = 0.01; // extra fall speed for every second played
    constexpr bn::fixed FALL_SPEED_MAX = 2.1;         // fall speed never goes above this
    constexpr int FALL_SPEED_RANDOM_PERCENT = 20;     // each item falls up to this % faster than the base speed
    constexpr int SPAWN_INTERVAL_START = 96;          // frames between drops at the start
    constexpr int SPAWN_INTERVAL_DROP_PER_MINUTE = 36; // the interval gets this many frames shorter every minute
    constexpr int SPAWN_INTERVAL_MIN = 30;            // fastest drop rate
    constexpr int SPAWN_INTERVAL_RANDOM = 16;         // +/- random frames added to every interval
    constexpr int DROP_SPREAD = 20;                   // items land up to this many px left/right of the sleigh's sack

    // What falls (percent chances, checked in this order)
    constexpr int COAL_FREE_SECONDS = 10;             // no coal at all at the very start
    constexpr int COAL_CHANCE_START = 12;             // % chance an item is coal once coal starts
    constexpr int COAL_CHANCE_PER_MINUTE = 12;        // coal chance grows this many % per minute
    constexpr int COAL_CHANCE_MAX = 35;               // coal chance never goes above this
    constexpr int STAR_CHANCE = 6;                    // % chance of a golden star present
    constexpr int BIG_CHANCE = 22;                    // % chance of a big present (otherwise small)

    // Scoring
    constexpr int POINTS_SMALL = 10;                  // small present
    constexpr int POINTS_BIG = 25;                    // big present
    constexpr int POINTS_STAR = 50;                   // golden star present
    constexpr int COMBO_X2_AT = 10;                   // catches in a row (no coal, no misses) for double points
    constexpr int COMBO_X3_AT = 25;                   // ... and for triple points
    constexpr int HEART_BONUS_EVERY = 500;            // get a lost heart back every this many points

    // Hearts and getting hit
    constexpr int MAX_HEARTS = 3;                     // hearts at the start (game over at 0)
    constexpr int HURT_SAFE_FRAMES = 70;              // after catching coal, more coal can't hurt for this long
    constexpr int SHAKE_FRAMES = 18;                  // screen shake length after catching coal
    constexpr int SHAKE_PIXELS = 2;                   // screen shake strength

    // Santa's sleigh
    constexpr int SLEIGH_Y = -46;                     // height of the sleigh's flight path
    constexpr bn::fixed SLEIGH_SPEED_START = 0.8;     // sleigh speed at the start (pixels per frame)
    constexpr bn::fixed SLEIGH_SPEED_PER_MINUTE = 0.6; // sleigh gets this much faster every minute
    constexpr bn::fixed SLEIGH_SPEED_MAX = 1.6;       // fastest sleigh speed
    constexpr int SLEIGH_TURN_X = 160;                // the sleigh turns around this far off screen
    constexpr int SLEIGH_DROP_LIMIT_X = 108;          // only drops gifts while the sack is this close to the centre
    constexpr int SLEIGH_OFFSCREEN_SPEEDUP = 3;       // the sleigh flies this many times faster while it can't drop gifts

    // Layout (not really tuning, but handy to have together)
    constexpr int GROUND_Y = 66;                      // centre y of an item lying in the snow
    constexpr int LANDED_FRAMES = 40;                 // how long missed items stay in the snow
    constexpr int BASKET_OFFSET_Y = -14;              // basket centre relative to the character centre
    constexpr int BASKET_RIM_OFFSET_Y = -2;           // the basket opening, relative to the basket centre
    constexpr int ITEM_COUNT = 12;                    // sprite pool for falling items
    constexpr int POPUP_COUNT = 5;                    // sprite pool for "+10" popups
    constexpr int POPUP_FRAMES = 40;                  // how long a popup floats

    // ============================================================================ data
    enum class kind
    {
        SMALL, BIG, STAR, COAL
    };

    enum class item_state
    {
        FREE, FALLING, LANDED
    };

    struct item
    {
        bn::sprite_ptr sprite;
        item_state state = item_state::FREE;
        kind type = kind::SMALL;
        int frame = 0;
        int timer = 0;
        bn::fixed x;
        bn::fixed y;
        bn::fixed speed;
    };

    struct popup
    {
        bn::sprite_ptr sprite;
        int timer = 0;
        bn::fixed y;
    };

    // Popup sheet frames (see make_catch.py).
    constexpr int POPUP_HEART_PLUS = 8;
    constexpr int POPUP_HEART_MINUS = 9;

    int popup_frame(int points)
    {
        switch(points)
        {
        case 10: return 0;
        case 20: return 1;
        case 25: return 2;
        case 30: return 3;
        case 50: return 4;
        case 75: return 5;
        case 100: return 6;
        default: return 7;
        }
    }

    constexpr int ITEM_FRAME_SMALL = 0;   // 3 colours: 0..2
    constexpr int ITEM_FRAME_BIG = 3;     // 2 colours: 3..4
    constexpr int ITEM_FRAME_STAR = 5;    // 2 sparkle frames: 5..6
    constexpr int ITEM_FRAME_COAL = 7;    // 2 frames: 7..8

    bn::fixed clamp_fixed(bn::fixed v, bn::fixed lo, bn::fixed hi)
    {
        return v < lo ? lo : v > hi ? hi : v;
    }

    class catch_game
    {
    public:
        catch_game() :
            _bg(bn::regular_bg_items::catch_bg.create_bg(0, 0)),
            _sleigh(bn::sprite_items::catch_sleigh.create_sprite(40, SLEIGH_Y)),
            _player(sb::character_sprite(sb::player_character()).create_sprite(0, PLAYER_Y, sb::FRAME_CHEER)),
            _basket(bn::sprite_items::catch_basket.create_sprite(0, PLAYER_Y + BASKET_OFFSET_Y)),
            _snow(14, 2)
        {
            _bg.set_priority(3);
            _sleigh.set_bg_priority(1);
            _sleigh.set_z_order(1);
            _player.set_bg_priority(1);
            _player.set_z_order(0);
            _basket.set_bg_priority(1);
            _basket.set_z_order(-1);

            for(int i = 0; i < ITEM_COUNT; ++i)
            {
                item it { bn::sprite_items::catch_items.create_sprite(0, 0), item_state::FREE, kind::SMALL, 0, 0, 0, 0, 0 };
                it.sprite.set_bg_priority(1);
                it.sprite.set_z_order(2);
                it.sprite.set_visible(false);
                _items.push_back(it);
            }

            for(int i = 0; i < POPUP_COUNT; ++i)
            {
                popup p { bn::sprite_items::catch_popup.create_sprite(0, 0), 0, 0 };
                p.sprite.set_bg_priority(1);
                p.sprite.set_z_order(-2);
                p.sprite.set_visible(false);
                _popups.push_back(p);
            }

            reset();
        }

        void reset()
        {
            for(item& it : _items)
            {
                it.state = item_state::FREE;
                it.sprite.set_visible(false);
            }

            for(popup& p : _popups)
            {
                p.timer = 0;
                p.sprite.set_visible(false);
            }

            _score = 0;
            _hearts = MAX_HEARTS;
            _caught = 0;
            _streak = 0;
            _play_frames = 0;
            _spawn_timer = 60;   // a short breather before the first gift
            _next_heart_score = HEART_BONUS_EVERY;
            _hurt_timer = 0;
            _shake_timer = 0;
            _bounce_timer = 0;
            _player_x = 0;
            _facing_left = false;
            _walk_timer = 0;
            _sleigh_x = -SLEIGH_TURN_X + 20;
            _sleigh_dir = 1;
            _shown_score = -1;
            _shown_hearts = -1;
            _shown_multiplier = -1;
            _bg.set_position(0, 0);
            _player.set_visible(true);
            update_hud();
            place_player(false);
            place_sleigh();
        }

        // Shows the scene in its idle pose (used behind the intro box).
        void show_idle()
        {
            _sleigh_x = 30;
            place_sleigh();
            place_player(false);
        }

        // One frame of play. Returns false when the player is out of hearts.
        bool update()
        {
            if(_play_frames < 60 * 60 * 60)   // stop counting after an hour (keeps the maths small)
            {
                ++_play_frames;
            }

            update_player();
            update_sleigh();
            update_spawning();
            update_items();
            update_popups();
            update_effects();
            update_hud();
            _snow.update();
            return _hearts > 0;
        }

        // A short pause after the last heart is lost, so you can see what happened.
        void game_over_pause()
        {
            _player.set_visible(true);
            _player.set_horizontal_flip(false);
            _player.set_tiles(sb::character_sprite(sb::player_character()).tiles_item(), sb::FRAME_DOWN);

            for(int t = 0; t < 60; ++t)
            {
                update_sleigh();
                update_popups();
                update_effects();
                _snow.update();
                sb::update();
            }

            _bg.set_position(0, 0);
        }

        int score() const
        {
            return _score;
        }

        int caught() const
        {
            return _caught;
        }

        void clear_hud()
        {
            _score_label.clear();
            _combo_label.clear();
            _hearts_label.clear();
            _lost_label.clear();
        }

    private:
        bn::regular_bg_ptr _bg;
        bn::sprite_ptr _sleigh;
        bn::sprite_ptr _player;
        bn::sprite_ptr _basket;
        sb::snowfall _snow;
        bn::vector<item, ITEM_COUNT> _items;
        bn::vector<popup, POPUP_COUNT> _popups;
        sb::label _score_label;
        sb::label _combo_label;
        sb::label _hearts_label;
        sb::label _lost_label;

        int _score = 0;
        int _hearts = MAX_HEARTS;
        int _caught = 0;
        int _streak = 0;
        int _play_frames = 0;
        int _spawn_timer = 0;
        int _next_heart_score = HEART_BONUS_EVERY;
        int _hurt_timer = 0;
        int _shake_timer = 0;
        int _bounce_timer = 0;
        bn::fixed _player_x;
        bool _facing_left = false;
        int _walk_timer = 0;
        bn::fixed _sleigh_x;
        int _sleigh_dir = 1;
        int _shown_score = -1;
        int _shown_hearts = -1;
        int _shown_multiplier = -1;

        // ---------------------------------------------------------------- difficulty
        int seconds() const
        {
            return _play_frames / 60;
        }

        bn::fixed fall_speed() const
        {
            bn::fixed speed = FALL_SPEED_START + FALL_SPEED_PER_SECOND * bn::fixed(_play_frames) / 60;
            return bn::min(speed, FALL_SPEED_MAX);
        }

        int spawn_interval() const
        {
            int interval = SPAWN_INTERVAL_START - (SPAWN_INTERVAL_DROP_PER_MINUTE * _play_frames) / 3600;
            return bn::max(interval, SPAWN_INTERVAL_MIN);
        }

        int coal_chance() const
        {
            if(seconds() < COAL_FREE_SECONDS)
            {
                return 0;
            }

            int chance = COAL_CHANCE_START + (COAL_CHANCE_PER_MINUTE * (_play_frames - COAL_FREE_SECONDS * 60)) / 3600;
            return bn::min(chance, COAL_CHANCE_MAX);
        }

        bn::fixed sleigh_speed() const
        {
            bn::fixed speed = SLEIGH_SPEED_START + SLEIGH_SPEED_PER_MINUTE * bn::fixed(_play_frames) / 3600;
            return bn::min(speed, SLEIGH_SPEED_MAX);
        }

        int multiplier() const
        {
            return _streak >= COMBO_X3_AT ? 3 : _streak >= COMBO_X2_AT ? 2 : 1;
        }

        // ---------------------------------------------------------------- player
        void update_player()
        {
            bn::fixed speed = (bn::keypad::a_held() || bn::keypad::b_held()) ? PLAYER_DASH_SPEED : PLAYER_SPEED;
            bool moving = false;

            if(bn::keypad::left_held())
            {
                _player_x -= speed;
                _facing_left = true;
                moving = true;
            }
            else if(bn::keypad::right_held())
            {
                _player_x += speed;
                _facing_left = false;
                moving = true;
            }

            _player_x = clamp_fixed(_player_x, -PLAYER_LIMIT_X, PLAYER_LIMIT_X);

            if(moving)
            {
                _walk_timer += speed > PLAYER_SPEED ? 2 : 1;
            }
            else
            {
                _walk_timer = 0;
            }

            place_player(moving);
        }

        void place_player(bool moving)
        {
            int frame = sb::FRAME_CHEER;
            int bob = 0;

            if(moving)
            {
                int step = (_walk_timer / 8) % 4;
                frame = step == 1 ? sb::FRAME_SIDE_WALK_A : step == 3 ? sb::FRAME_SIDE_WALK_B : sb::FRAME_SIDE;
                bob = (step == 1 || step == 3) ? 1 : 0;
            }
            else if(_hurt_timer > HURT_SAFE_FRAMES - 24)
            {
                frame = sb::FRAME_DOWN;
            }

            _player.set_tiles(sb::character_sprite(sb::player_character()).tiles_item(), frame);
            _player.set_horizontal_flip(moving && _facing_left);
            _player.set_position(_player_x, PLAYER_Y);

            int fill = _caught >= 20 ? 2 : _caught >= 5 ? 1 : 0;
            _basket.set_tiles(bn::sprite_items::catch_basket.tiles_item(), fill);
            _basket.set_position(_player_x, PLAYER_Y + BASKET_OFFSET_Y + bob + (_bounce_timer > 0 ? 1 : 0));

            // Blink while safe after catching coal.
            bool visible = _hurt_timer <= 0 || (_hurt_timer / 4) % 2 == 0;
            _player.set_visible(visible);
            _basket.set_visible(visible);
        }

        // ---------------------------------------------------------------- sleigh
        void update_sleigh()
        {
            bn::fixed sx = sack_x();
            bool can_drop = sx >= -SLEIGH_DROP_LIMIT_X && sx <= SLEIGH_DROP_LIMIT_X;
            _sleigh_x += sleigh_speed() * _sleigh_dir * (can_drop ? 1 : SLEIGH_OFFSCREEN_SPEEDUP);

            if(_sleigh_x > SLEIGH_TURN_X)
            {
                _sleigh_dir = -1;
            }
            else if(_sleigh_x < -SLEIGH_TURN_X)
            {
                _sleigh_dir = 1;
            }

            place_sleigh();
        }

        void place_sleigh()
        {
            int t = sb::frame_count();
            bn::fixed bob = bn::sin(bn::fixed(t % 128) / 128) * 3;
            _sleigh.set_position(_sleigh_x, SLEIGH_Y + bob);
            _sleigh.set_horizontal_flip(_sleigh_dir < 0);
            _sleigh.set_tiles(bn::sprite_items::catch_sleigh.tiles_item(), (t / 10) % 2);
        }

        // Where gifts fall from: Santa's sack at the back of the sleigh.
        bn::fixed sack_x() const
        {
            return _sleigh_x - 20 * _sleigh_dir;
        }

        // ---------------------------------------------------------------- items
        void update_spawning()
        {
            --_spawn_timer;

            if(_spawn_timer > 0)
            {
                return;
            }

            bn::fixed sx = sack_x();

            if(sx < -SLEIGH_DROP_LIMIT_X || sx > SLEIGH_DROP_LIMIT_X)
            {
                return;   // wait until the sleigh is back over the street
            }

            spawn(sx);
            _spawn_timer = spawn_interval() + sb::rand_range(-SPAWN_INTERVAL_RANDOM, SPAWN_INTERVAL_RANDOM);
        }

        void spawn(bn::fixed sx)
        {
            item* free_item = nullptr;

            for(item& it : _items)
            {
                if(it.state == item_state::FREE)
                {
                    free_item = &it;
                    break;
                }
            }

            if(! free_item)
            {
                return;
            }

            item& it = *free_item;
            int roll = sb::rand_range(0, 99);
            int coal = coal_chance();

            if(roll < coal)
            {
                it.type = kind::COAL;
                it.frame = ITEM_FRAME_COAL;
            }
            else if(roll < coal + STAR_CHANCE)
            {
                it.type = kind::STAR;
                it.frame = ITEM_FRAME_STAR;
            }
            else if(roll < coal + STAR_CHANCE + BIG_CHANCE)
            {
                it.type = kind::BIG;
                it.frame = ITEM_FRAME_BIG + sb::rand_range(0, 1);
            }
            else
            {
                it.type = kind::SMALL;
                it.frame = ITEM_FRAME_SMALL + sb::rand_range(0, 2);
            }

            it.state = item_state::FALLING;
            it.timer = 0;
            it.x = clamp_fixed(sx + sb::rand_range(-DROP_SPREAD, DROP_SPREAD), -112, 112);
            it.y = SLEIGH_Y + 6;
            it.speed = fall_speed() * (100 + sb::rand_range(0, FALL_SPEED_RANDOM_PERCENT)) / 100;
            it.sprite.set_tiles(bn::sprite_items::catch_items.tiles_item(), it.frame);
            it.sprite.set_position(it.x, it.y);
            it.sprite.set_visible(true);
        }

        void update_items()
        {
            bn::fixed rim_y = PLAYER_Y + BASKET_OFFSET_Y + BASKET_RIM_OFFSET_Y;

            for(item& it : _items)
            {
                if(it.state == item_state::FREE)
                {
                    continue;
                }

                ++it.timer;

                if(it.state == item_state::LANDED)
                {
                    if(it.timer >= LANDED_FRAMES)
                    {
                        it.state = item_state::FREE;
                        it.sprite.set_visible(false);
                    }
                    else if(it.timer > LANDED_FRAMES - 16)
                    {
                        it.sprite.set_visible((it.timer / 3) % 2 == 0);
                    }

                    continue;
                }

                it.y += it.speed;

                // Little animations: the star sparkles, the coal looks around grumpily.
                if(it.type == kind::STAR)
                {
                    it.sprite.set_tiles(bn::sprite_items::catch_items.tiles_item(), ITEM_FRAME_STAR + (it.timer / 8) % 2);
                }
                else if(it.type == kind::COAL)
                {
                    it.sprite.set_tiles(bn::sprite_items::catch_items.tiles_item(), ITEM_FRAME_COAL + (it.timer / 20) % 2);
                }

                // Caught in the basket?
                bn::fixed dx = it.x - _player_x;

                if(it.y + 6 >= rim_y && it.y <= rim_y + 6 && dx >= -CATCH_HALF_WIDTH && dx <= CATCH_HALF_WIDTH)
                {
                    it.state = item_state::FREE;
                    it.sprite.set_visible(false);
                    on_catch(it);
                    continue;
                }

                if(it.y >= GROUND_Y)
                {
                    it.y = GROUND_Y;
                    it.state = item_state::LANDED;
                    it.timer = 0;

                    if(it.type != kind::COAL)
                    {
                        // Missing a gift costs nothing but the combo.
                        sb::sfx(bn::sound_items::sfx_miss);
                        _streak = 0;
                    }
                }

                it.sprite.set_position(it.x, it.y);
            }
        }

        void on_catch(const item& it)
        {
            if(it.type == kind::COAL)
            {
                if(_hurt_timer > 0)
                {
                    sb::sfx(bn::sound_items::sfx_bump);   // still blinking: no harm done
                    return;
                }

                sb::sfx(bn::sound_items::sfx_coal);
                --_hearts;
                _streak = 0;
                _hurt_timer = HURT_SAFE_FRAMES;
                _shake_timer = SHAKE_FRAMES;
                show_popup(_player_x, POPUP_HEART_MINUS);
                return;
            }

            int old_multiplier = multiplier();
            int base = it.type == kind::STAR ? POINTS_STAR : it.type == kind::BIG ? POINTS_BIG : POINTS_SMALL;
            int points = base * old_multiplier;
            _score += points;
            ++_caught;
            ++_streak;
            _bounce_timer = 6;
            show_popup(it.x, popup_frame(points));

            if(it.type == kind::STAR)
            {
                sb::sfx(bn::sound_items::sfx_point);
            }
            else
            {
                sb::sfx(bn::sound_items::sfx_catch);
            }

            if(multiplier() > old_multiplier)
            {
                sb::sfx(bn::sound_items::sfx_point);
            }

            while(_score >= _next_heart_score)
            {
                _next_heart_score += HEART_BONUS_EVERY;

                if(_hearts < MAX_HEARTS)
                {
                    ++_hearts;
                    sb::sfx(bn::sound_items::sfx_point);
                    show_popup(_player_x, POPUP_HEART_PLUS, 14);
                }
            }
        }

        // ---------------------------------------------------------------- popups and effects
        void show_popup(bn::fixed x, int frame, int y_offset = 0)
        {
            popup* best = &_popups[0];

            for(popup& p : _popups)
            {
                if(p.timer <= 0)
                {
                    best = &p;
                    break;
                }

                if(p.timer < best->timer)
                {
                    best = &p;
                }
            }

            best->timer = POPUP_FRAMES;
            best->y = PLAYER_Y + BASKET_OFFSET_Y - 14 - y_offset;
            best->sprite.set_tiles(bn::sprite_items::catch_popup.tiles_item(), frame);
            best->sprite.set_position(clamp_fixed(x, -104, 104), best->y);
            best->sprite.set_visible(true);
        }

        void update_popups()
        {
            for(popup& p : _popups)
            {
                if(p.timer <= 0)
                {
                    continue;
                }

                --p.timer;
                p.y -= bn::fixed(1) / 2;
                p.sprite.set_y(p.y);

                if(p.timer == 0)
                {
                    p.sprite.set_visible(false);
                }
                else if(p.timer < 10)
                {
                    p.sprite.set_visible(p.timer % 2 == 0);
                }
            }
        }

        void update_effects()
        {
            if(_hurt_timer > 0)
            {
                --_hurt_timer;
            }

            if(_bounce_timer > 0)
            {
                --_bounce_timer;
            }

            if(_shake_timer > 0)
            {
                --_shake_timer;

                if(_shake_timer == 0)
                {
                    _bg.set_position(0, 0);
                }
                else
                {
                    _bg.set_position(sb::rand_range(-SHAKE_PIXELS, SHAKE_PIXELS),
                                     sb::rand_range(-SHAKE_PIXELS, SHAKE_PIXELS));
                }
            }
        }

        // ---------------------------------------------------------------- HUD
        void update_hud()
        {
            if(_shown_score != _score)
            {
                _shown_score = _score;
                bn::string<24> text("Score ");
                text.append(sb::to_text(_score));
                _score_label.set(-114, -72, text, sb::text_color::WHITE);
            }

            int mult = multiplier();

            if(_shown_multiplier != mult)
            {
                _shown_multiplier = mult;

                if(mult > 1)
                {
                    bn::string<16> text("Combo x");
                    text.append(sb::to_text(mult));
                    _combo_label.set(-114, -58, text, sb::text_color::YELLOW);
                }
                else
                {
                    _combo_label.clear();
                }
            }

            if(_shown_hearts != _hearts)
            {
                _shown_hearts = _hearts;
                bn::string<8> full;
                bn::string<8> lost;

                for(int i = 0; i < MAX_HEARTS; ++i)
                {
                    if(i < _hearts)
                    {
                        full.push_back('@');
                    }
                    else
                    {
                        lost.push_back('@');
                    }
                }

                constexpr int right = 116;
                int lost_width = lost.empty() ? 0 : sb::text_width(lost);
                _lost_label.clear();
                _hearts_label.clear();

                if(! lost.empty())
                {
                    _lost_label.set(right, -72, lost, sb::text_color::DARK, sb::align::RIGHT);
                }

                if(! full.empty())
                {
                    _hearts_label.set(right - lost_width, -72, full, sb::text_color::RED, sb::align::RIGHT);
                }
            }
        }
    };
}

void sb::catch_run()
{
    constexpr bn::string_view howto[] = {
        "Catch the falling gifts!",
        "< > Walk   Hold A: Run",
        "Gift 10  Big 25  { 50",
        "Coal costs a heart @",
    };

    bn::bg_palettes::set_transparent_color(bn::color(4, 5, 12));

    {
        catch_game g;
        g.show_idle();
        play_music(bn::music_items::catch_song);
        fade_in();

        bool show_intro = true;
        bool playing = true;

        while(playing)
        {
            if(show_intro && ! game_intro(GAME_CATCH, howto))
            {
                break;
            }

            show_intro = false;
            g.reset();

            while(true)
            {
                bool alive = g.update();
                sb::update();

                if(bn::keypad::start_pressed() && alive)
                {
                    if(game_pause())
                    {
                        playing = false;
                        break;
                    }
                }

                if(! alive)
                {
                    g.game_over_pause();
                    bn::string<32> detail("Gifts caught: ");
                    detail.append(to_text(g.caught()));

                    if(game_over(GAME_CATCH, g.score(), detail))
                    {
                        play_music(bn::music_items::catch_song);
                    }
                    else
                    {
                        playing = false;
                    }

                    break;
                }
            }
        }

        fade_out();
        g.clear_hud();
    }

    panel_clear();
}
