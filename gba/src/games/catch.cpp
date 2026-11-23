// Gift Tower: Santa's sleigh flies over the toy workshop and drops presents. Catch them on a stack in your
// arms, then carry the stack to Santa's parked sleigh to cash it in. Taller stacks are worth much more,
// but they wobble when you move, and if one leans too far it topples and every present in it is lost.
//
//   * Each present adds to the stack. The catch point is the top of the stack, and it leans with it.
//   * Moving (starting, stopping, turning) pushes the stack; tall stacks tip further and need you to step
//     under them to balance. Hold B to tiptoe (gentle steps). Catching off-centre also pushes it.
//   * Coal knocks the stack. Press A at the parked sleigh to deliver: points = the presents x a height
//     bonus (x1 for 1-2, x2 for 3-4, x3 for 5-6, x4 for 7-8, x5 for 9-10). Then Santa moves the sleigh
//     to the other side of the street.
//   * Short timed rounds; anything still in your arms when time runs out is lost.
//
// Records: the best score per level (sb_save extra records CATCH_RECORD_SLOT + level, see
// docs/decisions/0002-record-slots.md). Art: tools/art/make_catch.py -> graphics/catch/. Music: catch_song.
#include "bn_bg_palettes.h"
#include "bn_color.h"
#include "bn_fixed.h"
#include "bn_keypad.h"
#include "bn_log.h"
#include "bn_math.h"
#include "bn_regular_bg_ptr.h"
#include "bn_sprite_ptr.h"
#include "bn_string.h"
#include "bn_string_view.h"
#include "bn_vector.h"

