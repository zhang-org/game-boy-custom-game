// Snowball Fight: you defend your snow fort against the rest of the family, who pop up from behind the snow
// drifts, walls and snowmen across the yard and throw back.
//
//   * The family pops up at three spots (left, middle, right). Left / Right picks one, A throws: it's like
//     whack-a-mole. Snowballs are unlimited (there used to be ammo, packed by ducking: Charly found that
//     not fun, and free aiming over seven spots too hectic).
//   * Hold B to duck behind your wall: snowballs thrown at you splat on it.
//   * Someone about to throw raises their arms and a "!" flashes over them. Their snowball grows as it
//     flies at you: duck in time, or hit it in the air with one of yours (Intercept!).
//   * Quick hits (right after someone pops up) and far targets score more.
//   * Five waves, each needing a few hits; later waves are faster and sneakier (fake peeks, two or three
//     at once, quicker throws). Lose all your hearts and it's over.
//
// Records: the best score per level (sb_save extra records SNOWBALL_RECORD_SLOT + level, see
// docs/decisions/0002-record-slots.md). Art: tools/art/make_snowball.py -> graphics/snowball/.
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
#include "bn_regular_bg_items_snowball_cover.h"
#include "bn_regular_bg_items_snowball_fort.h"
#include "bn_regular_bg_items_snowball_yard.h"
#include "bn_sound_items.h"
#include "bn_sprite_items_snowball_aim.h"
#include "bn_sprite_items_snowball_ball16.h"
#include "bn_sprite_items_snowball_ball32.h"
#include "bn_sprite_items_snowball_ball8.h"
#include "bn_sprite_items_snowball_face.h"
#include "bn_sprite_items_snowball_icons.h"
#include "bn_sprite_items_snowball_splat.h"
#include "bn_sprite_items_snowball_stars.h"
#include "bn_sprite_items_snowball_tell.h"

#include "sb_audio.h"
#include "sb_characters.h"
#include "sb_core.h"
#include "sb_games.h"
#include "sb_save.h"
#include "sb_text.h"
#include "sb_ui.h"

namespace
{
    // ============================================================================ tuning
    struct level_def
    {
        const char* name;
        int hearts;
        int windup;             // frames someone shows their arms up before throwing
        int flight;             // frames their snowball takes to reach you
        int peek_min, peek_max; // frames someone stays up when they don't throw
        int gap_min, gap_max;   // frames before someone pops up again
    };

    constexpr level_def LEVELS[3] = {
        //  name      hearts windup flight  peek        gap
        { "Easy",     5,     72,    76,     110, 160,   90, 160 },
        { "Medium",   4,     56,    64,     90, 130,    70, 130 },
        { "Hard",     3,     44,    54,     70, 110,    50, 100 },
    };
    constexpr int LEVEL_COUNT = 3;

    // The five waves: hits needed, speed (percent), and how sneaky they are.
    struct wave_def
    {
        int hits;
        int speed;              // windup, flight, peek and gap are divided by this (percent)
        int fake_percent;       // chance someone peeks without throwing (and ducks fast)
        int max_up;             // how many can be up at once
        int move_percent;       // chance someone pops up somewhere else than last time... far away
    };

    constexpr wave_def WAVES[5] = {
        { 4, 100, 0,  1, 30 },
        { 5, 110, 15, 2, 50 },
        { 6, 120, 25, 2, 70 },
        { 7, 135, 30, 3, 85 },
        { 8, 150, 35, 3, 100 },
    };
    constexpr int WAVE_COUNT = 5;

    constexpr int AMMO_MAX = 6;
    constexpr int RELOAD_FRAMES = 22;           // ducked this long = one more snowball
    constexpr int THROW_COOLDOWN = 12;
    constexpr bn::fixed AIM_SPEED = 2.6;
    constexpr int DUCK_FRAMES = 6;              // time to get fully down (and back up)
    constexpr int HURT_FRAMES = 70;             // the snow on your face
    constexpr int QUICK_FRAMES = 45;            // a hit this soon after someone starts to rise = quick bonus
    constexpr int HIT_REACH_X = 9;              // how close to someone your snowball must land
    constexpr int INTERCEPT_REACH = 11;

    constexpr int POINTS_NEAR = 100;
    constexpr int POINTS_MID = 120;
    constexpr int POINTS_FAR = 150;
    constexpr int POINTS_QUICK = 50;
    constexpr int POINTS_INTERCEPT = 75;
    constexpr int POINTS_WAVE = 200;            // x the wave number
    constexpr int POINTS_NO_DAMAGE = 150;       // a wave without getting hit

    // ============================================================================ layout
    // Cover spots (keep in sync with SPOTS in tools/art/make_snowball.py: screen x 44.. -> Butano x - 120,
    // top y - 80): x centre and the top of the cover.
    enum spot_row { ROW_FAR, ROW_MID, ROW_NEAR };

    struct spot
    {
        int x, top, row;
    };

    constexpr spot SPOTS[] = {
        { -76, -28, ROW_FAR }, { 0, -30, ROW_FAR }, { 76, -28, ROW_FAR },
        { -46, -4, ROW_MID }, { 46, -4, ROW_MID },
        { -94, 16, ROW_NEAR }, { 94, 16, ROW_NEAR },
    };
    constexpr int SPOT_COUNT = 7;
    constexpr int AIM_SPOTS = 3;                // the family only pops up at the three far drifts (SPOTS 0..2):
                                                // left, middle, right. The walls and near drifts are scenery.
    constexpr int RISE_PX = 24;                 // px someone rises from hidden to up

    constexpr int PLAYER_X = 0;
    constexpr int PLAYER_UP_Y = 36;             // your sprite's centre standing behind the wall
    constexpr int PLAYER_DOWN_Y = 60;           // ducked (hidden behind the wall)
    constexpr int HAND_X = 6, HAND_Y = 24;      // where your snowballs start
    constexpr int WALL_Y = 40;                  // where snowballs at you splat on the wall
    constexpr int AIM_MIN_Y = -64, AIM_MAX_Y = 30;

    constexpr int CAST = 4;                     // family members you play against
    constexpr int MY_BALLS = 4;
    constexpr int THEIR_BALLS = 5;
    constexpr int SPLATS = 6;

    // Depth: only a few z values (Butano has 16 sprite sort layers in total).
    constexpr int Z_TOP = -3;
    constexpr int Z_BALL = -2;
    constexpr int Z_MARK = -1;
    constexpr int Z_BODY = 1;

    // Records: sb_save extra record slots SNOWBALL_RECORD_SLOT .. +2 (Easy, Medium, Hard).
    constexpr int SNOWBALL_RECORD_SLOT = 15;

    // ============================================================================ data
    enum class foe_state
    {
        WAIT, RISE, UP, WINDUP, AFTER, SINK, HIT
    };

    struct foe
    {
        bn::sprite_ptr body;
        bn::sprite_ptr tell;
        bn::sprite_ptr stars;
        int who = 0;
        int spot = -1;
        foe_state state = foe_state::WAIT;
        int timer = 0;
        int age = 0;            // frames since it started rising
        int raised = 0;         // px above hidden (0..RISE_PX)
        bool fake = false;
        int shown_frame = -1;
    };

    struct ball
    {
        bn::sprite_ptr sprite;
        bool active = false;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        int t = 0, duration = 1;
        int arc = 0;
        int from = -1;          // the foe who threw it (their balls)
        int x = 0, y = 0;       // current position
        int look = -1;          // which sprite and frame it shows (only changed when needed: it's not free)
    };

    struct splat
    {
        bn::sprite_ptr sprite;
        int timer = 0;
    };

    int lerp(int a, int b, int t, int d)
    {
        return a + (b - a) * t / d;
    }

    class snowball_game
    {
    public:
        snowball_game() :
            _yard(bn::regular_bg_items::snowball_yard.create_bg(0, 0)),
            _cover(bn::regular_bg_items::snowball_cover.create_bg(0, 0)),
            _fort(bn::regular_bg_items::snowball_fort.create_bg(0, 0)),
            _player(sb::character_sprite(sb::player_character()).create_sprite(PLAYER_X, PLAYER_UP_Y, sb::FRAME_UP)),
            _aim(bn::sprite_items::snowball_aim.create_sprite(0, -20)),
            _face(bn::sprite_items::snowball_face.create_sprite(0, 0))
        {
            _yard.set_priority(3);
            _cover.set_priority(2);
            _fort.set_priority(1);
            _player.set_bg_priority(2);
            _player.set_z_order(Z_BODY);
            _aim.set_bg_priority(1);
            _aim.set_z_order(Z_MARK);
            _face.set_bg_priority(1);
            _face.set_z_order(Z_TOP);
            _face.set_visible(false);

            // The family you play against: four people who aren't you.
            int picked = 0;
            int start = sb::rand_range(0, sb::character_count - 1);

            for(int k = 0; k < sb::character_count && picked < CAST; ++k)
            {
                int who = (start + k * 3) % sb::character_count;   // a shuffled-looking order

                if(who == sb::player_character())
                {
                    continue;
                }

                bool taken = false;

                for(const foe& f : _foes)
                {
                    taken |= f.who == who;
                }

                if(taken)
                {
                    continue;
                }

                foe f { sb::character_sprite(who).create_sprite(0, 0, sb::FRAME_DOWN),
                        bn::sprite_items::snowball_tell.create_sprite(0, 0),
                        bn::sprite_items::snowball_stars.create_sprite(0, 0) };
                f.who = who;
                f.body.set_bg_priority(3);
                f.body.set_z_order(Z_BODY);
                f.body.set_visible(false);
                f.tell.set_bg_priority(2);
                f.tell.set_z_order(Z_MARK);
                f.tell.set_visible(false);
                f.stars.set_bg_priority(2);
                f.stars.set_z_order(Z_MARK);
                f.stars.set_visible(false);
                _foes.push_back(f);
                ++picked;
            }

            for(int i = 0; i < MY_BALLS; ++i)
            {
                ball b { bn::sprite_items::snowball_ball8.create_sprite(0, 0) };
                b.sprite.set_bg_priority(1);
                b.sprite.set_z_order(Z_BALL);
                b.sprite.set_visible(false);
                _mine.push_back(b);
            }

            for(int i = 0; i < THEIR_BALLS; ++i)
            {
                ball b { bn::sprite_items::snowball_ball8.create_sprite(0, 0) };
                b.sprite.set_bg_priority(1);
                b.sprite.set_z_order(Z_BALL);
                b.sprite.set_visible(false);
                _theirs.push_back(b);
            }

            for(int i = 0; i < SPLATS; ++i)
            {
                splat s { bn::sprite_items::snowball_splat.create_sprite(0, 0) };
                s.sprite.set_bg_priority(2);
                s.sprite.set_z_order(Z_MARK);
                s.sprite.set_visible(false);
                _splats.push_back(s);
            }

            for(int i = 0; i < AMMO_MAX; ++i)
            {
                bn::sprite_ptr icon = bn::sprite_items::snowball_icons.create_sprite(-112 + i * 8, 72, 2);
                icon.set_bg_priority(1);
                icon.set_z_order(Z_MARK);
                _ammo_icons.push_back(icon);
            }

            for(int i = 0; i < 5; ++i)
            {
                bn::sprite_ptr icon = bn::sprite_items::snowball_icons.create_sprite(-112 + i * 9, -71, 0);
                icon.set_bg_priority(1);
                icon.set_z_order(Z_MARK);
                _heart_icons.push_back(icon);
            }

            reset(0);
            set_hud_visible(false);
        }