#include "bn_music_items.h"
#include "bn_regular_bg_items_catch_bg.h"
#include "bn_sound_items.h"
#include "bn_sprite_items_catch_items.h"
#include "bn_sprite_items_catch_parked.h"
#include "bn_sprite_items_catch_sleigh.h"

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
    // ============================================================================ tuning
    struct level_def
    {
        const char* name;
        int seconds;                // length of a round
        bn::fixed fall_start;       // fall speed at the start (px/frame) ...
        bn::fixed fall_end;         // ... and at the end of the round
        int spawn_start, spawn_end; // frames between drops at the start / end
        int coal_percent;           // chance a drop is coal
        int wobble_percent;         // how hard moving pushes the stack (100 = normal)
    };

    constexpr level_def LEVELS[3] = {
        //  name      time  fall           spawn     coal wobble
        { "Easy",     75,   0.6,  1.1,     70, 46,   8,   90 },
        { "Medium",   60,   0.75, 1.4,     60, 38,   14,  110 },
        { "Hard",     60,   0.9,  1.7,     52, 32,   20,  130 },
    };
    constexpr int LEVEL_COUNT = 3;

    // Player
    constexpr bn::fixed WALK_SPEED = 1.6;             // top speed (px/frame)
    constexpr bn::fixed WALK_ACCEL = 0.15;            // speed gained / lost per frame
    constexpr bn::fixed TIPTOE_SPEED = 0.8;           // holding B
    constexpr bn::fixed TIPTOE_ACCEL = 0.08;
    constexpr int PLAYER_Y = 58;                      // centre of the 16x32 character sprite (feet at ~74)
    constexpr int PLAYER_LIMIT_X = 108;

    // The stack. Lean is how far (px) the TOP of the stack is off to the side; the presents below lean
    // less. Every frame: moving pushes the lean, a spring pulls it back, tall stacks tip (the taller, the
    // weaker the spring), and it topples past TOPPLE_LEAN. All the small factors are per mille (1/1000)
    // and applied with int maths, because bn::fixed * bn::fixed drops everything below 1/64.
    constexpr int MAX_STACK = 10;
    constexpr int SPRING_PERMILLE = 80;               // pull back towards straight ...
    constexpr int TIP_PERMILLE_PER_GIFT = 8;          // ... minus this for every present (tall = tippy)
    constexpr int PUSH_PERMILLE = 150;                // a change of speed pushes the lean by this ...
    constexpr int PUSH_PERMILLE_PER_GIFT = 60;        // ... plus this for every present
    constexpr int DAMP_PERMILLE = 50;                 // the wobble dies down this much per frame
    constexpr int CATCH_PUSH_DIVISOR = 14;            // catching off-centre: lean speed += offset / this
    constexpr bn::fixed COAL_KICK = 1.6;              // lean speed added by a lump of coal
    constexpr int TOPPLE_LEAN = 16;
    constexpr int WARN_LEAN = 9;                      // the stack flashes past this lean
    constexpr int CATCH_HALF_WIDTH = 11;              // a present is caught this close to the top of the stack
    constexpr int GIFT_STEP_SMALL = 8;                // height a small present adds to the stack (px)
    constexpr int GIFT_STEP_BIG = 11;                 // ... a big one or a star

    // Scoring
    constexpr int POINTS_SMALL = 10;
    constexpr int POINTS_BIG = 25;
    constexpr int POINTS_STAR = 50;
    constexpr int STAR_CHANCE = 6;                    // % chance a drop is a golden star present
    constexpr int BIG_CHANCE = 24;                    // % chance of a big present

    // Santa's flying sleigh
    constexpr int SLEIGH_Y = -46;
    constexpr bn::fixed SLEIGH_SPEED = 1;
    constexpr int SLEIGH_TURN_X = 160;
    constexpr int SLEIGH_DROP_LIMIT_X = 104;
    constexpr int DROP_SPREAD = 22;

    // The parked sleigh (deliveries)
    constexpr int PARKED_X = 100;                     // it parks at -PARKED_X or +PARKED_X
    constexpr int PARKED_Y = 58;
    constexpr int DELIVER_REACH = 22;                 // walk this close to it to deliver
    constexpr int DELIVER_FRAMES_PER_GIFT = 5;        // presents fly into the sack one by one

    // Layout
    constexpr int GROUND_Y = 66;
    constexpr int LANDED_FRAMES = 40;
    constexpr int ITEM_COUNT = 10;
    constexpr int STACK_BASE_Y = 39;                  // centre of the bottom present (held over the head)

    // Depth: only a few z values (Butano has 16 sprite sort layers in total).
    constexpr int Z_FRONT = -2;
    constexpr int Z_STACK = -1;
    constexpr int Z_PLAYER = 0;
    constexpr int Z_BACK = 2;

    // Records: sb_save extra record slots CATCH_RECORD_SLOT .. +2 (Easy, Medium, Hard).
    constexpr int CATCH_RECORD_SLOT = 12;

    // ============================================================================ data
    enum class kind
    {
        SMALL, BIG, STAR, COAL
    };

    enum class item_state
    {
        FREE, FALLING, LANDED, TUMBLING
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
        bn::fixed vx;
    };

    struct gift
    {
        bn::sprite_ptr sprite;
        kind type = kind::SMALL;
        int frame = 0;
    };

    constexpr int ITEM_FRAME_SMALL = 0;   // 3 colours: 0..2
    constexpr int ITEM_FRAME_BIG = 3;     // 2 colours: 3..4
    constexpr int ITEM_FRAME_STAR = 5;    // 2 sparkle frames: 5..6
    constexpr int ITEM_FRAME_COAL = 7;    // 2 frames: 7..8

    int points_of(kind type)
    {
        return type == kind::STAR ? POINTS_STAR : type == kind::BIG ? POINTS_BIG : POINTS_SMALL;
    }

    int height_bonus(int stack)
    {
        return stack >= 9 ? 5 : stack >= 7 ? 4 : stack >= 5 ? 3 : stack >= 3 ? 2 : 1;
    }

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
            _parked(bn::sprite_items::catch_parked.create_sprite(PARKED_X, PARKED_Y)),
            _player(sb::character_sprite(sb::player_character()).create_sprite(0, PLAYER_Y, sb::FRAME_CHEER)),
            _snow(14, 2)
        {
            _bg.set_priority(3);
            _sleigh.set_bg_priority(1);
            _sleigh.set_z_order(Z_BACK);
            _parked.set_bg_priority(1);
            _parked.set_z_order(Z_BACK);
            _player.set_bg_priority(1);
            _player.set_z_order(Z_PLAYER);

            for(int i = 0; i < ITEM_COUNT; ++i)
            {
                item it { bn::sprite_items::catch_items.create_sprite(0, 0), item_state::FREE, kind::SMALL, 0, 0, 0,
                          0, 0, 0 };
                it.sprite.set_bg_priority(1);
                it.sprite.set_z_order(Z_BACK);
                it.sprite.set_visible(false);
                _items.push_back(it);
            }

            reset(0);
        }

        void reset(int level)
        {
            _level = level;

            for(item& it : _items)
            {
                it.state = item_state::FREE;
                it.sprite.set_visible(false);
            }

            _stack.clear();
            _score = 0;
            _delivered = 0;
            _deliveries = 0;
            _best_stack = 0;
            _toppled = 0;
            _frames_left = def().seconds * 60;
            _play_frames = 0;
            _spawn_timer = 50;
            _shake_timer = 0;
            _player_x = 0;
            _vx = 0;
            _lean = 0;
            _lean_v = 0;
            _facing_left = false;
            _walk_timer = 0;
            _sleigh_x = -SLEIGH_TURN_X + 20;
            _sleigh_dir = 1;
            _parked_side = 1;
            _delivering = 0;
            _deliver_points = 0;
            _shown_score = -1;
            _shown_seconds = -1;
            _shown_stack = -1;
            _popup_timer = 0;
            _hint_timer = 0;
            _popup.clear();
            _hint.clear();
            _prompt.clear();
            _shown_prompt = false;
            _bg.set_position(0, 0);
            _player.set_visible(true);
            place_player(false);
            place_sleigh();
            place_parked();
        }

        // Shows the scene in its idle pose (used behind the intro box).
        void show_idle()
        {
            _sleigh_x = 30;
            place_sleigh();
            place_player(false);
        }

        // One frame of play. Returns false when time is up (and any delivery has finished).
        bool update()
        {
            ++_play_frames;

            if(_frames_left > 0)
            {
                --_frames_left;
                int seconds = (_frames_left + 59) / 60;

                if(_frames_left % 60 == 0 && seconds > 0 && seconds <= 5)
                {
                    sb::sfx(bn::sound_items::sfx_countdown);
                }

                if(_frames_left == 10 * 60 && ! _stack.empty())
                {
                    hint("10 seconds! Deliver your stack!", sb::text_color::YELLOW);
                }
            }

            update_player();
            update_sleigh();
            update_spawning();
            update_items();
            update_stack();
            update_delivery();
            update_effects();
            update_hud();
            _snow.update();

            if(_frames_left <= 0 && _delivering == 0)
            {
                if(! _stack.empty())
                {
                    topple(false);   // time's up: whatever is still in your arms is lost
                }

                return false;
            }

            return true;
        }

        // A short pause at the end so you can see what happened.
        void end_pause()
        {
            for(int t = 0; t < 50; ++t)
            {
                update_sleigh();
                update_items();
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

        int level() const
        {
            return _level;
        }

        int delivered() const
        {
            return _delivered;
        }

        int deliveries() const
        {
            return _deliveries;
        }

        int best_stack() const
        {
            return _best_stack;
        }

        int toppled() const
        {
            return _toppled;
        }

        void set_hud_visible(bool visible)
        {
            _score_label.set_visible(visible);
            _time_label.set_visible(visible);
            _stack_label.set_visible(visible);
            _popup.set_visible(visible);
            _hint.set_visible(visible);
            _prompt.set_visible(visible);
        }

        void clear_hud()
        {
            _score_label.clear();
            _time_label.clear();
            _stack_label.clear();
            _popup.clear();
            _hint.clear();
            _prompt.clear();
            _shown_prompt = false;
        }

    private:
        bn::regular_bg_ptr _bg;
        bn::sprite_ptr _sleigh;
        bn::sprite_ptr _parked;
        bn::sprite_ptr _player;
        sb::snowfall _snow;
        bn::vector<item, ITEM_COUNT> _items;
        bn::vector<gift, MAX_STACK> _stack;
        sb::label _score_label, _time_label, _stack_label, _popup, _hint, _prompt;
        bool _shown_prompt = false;

        int _level = 0;
        int _score = 0;
        int _delivered = 0;           // presents delivered
        int _deliveries = 0;
        int _best_stack = 0;
        int _toppled = 0;
        int _frames_left = 0;
        int _play_frames = 0;
        int _spawn_timer = 0;
        int _shake_timer = 0;
        bn::fixed _player_x;
        bn::fixed _vx;
        bn::fixed _lean;              // px the top of the stack is off to the side (+ = right)
        bn::fixed _lean_v;
        bool _facing_left = false;
        int _walk_timer = 0;
        bn::fixed _sleigh_x;
        int _sleigh_dir = 1;
        int _parked_side = 1;
        int _delivering = 0;          // frames left of the presents flying into the sack
        int _deliver_points = 0;
        int _shown_score = -1;
        int _shown_seconds = -1;
        int _shown_stack = -1;
        int _popup_timer = 0;
        int _hint_timer = 0;

        const level_def& def() const
        {
            return LEVELS[_level];
        }

        // 0 at the start of the round, 1 at the end.
        bn::fixed progress() const
        {
            int total = def().seconds * 60;
            return bn::fixed(bn::min(_play_frames, total)) / total;
        }

        bn::fixed fall_speed() const
        {
            return def().fall_start + (def().fall_end - def().fall_start) * progress();
        }

        int spawn_interval() const
        {
            return def().spawn_start + ((def().spawn_end - def().spawn_start) * progress()).integer();
        }

        // ---------------------------------------------------------------- input
#ifdef CATCH_AUTOPILOT
        // Test builds only (make USERFLAGS=-DCATCH_AUTOPILOT=N): walks under the lowest falling present,
        // balances the stack (steps under the lean) and delivers once it holds N presents.
        int autopilot_direction() const
        {
            bn::fixed top_x = _player_x + _lean;
            int lean = _lean.integer();
            int lean_v = (_lean_v * 6).integer();

            if(bn::abs(lean + lean_v) > 3 && ! _stack.empty())
            {
                return lean + lean_v > 0 ? 1 : -1;
            }

            if(_stack.size() >= CATCH_AUTOPILOT || (_frames_left < 8 * 60 && ! _stack.empty()))
            {
                return _parked_side * PARKED_X > _player_x ? 1 : -1;
            }

            const item* target = nullptr;

            for(const item& it : _items)
            {
                if(it.state == item_state::FALLING && it.type != kind::COAL && (! target || it.y > target->y))
                {
                    target = &it;
                }
            }

            if(! target)
            {
                return 0;
            }

            bn::fixed dx = target->x - top_x;
            return dx > 3 ? 1 : dx < -3 ? -1 : 0;
        }
#endif

        bool deliver_pressed() const
        {
#ifdef CATCH_AUTOPILOT
            return _stack.size() >= CATCH_AUTOPILOT || _frames_left < 8 * 60;
#else
            return bn::keypad::a_pressed();
#endif
        }

        int input_direction() const
        {
#ifdef CATCH_AUTOPILOT
            return autopilot_direction();
#else
            return bn::keypad::left_held() ? -1 : bn::keypad::right_held() ? 1 : 0;
#endif
        }

        // ---------------------------------------------------------------- player
        void update_player()
        {
            int dir = _frames_left > 0 && _delivering == 0 ? input_direction() : 0;
            bool tiptoe = bn::keypad::b_held();
            bn::fixed top = tiptoe ? TIPTOE_SPEED : WALK_SPEED;
            bn::fixed accel = tiptoe ? TIPTOE_ACCEL : WALK_ACCEL;
            bn::fixed target = top * dir;
            bn::fixed old_vx = _vx;

            if(_vx < target)
            {
                _vx = bn::min(target, _vx + accel);
            }
            else if(_vx > target)
            {
                _vx = bn::max(target, _vx - accel);
            }

            _player_x += _vx;

            if(_player_x < -PLAYER_LIMIT_X || _player_x > PLAYER_LIMIT_X)
            {
                _player_x = clamp_fixed(_player_x, -PLAYER_LIMIT_X, PLAYER_LIMIT_X);
                _vx = 0;
            }

            // A change of speed pushes the stack the other way (the presents want to stay where they were).
            bn::fixed accel_now = _vx - old_vx;
            int n = _stack.size();

            if(n > 0)
            {
                int push = (PUSH_PERMILLE + PUSH_PERMILLE_PER_GIFT * n) * def().wobble_percent / 100;
                _lean_v -= accel_now * push / 1000;
            }

            if(dir != 0)
            {
                _facing_left = dir < 0;
                _walk_timer += tiptoe ? 1 : 2;
            }
            else
            {
                _walk_timer = 0;
            }

            place_player(dir != 0);
        }

        void place_player(bool moving)
        {
            int frame = _stack.empty() ? sb::FRAME_DOWN : sb::FRAME_CHEER;

            if(moving && _stack.empty())
            {
                int step = (_walk_timer / 10) % 4;
                frame = step == 1 ? sb::FRAME_SIDE_WALK_A : step == 3 ? sb::FRAME_SIDE_WALK_B : sb::FRAME_SIDE;
            }

            int bob = moving && (_walk_timer / 10) % 2 ? 1 : 0;
            _player.set_tiles(sb::character_sprite(sb::player_character()).tiles_item(), frame);
            _player.set_horizontal_flip(moving && _stack.empty() && _facing_left);
            _player.set_position(_player_x, PLAYER_Y + bob);
        }

        // ---------------------------------------------------------------- the stack
        // Height of the stack's top above the bottom present's centre.
        int stack_height() const
        {
            int h = 0;

            for(int i = 0; i + 1 < _stack.size(); ++i)
            {
                h += _stack[i].type == kind::SMALL ? GIFT_STEP_SMALL : GIFT_STEP_BIG;
            }

            return h;
        }

        // Where the top of the stack is (the catch point): x and the y of the top present's lid.
        bn::fixed top_x() const
        {
            return _player_x + _lean;
        }

        int top_y() const
        {
            if(_stack.empty())
            {
                return STACK_BASE_Y + 2;
            }

            const gift& top = _stack.back();
            return STACK_BASE_Y - stack_height() - (top.type == kind::SMALL ? 2 : 5);
        }

        void update_stack()
        {
            int n = _stack.size();

            if(n == 0)
            {
                _lean = 0;
                _lean_v = 0;
                return;
            }

            // Spring back, tip over (tall stacks), and damping. Int maths for the small factors.
            int spring = SPRING_PERMILLE - TIP_PERMILLE_PER_GIFT * n;
            _lean_v -= _lean * spring / 1000;
            _lean_v -= _lean_v * DAMP_PERMILLE / 1000;
            _lean += _lean_v;

            if(bn::abs(_lean) > TOPPLE_LEAN)
            {
                topple(true);
                return;
            }

            // Place the presents: each one leans more the higher it is (a gentle curve).
            int y = STACK_BASE_Y;
            bool warn = bn::abs(_lean) > WARN_LEAN && (_play_frames / 4) % 2 == 0;

            for(int i = 0; i < n; ++i)
            {
                gift& g = _stack[i];
                int rel = (i + 1) * 1000 / n;                          // 0..1000 up the stack
                int bend = rel * rel / 1000;                           // curve
                bn::fixed x = _player_x + _lean * bend / 1000;
                g.sprite.set_position(x, y);
                g.sprite.set_visible(! warn || i < n - 2);

                if(g.type == kind::STAR)
                {
                    g.sprite.set_tiles(bn::sprite_items::catch_items.tiles_item(),
                                       ITEM_FRAME_STAR + (_play_frames / 8) % 2);
                }

                y -= g.type == kind::SMALL ? GIFT_STEP_SMALL : GIFT_STEP_BIG;
            }
        }

        void add_to_stack(const item& it)
        {
            gift g { bn::sprite_items::catch_items.create_sprite(it.x, it.y, it.frame), it.type, it.frame };
            g.sprite.set_bg_priority(1);
            g.sprite.set_z_order(Z_STACK);
            _stack.push_back(g);
            _best_stack = bn::max(_best_stack, int(_stack.size()));

            // Catching off-centre gives the stack a push.
            _lean_v += (it.x - top_x()) / CATCH_PUSH_DIVISOR;

            int n = _stack.size();
            sb::sfx(it.type == kind::STAR ? bn::sound_items::sfx_point : bn::sound_items::sfx_catch);

            if(n == 3 || n == 5 || n == 7 || n == 9)
            {
                bn::string<24> text("x");
                text.append(sb::to_text(height_bonus(n)));
                text.append(" stack!");
                show_popup(_player_x, top_y() - 12, text, sb::text_color::YELLOW);
            }
            else if(n == MAX_STACK)
            {
                show_popup(_player_x, top_y() - 12, "Full! Deliver!", sb::text_color::YELLOW);
            }
        }

        // The stack falls (toppled = it leaned too far; otherwise time ran out).
        void topple(bool toppled)
        {
            int dir = _lean >= 0 ? 1 : -1;

            for(int i = 0; i < _stack.size(); ++i)
            {
                gift& g = _stack[i];

                // Hand the sprite to a free item so it can tumble to the ground.
                for(item& it : _items)
                {
                    if(it.state == item_state::FREE)
                    {
                        it.state = item_state::TUMBLING;
                        it.type = g.type;
                        it.frame = g.frame;
                        it.timer = 0;
                        it.x = g.sprite.x();
                        it.y = g.sprite.y();
                        it.vx = dir * (bn::fixed(1) / 2 + bn::fixed(i) / 4);
                        it.speed = -1 - bn::fixed(sb::rand_range(0, 4)) / 4;
                        it.sprite.set_tiles(bn::sprite_items::catch_items.tiles_item(), it.frame);
                        it.sprite.set_position(it.x, it.y);
                        it.sprite.set_visible(true);
                        break;
                    }
                }
            }

            if(toppled)
            {
                BN_LOG("Gift Tower: a stack of ", _stack.size(), " toppled at frame ", _play_frames);
                ++_toppled;
                sb::sfx(bn::sound_items::sfx_crash);
                show_popup(_player_x + dir * 20, STACK_BASE_Y - 20, "Toppled!", sb::text_color::RED);
                _shake_timer = 14;
            }

            _stack.clear();
            _lean = 0;
            _lean_v = 0;
        }

        // ---------------------------------------------------------------- delivering
        void place_parked()
        {
            _parked.set_position(_parked_side * PARKED_X, PARKED_Y);
            _parked.set_horizontal_flip(_parked_side < 0);
        }

        void update_delivery()
        {
            if(_delivering > 0)
            {
                --_delivering;

                // The presents fly from the top of the stack into the sack, one by one.
                if(_delivering % DELIVER_FRAMES_PER_GIFT == 0 && ! _stack.empty())
                {
                    _stack.pop_back();
                    sb::sfx(bn::sound_items::sfx_select);
                }

                _parked.set_tiles(bn::sprite_items::catch_parked.tiles_item(), (_delivering / 4) % 2);

                if(_delivering == 0)
                {
                    _score += _deliver_points;
                    sb::sfx(bn::sound_items::sfx_record);
                    _parked.set_tiles(bn::sprite_items::catch_parked.tiles_item(), 0);

                    // Santa moves the sleigh to the other side of the street.
                    _parked_side = -_parked_side;
                    place_parked();
                }

                return;
            }

            if(_stack.empty() || _frames_left <= 0)
            {
                return;
            }

            bool in_reach = bn::abs(_player_x - _parked_side * PARKED_X) <= DELIVER_REACH;

            if(in_reach != _shown_prompt)
            {
                _shown_prompt = in_reach;

                if(in_reach)
                {
                    _prompt.set(_parked_side * (PARKED_X - 6), PARKED_Y - 30, "A: Deliver", sb::text_color::YELLOW,
                                sb::align::CENTER);
                }
                else
                {
                    _prompt.clear();
                }
            }

            if(in_reach && deliver_pressed())
            {
                _prompt.clear();
                _shown_prompt = false;
                int base = 0;

                for(const gift& g : _stack)
                {
                    base += points_of(g.type);
                }

                int n = _stack.size();
                _deliver_points = base * height_bonus(n);
                _delivered += n;
                ++_deliveries;
                _delivering = n * DELIVER_FRAMES_PER_GIFT + 1;
                _vx = 0;

                bn::string<24> text("+");
                text.append(sb::to_text(_deliver_points));

                if(height_bonus(n) > 1)
                {
                    text.append(" (x");
                    text.append(sb::to_text(height_bonus(n)));
                    text.append(")");
                }

                show_popup(_parked_side * (PARKED_X - 24), PARKED_Y - 34, text, sb::text_color::YELLOW);
            }
        }

        // ---------------------------------------------------------------- sleigh
        void update_sleigh()
        {
            bn::fixed sx = sack_x();
            bool can_drop = sx >= -SLEIGH_DROP_LIMIT_X && sx <= SLEIGH_DROP_LIMIT_X;
            _sleigh_x += SLEIGH_SPEED * _sleigh_dir * (can_drop ? 1 : 3);

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

        bn::fixed sack_x() const
        {
            return _sleigh_x - 20 * _sleigh_dir;
        }

        // ---------------------------------------------------------------- items
        void update_spawning()
        {
            if(_frames_left <= 0 || --_spawn_timer > 0)
            {
                return;
            }

            bn::fixed sx = sack_x();

            if(sx < -SLEIGH_DROP_LIMIT_X || sx > SLEIGH_DROP_LIMIT_X)
            {
                return;   // wait until the sleigh is back over the street
            }

            spawn(sx);
            _spawn_timer = spawn_interval() + sb::rand_range(-10, 10);
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
            int coal = _play_frames < 6 * 60 ? 0 : def().coal_percent;

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
            it.x = clamp_fixed(sx + sb::rand_range(-DROP_SPREAD, DROP_SPREAD), -110, 110);
            it.y = SLEIGH_Y + 6;
            it.speed = fall_speed() * (100 + sb::rand_range(0, 20)) / 100;
            it.vx = 0;
            it.sprite.set_tiles(bn::sprite_items::catch_items.tiles_item(), it.frame);
            it.sprite.set_position(it.x, it.y);
            it.sprite.set_z_order(Z_BACK);
            it.sprite.set_visible(true);
        }

        void update_items()
        {
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

                if(it.state == item_state::TUMBLING)
                {
                    // A present from a toppled stack: flies off and bounces into the snow.
                    it.x += it.vx;
                    it.y += it.speed;
                    it.speed += bn::fixed(1) / 8;

                    if(it.y >= GROUND_Y)
                    {
                        it.y = GROUND_Y;
                        it.state = item_state::LANDED;
                        it.timer = 0;
                    }

                    it.sprite.set_position(it.x, it.y);
                    continue;
                }

                it.y += it.speed;

                if(it.type == kind::STAR)
                {
                    it.sprite.set_tiles(bn::sprite_items::catch_items.tiles_item(), ITEM_FRAME_STAR + (it.timer / 8) % 2);
                }
                else if(it.type == kind::COAL)
                {
                    it.sprite.set_tiles(bn::sprite_items::catch_items.tiles_item(), ITEM_FRAME_COAL + (it.timer / 20) % 2);
                }

                // Caught on top of the stack (or on your head when you hold nothing)?
                bn::fixed dx = it.x - top_x();
                int ty = top_y();

                if(_delivering == 0 && it.y + 6 >= ty && it.y + 6 <= ty + 8 && dx >= -CATCH_HALF_WIDTH &&
                   dx <= CATCH_HALF_WIDTH)
                {
                    if(it.type == kind::COAL)
                    {
                        hit_by_coal(it);
                    }
                    else if(_stack.full())
                    {
                        // No room: it bounces off.
                        it.state = item_state::TUMBLING;
                        it.vx = dx >= 0 ? 1 : -1;
                        it.speed = -1;
                        sb::sfx(bn::sound_items::sfx_bump);
                    }
                    else
                    {
                        it.state = item_state::FREE;
                        it.sprite.set_visible(false);
                        add_to_stack(it);
                    }

                    continue;
                }

                if(it.y >= GROUND_Y)
                {
                    it.y = GROUND_Y;
                    it.state = item_state::LANDED;
                    it.timer = 0;
                }

                it.sprite.set_position(it.x, it.y);
            }
        }

        void hit_by_coal(item& it)
        {
            sb::sfx(bn::sound_items::sfx_coal);
            it.state = item_state::TUMBLING;
            it.vx = it.x >= top_x() ? 1 : -1;
            it.speed = -1;
            _shake_timer = 10;

            if(! _stack.empty())
            {
                // Knocks the stack away from where it hit.
                _lean_v += it.x >= top_x() ? -COAL_KICK : COAL_KICK;
                show_popup(it.x, top_y() - 14, "Coal!", sb::text_color::RED);
            }
        }

        // ---------------------------------------------------------------- effects and HUD
        void show_popup(bn::fixed x, int y, const bn::string_view& text, sb::text_color color)
        {
            _popup.set(clamp_fixed(x, -80, 80).integer(), y, text, color, sb::align::CENTER);
            _popup_timer = 60;
        }

        void hint(const bn::string_view& text, sb::text_color color)
        {
            _hint.set(0, -56, text, color, sb::align::CENTER);
            _hint_timer = 120;
        }

        void update_effects()
        {
            if(_popup_timer > 0 && --_popup_timer == 0)
            {
                _popup.clear();
            }

            if(_hint_timer > 0 && --_hint_timer == 0)
            {
                _hint.clear();
            }

            if(_shake_timer > 0)
            {
                --_shake_timer;
                _bg.set_position(_shake_timer == 0 ? 0 : sb::rand_range(-2, 2), _shake_timer == 0 ? 0 : sb::rand_range(-2, 2));
            }
        }

        void update_hud()
        {
            if(_shown_score != _score)
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
                _time_label.set(114, -72, text, seconds <= 10 ? sb::text_color::YELLOW : sb::text_color::WHITE,
                                sb::align::RIGHT);
            }

            // The stack's worth if you deliver it now.
            int n = _stack.size();

            if(n != _shown_stack)
            {
                _shown_stack = n;

                if(n == 0)
                {
                    _stack_label.clear();
                }
                else
                {
                    int base = 0;

                    for(const gift& g : _stack)
                    {
                        base += points_of(g.type);
                    }

                    bn::string<32> text("Stack ");
                    text.append(sb::to_text(n));
                    text.append(": ");
                    text.append(sb::to_text(base * height_bonus(n)));
                    _stack_label.set(0, -72, text, n >= 7 ? sb::text_color::YELLOW : sb::text_color::WHITE,
                                     sb::align::CENTER);
                }
            }
        }
    };

    int g_level_choice = 0;

    bool choose_level()
    {
        sb::label title;
        title.set(0, -60, "How busy is Santa?", sb::text_color::WHITE, sb::align::CENTER);
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

    // The result box: score, deliveries, and the best score at every level. Returns true to play again.
    bool show_results(const catch_game& game)
    {
        bool record = game.score() > 0 &&
                      sb::submit_extra(CATCH_RECORD_SLOT + game.level(), game.score(), sb::player_character());
        sb::play_jingle(record ? bn::music_items::jingle_record : bn::music_items::jingle_over);

        // Everything fits in the box (rows 0..13) so the menu below (rows 14..19) doesn't cover it.
        sb::panel(3, 0, 24, 14);
        sb::label title, result, detail1, detail2, heading, lines[LEVEL_COUNT], record_label;
        title.set(0, -71, "Time's up!", sb::text_color::BLUE, sb::align::CENTER);

        bn::string<40> text(LEVELS[game.level()].name);
        text.append(": ");
        text.append(sb::to_text(game.score()));
        text.append(" points");
        result.set(0, -59, text, sb::text_color::DARK, sb::align::CENTER);

        bn::string<40> d1(sb::to_text(game.delivered()));
        d1.append(" presents in ");
        d1.append(sb::to_text(game.deliveries()));
        d1.append(game.deliveries() == 1 ? " trip" : " trips");
        detail1.set(0, -48, d1, sb::text_color::DARK, sb::align::CENTER);

        bn::string<40> d2("Tallest stack ");
        d2.append(sb::to_text(game.best_stack()));
        d2.append("  Toppled ");
        d2.append(sb::to_text(game.toppled()));
        detail2.set(0, -37, d2, sb::text_color::DARK, sb::align::CENTER);
        heading.set(0, -24, "{ Best scores {", sb::text_color::RED, sb::align::CENTER);

        for(int l = 0; l < LEVEL_COUNT; ++l)
        {
            bn::string<40> best(LEVELS[l].name);
            best.append(":  ");
            int value = sb::extra_score(CATCH_RECORD_SLOT + l);

            if(value > 0)
            {
                best.append(sb::to_text(value));
                best.append("  ");
                best.append(sb::extra_name(CATCH_RECORD_SLOT + l));
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

// The best record shown on the map, the intro and the high score list (sb_games.h: game_info::best_text):
// the hardest level with a score, e.g. "Hard 860 by Adam".
bn::string<40> sb::catch_best_text(bool compact)
{
    static constexpr const char* names[] = { "Easy", "Medium", "Hard" };
    return level_record_text(CATCH_RECORD_SLOT, LEVEL_COUNT, names, record_value_text, compact);
}

void sb::catch_run()
{
    // Shown on the start screen (scroll with Up/Down) and in the pause menu. '#' = heading.
    static constexpr bn::string_view howto[] = {
        "#Build a tower",
        "< > walk. Catch Santa's",
        "presents on top of the",
        "stack in your arms.",
        "Gift 10  Big 25  { 50",
        "",
        "#Deliver",
        "Press A at the parked",
        "sleigh to cash in your",
        "stack. Taller = more:",
        "3 presents x2, 5 x3,",
        "7 x4, 9 or 10 x5!",
        "Then Santa moves the",
        "sleigh across the street.",
        "",
        "#Don't let it fall",
        "Starting, stopping and",
        "turning make the stack",
        "wobble. Tall stacks tip:",
        "step under the lean to",
        "save it. Hold B to walk",
        "gently. Coal knocks it!",
        "If it falls, it's lost.",
        "",
        "#Time",
        "Deliver before time runs",
        "out: what's still in your",
        "arms then is lost.",
    };

    bn::bg_palettes::set_transparent_color(bn::color(4, 5, 12));

    {
        catch_game g;
        g.show_idle();
        play_music(bn::music_items::catch_song);
        fade_in();

        if(game_intro(GAME_CATCH, howto))
        {
            while(choose_level())
            {
                g.reset(g_level_choice);
                bool quit = false;

                while(true)
                {
                    bool playing = g.update();
                    sb::update();

                    if(bn::keypad::start_pressed() && playing)
                    {
                        g.set_hud_visible(false);
                        bool leave = game_pause();
                        g.set_hud_visible(true);

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

                BN_LOG("Gift Tower level ", g.level(), ": score ", g.score(), ", delivered ", g.delivered(), " in ",
                       g.deliveries(), " trips, tallest ", g.best_stack(), ", toppled ", g.toppled());
                g.end_pause();
                g.clear_hud();

                if(! show_results(g))
                {
                    break;
                }

                play_music(bn::music_items::catch_song);
            }
        }

        fade_out();
        g.clear_hud();
    }

    panel_clear();
}