        void reset(int level)
        {
            _level = level;
            _hearts = def().hearts;
            _score = 0;
            _wave = 0;
            _wave_hits = 0;
            _wave_damage = false;
            _hits = 0;
            _throws = 0;
            _intercepts = 0;
            _quick = 0;
            _ammo = AMMO_MAX;
            _reload = 0;
            _cooldown = 0;
            _duck = 0;
            _hurt = 0;
            _shake = 0;
            _banner_timer = 0;
            _between = 90;      // "Wave 1" shows first
            _finished = false;
            _won = false;
            _aim_spot = 1;
            _aim_x = SPOTS[1].x;
            _aim_y = SPOTS[1].top - 10;
            _t = 0;
            _shown_score = -1;
            _shown_wave = -1;
            _shown_hits = -1;
            _shown_hearts = -1;
            _shown_ammo = -1;

            for(foe& f : _foes)
            {
                f.state = foe_state::WAIT;
                f.spot = -1;
                f.timer = sb::rand_range(40, 120);
                f.body.set_visible(false);
                f.tell.set_visible(false);
                f.stars.set_visible(false);
            }

            for(ball& b : _mine)
            {
                b.active = false;
                b.sprite.set_visible(false);
            }

            for(ball& b : _theirs)
            {
                b.active = false;
                b.sprite.set_visible(false);
            }

            for(splat& s : _splats)
            {
                s.timer = 0;
                s.sprite.set_visible(false);
            }

            _face.set_visible(false);
            _banner.clear();
            _popup.clear();
            show_banner("Wave 1", sb::text_color::YELLOW, 90);
            draw();
        }

        // One frame. Returns false when the game is over (won or out of hearts).
        bool step()
        {
            ++_t;
            update_player();
            update_waves();
            update_foes();
            update_my_balls();
            update_their_balls();
            update_effects();
            draw();
            update_hud();
            return ! _finished;
        }

        void set_hud_visible(bool visible)
        {
            _score_label.set_visible(visible);
            _wave_label.set_visible(visible);
            _banner.set_visible(visible);
            _popup.set_visible(visible);

            for(bn::sprite_ptr& icon : _ammo_icons)
            {
                icon.set_visible(false);    // unlimited snowballs now: no ammo to show
            }

            for(int i = 0; i < _heart_icons.size(); ++i)
            {
                _heart_icons[i].set_visible(visible && i < def().hearts);
            }

            _aim.set_visible(visible);
        }

        void clear_hud()
        {
            set_hud_visible(false);
            _score_label.clear();
            _wave_label.clear();
            _banner.clear();
            _popup.clear();
            _shown_score = -1;
            _shown_wave = -1;
            _shown_hits = -1;
            _shown_hearts = -1;
            _shown_ammo = -1;
        }

        int score() const
        {
            return _score;
        }

        int level() const
        {
            return _level;
        }

        bool won() const
        {
            return _won;
        }

        int waves_cleared() const
        {
            return _won ? WAVE_COUNT : _wave;
        }

        int hits() const
        {
            return _hits;
        }

        int throws() const
        {
            return _throws;
        }

        int intercepts() const
        {
            return _intercepts;
        }

    private:
        bn::regular_bg_ptr _yard, _cover, _fort;
        bn::sprite_ptr _player, _aim, _face;
        bn::vector<foe, CAST> _foes;
        bn::vector<ball, MY_BALLS> _mine;
        bn::vector<ball, THEIR_BALLS> _theirs;
        bn::vector<splat, SPLATS> _splats;
        bn::vector<bn::sprite_ptr, AMMO_MAX> _ammo_icons;
        bn::vector<bn::sprite_ptr, 5> _heart_icons;
        sb::label _score_label, _wave_label, _banner, _popup;

        int _level = 0;
        int _hearts = 0;
        int _score = 0;
        int _wave = 0;            // 0-based
        int _wave_hits = 0;
        bool _wave_damage = false;
        int _hits = 0;
        int _throws = 0;
        int _intercepts = 0;
        int _quick = 0;
        int _ammo = 0;
        int _aim_spot = 1;          // 0 left, 1 middle, 2 right
        int _reload = 0;
        int _cooldown = 0;
        int _duck = 0;            // 0 = up .. DUCK_FRAMES = fully down
        int _hurt = 0;
        int _shake = 0;
        int _banner_timer = 0;
        int _popup_timer = 0;
        int _between = 0;         // frames of pause between waves
        bool _finished = false;
        bool _won = false;
        bn::fixed _aim_x, _aim_y;
        int _t = 0;
        int _shown_score = -1;
        int _shown_wave = -1;
        int _shown_hits = -1;
        int _shown_hearts = -1;
        int _shown_ammo = -1;
        bool _shown_throwing = false;
        bool _shown_hot = false;

        const level_def& def() const
        {
            return LEVELS[_level];
        }

        const wave_def& wave() const
        {
            return WAVES[bn::min(_wave, WAVE_COUNT - 1)];
        }

        // A level time, sped up by the wave.
        int scaled(int frames) const
        {
            return bn::max(8, frames * 100 / wave().speed);
        }

        // ---------------------------------------------------------------- input
#ifdef SNOWBALL_AUTOPILOT
        // Test builds only (make USERFLAGS=-DSNOWBALL_AUTOPILOT): aims at whoever is up, throws when on
        // target, ducks when a snowball is about to arrive, and reloads when low.
        bool _auto_duck = false;
        int _auto_dx = 0;
        bool _auto_throw = false;

        void autopilot()
        {
            _auto_duck = false;
            _auto_throw = false;
            _auto_dx = 0;

            for(const ball& b : _theirs)
            {
                if(b.active && b.t * 100 / b.duration > 70)
                {
                    _auto_duck = true;
                }
            }

            const foe* target = nullptr;

            for(const foe& f : _foes)
            {
                if((f.state == foe_state::UP || f.state == foe_state::WINDUP || f.state == foe_state::RISE) &&
                   (! target || f.age > target->age))
                {
                    target = &f;
                }
            }

            if(! target)
            {
                return;
            }

            const spot& s = SPOTS[target->spot];
            _auto_dx = target->spot > _aim_spot ? 1 : target->spot < _aim_spot ? -1 : 0;
            _auto_throw = _auto_dx == 0 && target->raised >= RISE_PX - 4 && ! my_ball_heading_to(s.x);
        }

        bool my_ball_heading_to(int x) const
        {
            for(const ball& b : _mine)
            {
                if(b.active && bn::abs(b.x1 - x) < 8)
                {
                    return true;
                }
            }

            return false;
        }

        bool any_up() const
        {
            for(const foe& f : _foes)
            {
                if(f.state != foe_state::WAIT)
                {
                    return true;
                }
            }

            return false;
        }

        bool duck_held() const { return _auto_duck; }
        bool throw_pressed() const { return _auto_throw; }
        int aim_step() const { return _auto_dx; }
#else
        static bool duck_held() { return bn::keypad::b_held(); }
        static bool throw_pressed() { return bn::keypad::a_pressed(); }
        static int aim_step() { return bn::keypad::left_pressed() ? -1 : bn::keypad::right_pressed() ? 1 : 0; }
#endif

        // ---------------------------------------------------------------- you
        void update_player()
        {
#ifdef SNOWBALL_AUTOPILOT
            autopilot();
#endif
            bool down = duck_held() && ! _finished;
            _duck = bn::clamp(_duck + (down ? 1 : -1), 0, DUCK_FRAMES);

            // Left / Right picks one of the three spots; the aim glides there.
            _aim_spot = bn::clamp(_aim_spot + aim_step(), 0, AIM_SPOTS - 1);
            const spot& target = SPOTS[_aim_spot];
            _aim_x += (bn::fixed(target.x) - _aim_x) / 3;
            _aim_y += (bn::fixed(target.top - 10) - _aim_y) / 3;

            if(_cooldown > 0)
            {
                --_cooldown;
            }

            // Unlimited snowballs: you just can't throw from behind the wall.
            if(throw_pressed() && ! _finished && _between == 0 && _duck == 0 && _cooldown == 0)
            {
                throw_mine();
            }
        }

        void throw_mine()
        {
            for(ball& b : _mine)
            {
                if(b.active)
                {
                    continue;
                }

                // Straight at the chosen spot (not where the gliding aim happens to be right now).
                int tx = SPOTS[_aim_spot].x, ty = SPOTS[_aim_spot].top - 10;
                int dist = bn::abs(tx - HAND_X) + bn::abs(ty - HAND_Y);
                b.active = true;
                b.x0 = PLAYER_X + HAND_X;
                b.y0 = HAND_Y;
                b.x1 = tx;
                b.y1 = ty;
                b.t = 0;
                b.duration = 10 + dist / 10;   // quick: it's about timing, not leading your shots
                b.arc = 10 + dist / 6;
                b.look = -1;
                b.sprite.set_visible(true);
                ++_throws;
                _cooldown = THROW_COOLDOWN;
                sb::sfx(bn::sound_items::sfx_whoosh);
                return;
            }
        }

        // ---------------------------------------------------------------- waves
        void update_waves()
        {
            if(_between > 0)
            {
                --_between;
            }
        }

        int up_count() const
        {
            int n = 0;

            for(const foe& f : _foes)
            {
                n += f.state != foe_state::WAIT && f.state != foe_state::HIT;
            }

            return n;
        }

        bool spot_taken(int s) const
        {
            for(const foe& f : _foes)
            {
                if(f.state != foe_state::WAIT && f.spot == s)
                {
                    return true;
                }
            }

            return false;
        }

        void wave_cleared()
        {
            int bonus = POINTS_WAVE * (_wave + 1) + (_wave_damage ? 0 : POINTS_NO_DAMAGE);
            _score += bonus;
            sb::sfx(bn::sound_items::sfx_record);

            // Everyone ducks.
            for(foe& f : _foes)
            {
                if(f.state != foe_state::WAIT && f.state != foe_state::HIT)
                {
                    f.state = foe_state::SINK;
                }
            }

            for(ball& b : _theirs)
            {
                b.active = false;
                b.sprite.set_visible(false);
            }

            if(_wave + 1 >= WAVE_COUNT)
            {
                _won = true;
                _finished = true;
                show_banner("You win the fight!", sb::text_color::YELLOW, 200);
                return;
            }

            bn::string<32> text("Wave ");
            text.append(sb::to_text(_wave + 1));
            text.append(" clear! +");
            text.append(sb::to_text(bonus));
            show_banner(text, sb::text_color::YELLOW, 100);
            ++_wave;
            _wave_hits = 0;
            _wave_damage = false;
            _between = 150;
            _pending_wave_banner = 100;
        }

        int _pending_wave_banner = 0;

        // ---------------------------------------------------------------- the family
        void update_foes()
        {
            if(_pending_wave_banner > 0 && --_pending_wave_banner == 0)
            {
                bn::string<16> text("Wave ");
                text.append(sb::to_text(_wave + 1));
                show_banner(text, sb::text_color::YELLOW, 60);
            }

            for(int i = 0; i < _foes.size(); ++i)
            {
                foe& f = _foes[i];
                ++f.age;

                switch(f.state)
                {
                case foe_state::WAIT:
                    if(_finished || _between > 0)
                    {
                        break;
                    }

                    if(--f.timer <= 0)
                    {
                        if(up_count() >= wave().max_up)
                        {
                            f.timer = 20;
                            break;
                        }

                        pop_up(f);
                    }
                    break;

                case foe_state::RISE:
                    f.raised = bn::min(f.raised + 3, RISE_PX);

                    if(f.raised >= RISE_PX)
                    {
                        f.state = foe_state::UP;
                        f.timer = f.fake ? scaled(sb::rand_range(20, 40)) :
                                           scaled(sb::rand_range(def().peek_min, def().peek_max)) / 3;
                    }
                    break;

                case foe_state::UP:
                    if(--f.timer <= 0)
                    {
                        if(f.fake || _finished)
                        {
                            f.state = foe_state::SINK;
                        }
                        else
                        {
                            f.state = foe_state::WINDUP;
                            f.timer = scaled(def().windup);
                            sb::sfx(bn::sound_items::sfx_countdown);
                        }
                    }
                    break;

                case foe_state::WINDUP:
                    if(--f.timer <= 0)
                    {
                        throw_theirs(i);
                        f.state = foe_state::AFTER;
                        f.timer = scaled(30);
                    }
                    break;

                case foe_state::AFTER:
                    if(--f.timer <= 0)
                    {
                        f.state = foe_state::SINK;
                    }
                    break;

                case foe_state::SINK:
                    f.raised = bn::max(f.raised - 4, 0);

                    if(f.raised == 0)
                    {
                        f.state = foe_state::WAIT;
                        f.timer = scaled(sb::rand_range(def().gap_min, def().gap_max));
                    }
                    break;

                case foe_state::HIT:
                    if(--f.timer <= 0)
                    {
                        f.state = foe_state::SINK;
                        f.stars.set_visible(false);
                    }
                    else if(f.timer < 20)
                    {
                        f.raised = bn::max(f.raised - 1, 0);
                    }
                    break;

                default:
                    break;
                }
            }
        }

        void pop_up(foe& f)
        {
            // Somewhere free. Later waves move around more (and prefer a different place than before).
            int choices[SPOT_COUNT];
            int n = 0;

            for(int s = 0; s < AIM_SPOTS; ++s)
            {
                if(! spot_taken(s) && (s != f.spot || sb::rand_range(0, 99) >= wave().move_percent))
                {
                    choices[n++] = s;
                }
            }

            if(n == 0)
            {
                f.timer = 20;
                return;
            }

            f.spot = choices[sb::rand_range(0, n - 1)];
            f.state = foe_state::RISE;
            f.raised = 0;
            f.age = 0;
            f.fake = sb::rand_range(0, 99) < wave().fake_percent;
        }

        void throw_theirs(int index)
        {
            foe& f = _foes[index];
            const spot& s = SPOTS[f.spot];

            for(ball& b : _theirs)
            {
                if(b.active)
                {
                    continue;
                }

                b.active = true;
                b.from = index;
                b.x0 = s.x + 4;
                b.y0 = s.top - 18;
                b.x1 = PLAYER_X + sb::rand_range(-14, 14);
                b.y1 = WALL_Y - 6;
                b.t = 0;
                b.duration = scaled(def().flight) + (2 - s.row) * 6;     // from far away it takes longer
                b.arc = 18 + (2 - s.row) * 8;
                b.sprite.set_item(bn::sprite_items::snowball_ball8, 0);
                b.look = 0;
                b.sprite.set_visible(true);
                sb::sfx(bn::sound_items::sfx_whoosh);
                return;
            }
        }

        void hit_foe(foe& f, int x, int y)
        {
            const spot& s = SPOTS[f.spot];
            int points = s.row == ROW_FAR ? POINTS_FAR : s.row == ROW_MID ? POINTS_MID : POINTS_NEAR;
            bool quick = f.age <= QUICK_FRAMES;

            if(quick)
            {
                points += POINTS_QUICK;
                ++_quick;
            }

            _score += points;
            ++_hits;
            ++_wave_hits;
            f.state = foe_state::HIT;
            f.timer = 50;
            f.tell.set_visible(false);
            add_splat(x, y, 2);
            sb::sfx(bn::sound_items::sfx_pp_perfect);

            bn::string<24> text("+");
            text.append(sb::to_text(points));

            if(quick)
            {
                text.append(" Quick!");
            }

            show_popup(bn::clamp(s.x, -80, 80), s.top - 30, text, sb::text_color::YELLOW);

            if(_wave_hits >= wave().hits)
            {
                wave_cleared();
            }
        }

        // ---------------------------------------------------------------- snowballs
        static void move_ball(ball& b)
        {
            ++b.t;
            int t = bn::min(b.t, b.duration);
            b.x = lerp(b.x0, b.x1, t, b.duration);
            b.y = lerp(b.y0, b.y1, t, b.duration) - 4 * b.arc * t * (b.duration - t) / (b.duration * b.duration);
            b.sprite.set_position(b.x, b.y);
        }

        void update_my_balls()
        {
            for(ball& b : _mine)
            {
                if(! b.active)
                {
                    continue;
                }

                move_ball(b);
                // Gets smaller as it flies away from you.
                int look = b.t * 2 < b.duration ? 1 : 0;

                if(look != b.look)
                {
                    b.look = look;
                    b.sprite.set_tiles(bn::sprite_items::snowball_ball8.tiles_item(), look);
                }

                if(b.t < b.duration)
                {
                    continue;
                }

                b.active = false;
                b.sprite.set_visible(false);
                land_mine(b.x1, b.y1);
            }
        }

        void land_mine(int x, int y)
        {
            // Hit one of their snowballs in the air?
            for(ball& t : _theirs)
            {
                if(t.active && bn::abs(t.x - x) <= INTERCEPT_REACH && bn::abs(t.y - y) <= INTERCEPT_REACH)
                {
                    t.active = false;
                    t.sprite.set_visible(false);
                    _score += POINTS_INTERCEPT;
                    ++_intercepts;
                    add_splat(x, y, 1);
                    sb::sfx(bn::sound_items::sfx_pp_perfect);
                    show_popup(bn::clamp(x, -80, 80), y - 14, "Intercept! +75", sb::text_color::YELLOW);
                    return;
                }
            }

            // Hit someone?
            for(foe& f : _foes)
            {
                if(f.state == foe_state::WAIT || f.state == foe_state::HIT || f.raised < RISE_PX / 2)
                {
                    continue;
                }

                const spot& s = SPOTS[f.spot];
                int head_top = s.top - f.raised + 2;

                if(bn::abs(x - s.x) <= HIT_REACH_X && y >= head_top - 4 && y <= s.top + 2)
                {
                    hit_foe(f, x, y);
                    return;
                }
            }

            add_splat(x, y, 1);
            sb::sfx(bn::sound_items::sfx_land);
        }

        void update_their_balls()
        {
            for(ball& b : _theirs)
            {
                if(! b.active)
                {
                    continue;
                }

                move_ball(b);
                int p = b.t * 100 / b.duration;

                // It grows as it comes at you.
                int look = p < 15 ? 0 : p < 30 ? 1 : p < 50 ? 2 : p < 70 ? 3 : p < 85 ? 4 : 5;

                if(look != b.look)
                {
                    b.look = look;

                    if(look < 2)
                    {
                        b.sprite.set_item(bn::sprite_items::snowball_ball8, look);
                    }
                    else if(look < 4)
                    {
                        b.sprite.set_item(bn::sprite_items::snowball_ball16, look - 2);
                    }
                    else
                    {
                        b.sprite.set_item(bn::sprite_items::snowball_ball32, look - 4);
                    }
                }

                if(b.t < b.duration)
                {
                    continue;
                }

                b.active = false;
                b.sprite.set_visible(false);

                if(_duck >= DUCK_FRAMES - 2)
                {
                    // Splat on your wall.
                    add_splat(b.x1, WALL_Y + 2, 0, 1);
                    sb::sfx(bn::sound_items::sfx_bump);
                    show_popup(0, 14, "Blocked!", sb::text_color::WHITE);
                }
                else if(_hurt == 0 && ! _finished)
                {
                    get_hit();
                }
            }
        }

        void get_hit()
        {
            --_hearts;
            _wave_damage = true;
            _hurt = HURT_FRAMES;
            _shake = 16;
            _face.set_visible(true);
            _face.set_tiles(bn::sprite_items::snowball_face.tiles_item(), 0);
            _face.set_position(sb::rand_range(-20, 20), sb::rand_range(-10, 10));
            sb::sfx(bn::sound_items::sfx_hurt);

            if(_hearts <= 0)
            {
                _finished = true;
                show_banner("Snowed under!", sb::text_color::RED, 200);
            }
        }

        void add_splat(int x, int y, int priority_layer, int bg_priority = 2)
        {
            for(splat& s : _splats)
            {
                if(s.timer > 0)
                {
                    continue;
                }

                s.timer = 24;
                s.sprite.set_position(x, y);
                s.sprite.set_bg_priority(bg_priority);
                s.sprite.set_tiles(bn::sprite_items::snowball_splat.tiles_item(), 0);
                s.sprite.set_visible(true);
                (void) priority_layer;
                return;
            }
        }

        // ---------------------------------------------------------------- effects, drawing, HUD
        void update_effects()
        {
            for(splat& s : _splats)
            {
                if(s.timer > 0)
                {
                    --s.timer;
                    s.sprite.set_tiles(bn::sprite_items::snowball_splat.tiles_item(), bn::min(2, (24 - s.timer) / 8));
                    s.sprite.set_visible(s.timer > 0);
                }
            }

            if(_hurt > 0)
            {
                --_hurt;
                _face.set_tiles(bn::sprite_items::snowball_face.tiles_item(), _hurt < HURT_FRAMES / 2 ? 1 : 0);
                _face.set_y(_face.y() + (_hurt < HURT_FRAMES / 2 ? bn::fixed(0.5) : bn::fixed(0)));
                _face.set_visible(_hurt > 0 && (_hurt > 12 || _hurt % 2));
            }

            if(_shake > 0)
            {
                --_shake;
                int dx = _shake ? sb::rand_range(-2, 2) : 0;
                int dy = _shake ? sb::rand_range(-2, 2) : 0;
                _yard.set_position(dx, dy);
                _cover.set_position(dx, dy);
                _fort.set_position(dx, dy);
            }

            if(_banner_timer > 0 && --_banner_timer == 0)
            {
                _banner.clear();
            }

            if(_popup_timer > 0 && --_popup_timer == 0)
            {
                _popup.clear();
            }
        }

        void draw()
        {
            int py = PLAYER_UP_Y + (PLAYER_DOWN_Y - PLAYER_UP_Y) * _duck / DUCK_FRAMES;
            bool throwing = _cooldown > THROW_COOLDOWN - 6;
            _player.set_position(PLAYER_X, py);

            if(throwing != _shown_throwing)
            {
                _shown_throwing = throwing;
                _player.set_tiles(sb::character_sprite(sb::player_character()).tiles_item(),
                                  throwing ? sb::FRAME_UP_WALK_A : sb::FRAME_UP);
            }

            for(foe& f : _foes)
            {
                bool visible = f.state != foe_state::WAIT && f.spot >= 0;
                f.body.set_visible(visible);

                if(! visible)
                {
                    f.tell.set_visible(false);
                    continue;
                }

                const spot& s = SPOTS[f.spot];
                // Hidden: the sprite's top at the cover's top (centre top + 16). Up: centre top + 16 - RISE_PX.
                int y = s.top + 16 - f.raised;
                int frame = f.state == foe_state::WINDUP ? sb::FRAME_CHEER :
                            f.state == foe_state::HIT ? sb::FRAME_SIT : sb::FRAME_DOWN;
                f.body.set_position(s.x, y);

                if(frame != f.shown_frame)
                {
                    f.shown_frame = frame;
                    f.body.set_tiles(sb::character_sprite(f.who).tiles_item(), frame);
                }

                bool tell = f.state == foe_state::WINDUP;
                f.tell.set_visible(tell);

                if(tell)
                {
                    f.tell.set_position(s.x + 9, y - 18);
                    f.tell.set_tiles(bn::sprite_items::snowball_tell.tiles_item(), (_t / 5) % 2);
                }

                if(f.state == foe_state::HIT)
                {
                    f.stars.set_visible(true);
                    f.stars.set_position(s.x, y - 18);
                    f.stars.set_tiles(bn::sprite_items::snowball_stars.tiles_item(), (_t / 8) % 2);
                }
            }

            // The aim turns red over someone you can hit.
            bool hot = false;

            for(const foe& f : _foes)
            {
                if(f.state != foe_state::WAIT && f.state != foe_state::HIT && f.raised >= RISE_PX / 2)
                {
                    const spot& s = SPOTS[f.spot];
                    int ax = _aim_x.integer(), ay = _aim_y.integer();
                    hot |= bn::abs(ax - s.x) <= HIT_REACH_X && ay >= s.top - f.raised - 2 && ay <= s.top + 2;
                }
            }

            _aim.set_position(_aim_x, _aim_y);

            if(hot != _shown_hot)
            {
                _shown_hot = hot;
                _aim.set_tiles(bn::sprite_items::snowball_aim.tiles_item(), hot ? 1 : 0);
            }
        }

        void show_banner(const bn::string_view& text, sb::text_color color, int frames)
        {
            _banner.set(0, -48, text, color, sb::align::CENTER);
            _banner_timer = frames;
        }

        void show_popup(int x, int y, const bn::string_view& text, sb::text_color color)
        {
            _popup.set(x, y, text, color, sb::align::CENTER);
            _popup_timer = 45;
        }

        void update_hud()
        {
            if(_score != _shown_score)
            {
                _shown_score = _score;
                bn::string<24> text("Score ");
                text.append(sb::to_text(_score));
                _score_label.set(114, -72, text, sb::text_color::WHITE, sb::align::RIGHT);
            }

            if(_wave != _shown_wave || _wave_hits != _shown_hits)
            {
                _shown_wave = _wave;
                _shown_hits = _wave_hits;
                bn::string<24> text("Wave ");
                text.append(sb::to_text(_wave + 1));
                text.append(": ");
                text.append(sb::to_text(bn::min(_wave_hits, wave().hits)));
                text.append("/");
                text.append(sb::to_text(wave().hits));
                _wave_label.set(0, -72, text, sb::text_color::WHITE, sb::align::CENTER);
            }

            if(_hearts != _shown_hearts)
            {
                _shown_hearts = _hearts;

                for(int i = 0; i < _heart_icons.size(); ++i)
                {
                    _heart_icons[i].set_tiles(bn::sprite_items::snowball_icons.tiles_item(), i < _hearts ? 0 : 1);
                }
            }

        }
    };

    int g_level_choice = 0;

    bool choose_level()
    {
        sb::label title;
        title.set(0, -60, "How tough is the family?", sb::text_color::WHITE, sb::align::CENTER);
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

    // The result box. Returns true to play again.
    bool show_results(const snowball_game& game)
    {
        bool record = game.score() > 0 &&
                      sb::submit_extra(SNOWBALL_RECORD_SLOT + game.level(), game.score(), sb::player_character());
        sb::play_jingle(record || game.won() ? bn::music_items::jingle_record : bn::music_items::jingle_over);

        // Everything fits in the box (rows 0..13) so the menu below (rows 14..19) doesn't cover it.
        sb::panel(3, 0, 24, 14);
        sb::label title, result, detail1, detail2, heading, lines[LEVEL_COUNT], record_label;
        title.set(0, -71, game.won() ? "You win the fight!" : "Snowed under!",
                  game.won() ? sb::text_color::BLUE : sb::text_color::RED, sb::align::CENTER);

        bn::string<40> text(LEVELS[game.level()].name);
        text.append(": ");
        text.append(sb::to_text(game.score()));
        text.append(" points");
        result.set(0, -59, text, sb::text_color::DARK, sb::align::CENTER);

        bn::string<40> d1("Waves ");
        d1.append(sb::to_text(game.waves_cleared()));
        d1.append("/5  Hits ");
        d1.append(sb::to_text(game.hits()));
        d1.append("/");
        d1.append(sb::to_text(game.throws()));
        detail1.set(0, -48, d1, sb::text_color::DARK, sb::align::CENTER);

        bn::string<40> d2("Intercepts ");
        d2.append(sb::to_text(game.intercepts()));
        detail2.set(0, -37, d2, sb::text_color::DARK, sb::align::CENTER);
        heading.set(0, -24, "{ Best scores {", sb::text_color::RED, sb::align::CENTER);

        for(int l = 0; l < LEVEL_COUNT; ++l)
        {
            bn::string<40> best(LEVELS[l].name);
            best.append(":  ");
            int value = sb::extra_score(SNOWBALL_RECORD_SLOT + l);

            if(value > 0)
            {
                best.append(sb::to_text(value));
                best.append("  ");
                best.append(sb::extra_name(SNOWBALL_RECORD_SLOT + l));
            }
            else
            {
                best.append("---");
            }

            lines[l].set(-72, -12 + l * 11, best, l == game.level() ? sb::text_color::BLUE : sb::text_color::DARK);
        }

        for(int t = 0; t < 50; ++t)
        {
            if(record && t % 20 == 0)
            {
                record_label.set(0, 22, "{ NEW RECORD! {", sb::text_color::YELLOW, sb::align::CENTER);
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

    bn::string<16> record_value_text(int value)
    {
        return sb::to_text(value);
    }
}

// The best record shown on the map, the intro and the high score list (sb_games.h: game_info::best_text).
bn::string<40> sb::snowball_best_text(bool compact)
{
    static constexpr const char* names[] = { "Easy", "Medium", "Hard" };
    return level_record_text(SNOWBALL_RECORD_SLOT, LEVEL_COUNT, names, record_value_text, compact);
}

void sb::snowball_run()
{
    // Shown on the start screen (scroll with Up/Down) and in the pause menu. '#' = heading.
    static constexpr bn::string_view howto[] = {
        "#Defend your fort!",
        "The family hides behind",
        "three snow piles: left,",
        "middle and right. They",
        "pop up to throw.",
        "",
        "#Throw",
        "< > pick a snow pile.",
        "A: throw. Snowballs never",
        "run out: hit them before",
        "they throw! The aim turns",
        "red when someone's there.",
        "",
        "#Duck",
        "Hold B to duck behind",
        "your wall: their snow-",
        "balls splat on it.",
        "",
        "#Watch for",
        "Arms up and a ! means",
        "they're about to throw.",
        "Quick hits score more.",
        "",
        "#Waves",
        "Clear 5 waves to win.",
        "Each gets faster and",
        "sneakier. Get hit too",
        "often and you're out!",
    };

    bn::bg_palettes::set_transparent_color(bn::color(8, 12, 22));

    {
        snowball_game game;
        play_music(bn::music_items::catch_song);
        fade_in();

        if(game_intro(GAME_SNOWBALL, howto))
        {
            while(choose_level())
            {
                game.reset(g_level_choice);
                game.set_hud_visible(true);
                bool quit = false;

                while(true)
                {
                    bool playing = game.step();
                    update();

                    if(bn::keypad::start_pressed() && playing)
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

                    if(! playing)
                    {
                        break;
                    }
                }

                if(quit)
                {
                    break;
                }

                // Let the last moment play out a little.
                for(int i = 0; i < 70; ++i)
                {
                    game.step();
                    update();
                }

                BN_LOG("Snowball level ", game.level(), ": score ", game.score(), ", waves ", game.waves_cleared(),
                       ", hits ", game.hits(), "/", game.throws(), ", intercepts ", game.intercepts());
                game.clear_hud();

                if(! show_results(game))
                {
                    break;
                }

                play_music(bn::music_items::catch_song);
            }
        }

        game.clear_hud();
        fade_out();
    }

    panel_clear();
}
