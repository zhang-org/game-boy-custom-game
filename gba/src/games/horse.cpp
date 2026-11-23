// Pony Gallop: a side-scrolling horse race across the snowy farm.
//
// The player rides a chestnut horse along the front track and jumps fences, hay bales and snow
// drifts with A (hold A for a higher, longer jump). A CPU rival (another family member on a grey
// horse) races along the back lane. Hitting an obstacle costs one of 3 hearts and slows you down;
// clean jumps give a little burst of speed, which is how you pull ahead of the rival.
// Score: distance in metres.
#include "bn_bg_palettes.h"
#include "bn_color.h"
#include "bn_fixed.h"
#include "bn_keypad.h"
#include "bn_math.h"
#include "bn_optional.h"
#include "bn_regular_bg_ptr.h"
#include "bn_sprite_ptr.h"
#include "bn_sprite_text_generator.h"
#include "bn_string.h"
#include "bn_string_view.h"
#include "bn_vector.h"

#ifdef HORSE_AUTOPILOT
    #include "bn_log.h"
#endif

#include "bn_music_items.h"
#include "bn_regular_bg_items_horse_far.h"
#include "bn_regular_bg_items_horse_mid.h"
#include "bn_regular_bg_items_horse_near.h"
#include "bn_sound_items.h"
#include "bn_sprite_items_horse_drift.h"
#include "bn_sprite_items_horse_horse.h"
#include "bn_sprite_items_horse_obstacles.h"
#include "bn_sprite_items_horse_puff.h"
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
    // Speeds are in pixels per frame (60 frames per second).
    constexpr bn::fixed START_SPEED = 1.75;          // gallop speed at the start line
    constexpr bn::fixed MAX_SPEED = 3.5;             // top speed, reached after FULL_DIFFICULTY_M metres
    constexpr int FULL_DIFFICULTY_M = 1500;          // metres until top speed and the tightest obstacle gaps
    constexpr int PIXELS_PER_METRE = 8;              // world pixels per metre of score

    // Jump (heights in pixels, up is positive). Tested in tools: with these numbers every obstacle
    // has a timing window of at least ~7 frames for a tap and ~20 frames for a held jump.
    constexpr bn::fixed JUMP_SPEED = 2.8;            // upward speed when A is pressed
    constexpr bn::fixed GRAVITY = 0.14;              // pull back down every frame
    constexpr bn::fixed GRAVITY_HOLD = 0.06;         // lighter pull while A is held at the start of a jump
    constexpr int JUMP_HOLD_FRAMES = 12;             // how long holding A keeps the jump floaty
    constexpr int JUMP_BUFFER_FRAMES = 8;            // A pressed this soon before landing still jumps

    // Collisions.
    constexpr int HORSE_HIT_HALF_WIDTH = 10;         // half width of the horse's hitbox (legs only: forgiving)
    constexpr int HIT_TOLERANCE = 3;                 // hooves may dip this many px into an obstacle's top

    // Obstacle spacing, in frames of galloping between obstacles (converted to pixels with the
    // current speed so that faster = same reaction time). Lerped from START to END with difficulty.
    constexpr int GAP_MIN_START = 130;
    constexpr int GAP_MAX_START = 210;
    constexpr int GAP_MIN_END = 80;                  // must stay above the longest jump (53 frames)
    constexpr int GAP_MAX_END = 140;
    constexpr int FIRST_OBSTACLE_FRAMES = 150;       // breathing room after "GO!"
    constexpr int TALL_FENCES_FROM_M = 150;          // tall fences appear after this distance
    constexpr int DOUBLE_FENCES_FROM_M = 400;        // double fences appear after this distance
    constexpr int DOUBLE_FENCE_SPACING = 28;         // px between the two fences of a double

    // Hits.
    constexpr int START_HEARTS = 3;
    constexpr int STUMBLE_FRAMES = 45;               // slowed down for this long after a hit
    constexpr bn::fixed STUMBLE_SPEED_FACTOR = 0.4;  // speed multiplier right after a hit (recovers to 1)
    constexpr int INVULNERABLE_FRAMES = 90;          // blinking, can't be hit again

    // Rewards.
    constexpr bn::fixed CLEAR_BOOST = 0.35;          // extra speed for every clean jump
    constexpr bn::fixed MAX_BOOST = 0.9;             // boosts stack up to this
    constexpr bn::fixed BOOST_DECAY = 0.006;         // boost lost per frame
    constexpr int STREAK_CHEER = 5;                  // "Great riding!" every this many clean jumps

    // Rival.
    constexpr bn::fixed RIVAL_EDGE = 0.04;           // the rival is a tiny bit faster than your base speed
    constexpr bn::fixed RIVAL_WOBBLE = 0.18;         // the rival's pace drifts up and down by this much
    constexpr int RIVAL_WOBBLE_PERIOD = 600;         // frames for one slow pace cycle
    constexpr bn::fixed RIVAL_RUBBER = 0.0025;       // pulls the rival back towards you per px of lead

    // Animation.
    constexpr bn::fixed GALLOP_ANIM_RATE = 0.085;    // gallop poses per pixel travelled

    // ---- Layout (Butano coordinates: (0, 0) is the screen centre) -----------------------------
    constexpr int PLAYER_X = -64;                    // horse sprite centre x
    constexpr int GROUND_Y = 60;                     // player's hoof line
    constexpr int RIVAL_GROUND_Y = 24;               // rival's hoof line (back lane)
    constexpr int MARKER_GROUND_Y = 31;              // distance flags stand on the snow bank between lanes
    constexpr int RIDER_DX = -2;                     // rider sprite centre relative to the horse sprite centre
    constexpr int RIDER_DY = -21;
    constexpr int MARKER_EVERY_M = 100;

    // Horse sheet frames.
    enum horse_frame
    {
        HF_GALLOP = 0, HF_JUMP = 4, HF_STUMBLE = 5, HF_STAND = 6
    };

    constexpr int frame_bob[] = { -1, 0, 1, 0, -1, 2, 0 };

    // Obstacle kinds. Heights/widths are the hit area (px), frames index horse_obstacles/horse_drift.
    enum obstacle_kind
    {
        OK_LOW, OK_TALL, OK_HAY, OK_DRIFT
    };

    struct obstacle_type
    {
        int height;
        int width;
        int frame;
        int knocked_frame;
    };

    constexpr obstacle_type obstacle_types[] = {
        { 15, 12, 0, 2 },  // low fence
        { 23, 12, 1, 3 },  // tall fence
        { 12, 14, 4, 5 },  // hay bale
        { 9, 28, 0, 1 },   // snow drift (32x16 sheet)
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
        bool passed;
    };

    int lerp_int(int a, int b, bn::fixed t)
    {
        return a + ((b - a) * t).round_integer();
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
            _snow(14, 1)
        {
            bn::bg_palettes::set_transparent_color(bn::color(13, 21, 29));
            _far.set_priority(3);
            _far.set_z_order(2);
            _mid.set_priority(3);
            _mid.set_z_order(1);
            _near.set_priority(2);

            _horse.set_bg_priority(1);
            _horse.set_z_order(2);
            _rider.set_bg_priority(1);
            _rider.set_z_order(1);
            _rival_horse.set_palette(bn::sprite_palette_items::horse_rival_pal);
            _rival_horse.set_bg_priority(2);
            _rival_horse.set_z_order(2);
            _rival_rider.set_bg_priority(2);
            _rival_rider.set_z_order(1);

            // The rival is someone else from the family.
            int player = sb::player_character();
            _rival = sb::rand_range(0, sb::character_count - 2);

            if(_rival >= player)
            {
                ++_rival;
            }

            _rival_rider.set_item(sb::character_sprite(_rival), sb::FRAME_SIDE_SIT);
            reset();
        }

        // Puts everything back at the start line.
        void reset()
        {
            _obstacles.clear();
            _puffs.clear();
            _markers.clear();
            _distance = 0;
            _rival_x = 0;
            _speed = START_SPEED;
            _height = 0;
            _vy = 0;
            _airborne = false;
            _hold_frames = 0;
            _holding = false;
            _buffer = 0;
            _hearts = START_HEARTS;
            _stumble = 0;
            _invulnerable = 0;
            _boost = 0;
            _streak = 0;
            _anim = 0;
            _rival_anim = 0;
            _t = 0;
            _next_obstacle_x = (START_SPEED * FIRST_OBSTACLE_FRAMES).integer() + 140;
            _next_marker_m = MARKER_EVERY_M;
            _banner_timer = 0;
            _banner.clear();
            _shown_metres = -1;
            _shown_hearts = -1;
            _shown_rival_hint = 0;
            _rival_hint.clear();
            _horse_frame = HF_STAND;
            _rival_frame = HF_STAND;
            update_hud();
            draw();
        }

        // 3, 2, 1, GO! Returns false if the player paused and quit.
        void countdown()
        {
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
            sb::sfx(bn::sound_items::sfx_gallop);
            _banner_timer = 0;

            for(int i = 0; i < 30; ++i)
            {
                step();
                sb::update();

                if(_hearts == 0)
                {
                    break;
                }
            }
        }

        // One frame of the race. Returns false once the horse is out of hearts.
        bool step()
        {
            ++_t;
            int metres = this->metres();
            bn::fixed difficulty = bn::min(bn::fixed(metres) / FULL_DIFFICULTY_M, bn::fixed(1));
            bn::fixed nominal = START_SPEED + (MAX_SPEED - START_SPEED) * difficulty;

            // Player speed: nominal, slowed after a hit, plus the clean-jump boost.
            bn::fixed speed = nominal + _boost;

            if(_stumble > 0)
            {
                bn::fixed recovered = bn::fixed(STUMBLE_FRAMES - _stumble) / STUMBLE_FRAMES;
                speed = nominal * (STUMBLE_SPEED_FACTOR + (1 - STUMBLE_SPEED_FACTOR) * recovered);
                --_stumble;
            }

            _speed = speed;
            _boost = bn::max(_boost - BOOST_DECAY, bn::fixed(0));
            _distance += speed;

            // Rival pace: a slow wobble around your base speed, gently pulled back towards you.
            bn::fixed phase = bn::fixed(_t % RIVAL_WOBBLE_PERIOD) / RIVAL_WOBBLE_PERIOD;
            bn::fixed lead = _rival_x - _distance;
            bn::fixed rival_speed = nominal + RIVAL_EDGE + RIVAL_WOBBLE * bn::sin(phase) - lead * RIVAL_RUBBER;
            _rival_x += bn::max(rival_speed, bn::fixed(0.5));

            jump_physics();
            spawn(metres, difficulty, nominal);
            collide();
            update_markers();
            update_puffs();

            if(_invulnerable > 0)
            {
                --_invulnerable;
            }

            if(_banner_timer > 0 && --_banner_timer == 0)
            {
                _banner.clear();
            }

            // Animation.
            _anim += speed * GALLOP_ANIM_RATE;
            _rival_anim += bn::max(rival_speed, bn::fixed(0.5)) * GALLOP_ANIM_RATE;

            if(_anim >= 4)
            {
                _anim -= 4;
            }

            if(_rival_anim >= 4)
            {
                _rival_anim -= 4;
            }

            if(_stumble > STUMBLE_FRAMES - 18)
            {
                _horse_frame = HF_STUMBLE;
            }
            else if(_airborne)
            {
                _horse_frame = HF_JUMP;
            }
            else
            {
                _horse_frame = HF_GALLOP + _anim.integer();
            }

            _rival_frame = HF_GALLOP + _rival_anim.integer();
            _snow.update(-speed / 3, 0);
            update_hud();
            draw();
            return _hearts > 0;
        }

        // After the last heart: the horse slows to a stop while the rival gallops away.
        void finish()
        {
            sb::sfx(bn::sound_items::sfx_crash);
            bn::fixed speed = _speed;

            for(int i = 0; i < 80; ++i)
            {
                speed = bn::max(speed - bn::fixed(0.05), bn::fixed(0));
                _distance += speed;
                _rival_x += START_SPEED + (MAX_SPEED - START_SPEED) / 2;
                _rival_anim += bn::fixed(0.2);

                if(_rival_anim >= 4)
                {
                    _rival_anim -= 4;
                }

                _rival_frame = HF_GALLOP + _rival_anim.integer();

                if(_airborne)
                {
                    jump_physics();
                }

                _horse_frame = speed > bn::fixed(0.6) || _airborne ? HF_STUMBLE : HF_STAND;
                _invulnerable = 0;
                update_markers();
                update_puffs();
                _snow.update(-speed / 3, 0);
                update_hud();
                draw();
                sb::update();
            }

            _rival_hint.clear();
            _shown_rival_hint = 0;
            _banner.clear();
        }

        int metres() const
        {
            return _distance.integer() / PIXELS_PER_METRE;
        }

        // Who was ahead when the race ended.
        bn::string<32> result_text() const
        {
            bn::string<32> text;

            if(_finish_lead >= 0)
            {
                text.append("You beat ");
                text.append(sb::character_name(_rival));
                text.append("!");
            }
            else
            {
                text.append(sb::character_name(_rival));
                text.append(" was ahead!");
            }

            return text;
        }

        void remember_lead()
        {
            _finish_lead = (_distance - _rival_x).integer();
        }

    private:
        bn::regular_bg_ptr _far, _mid, _near;
        bn::sprite_ptr _horse, _rider, _rival_horse, _rival_rider;
        sb::snowfall _snow;
        bn::vector<obstacle, 12> _obstacles;
        bn::vector<puff, 6> _puffs;
        bn::vector<marker, 2> _markers;
        sb::label _distance_label, _hearts_label, _banner, _rival_hint;

        int _rival = 0;
        bn::fixed _distance;     // player's world x (pixels travelled)
        bn::fixed _rival_x;
        bn::fixed _speed;
        bn::fixed _height;       // hooves above the ground
        bn::fixed _vy;           // upward speed
        bool _airborne = false;
        bool _holding = false;
        int _hold_frames = 0;
        int _buffer = 0;
        int _hearts = START_HEARTS;
        int _stumble = 0;
        int _invulnerable = 0;
        bn::fixed _boost;
        int _streak = 0;
        bn::fixed _anim, _rival_anim;
        int _t = 0;
        int _next_obstacle_x = 0;
        int _next_marker_m = MARKER_EVERY_M;
        int _banner_timer = 0;
        int _shown_metres = -1;
        int _shown_hearts = -1;
        int _shown_rival_hint = 0;
        int _horse_frame = HF_STAND;
        int _rival_frame = HF_STAND;
        int _finish_lead = 0;

        int screen_x(int world_x) const
        {
            return PLAYER_X + world_x - _distance.integer();
        }

        void show_banner(const bn::string_view& text)
        {
            _banner.set(0, -40, text, sb::text_color::YELLOW, sb::align::CENTER);
            _banner_timer = 70;
        }

#ifdef HORSE_AUTOPILOT
        // Test build only (make USERFLAGS=-DHORSE_AUTOPILOT): jumps by itself, aiming the middle of
        // each jump at the middle of the next obstacle, to prove every obstacle can be cleared.
        bool _auto_hold = false;

        const obstacle* next_obstacle() const
        {
            const obstacle* best = nullptr;

            for(const obstacle& o : _obstacles)
            {
                if(! o.passed && o.x + obstacle_types[o.kind].width / 2 > _distance.integer() - HORSE_HIT_HALF_WIDTH &&
                   (! best || o.x < best->x))
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

            // Tall fences and doubles get a held jump (air ~53 frames), the rest a tap (~40 frames).
            bool tall = o->kind == OK_TALL;

            for(const obstacle& other : _obstacles)
            {
                if(&other != o && other.x > o->x && other.x - o->x <= DOUBLE_FENCE_SPACING)
                {
                    tall = true;
                }
            }

            int centre = o->x + (tall && o->kind == OK_LOW ? DOUBLE_FENCE_SPACING / 2 : 0);
            int frames_to_peak = tall ? 26 : 19;

            if(centre - _distance.integer() <= (_speed * frames_to_peak).integer())
            {
                _auto_hold = tall;
                return true;
            }

            return false;
        }

        bool a_held() const
        {
            return _auto_hold;
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
#endif

        void jump_physics()
        {
            if(a_pressed())
            {
                _buffer = JUMP_BUFFER_FRAMES;
            }
            else if(_buffer > 0)
            {
                --_buffer;
            }

            if(! _airborne)
            {
                if(_buffer > 0 && _hearts > 0)
                {
                    _buffer = 0;
                    _airborne = true;
                    _vy = JUMP_SPEED;
                    _hold_frames = 0;
                    _holding = a_held();
                    sb::sfx(bn::sound_items::sfx_jump);
                }

                return;
            }

            _height += _vy;

            if(_holding && a_held() && _hold_frames < JUMP_HOLD_FRAMES && _vy > 0)
            {
                _vy -= GRAVITY_HOLD;
                ++_hold_frames;
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
            _puffs.push_back({ sprite, world_x, 0 });
        }

        void add_obstacle(int world_x, int kind)
        {
            if(_obstacles.full())
            {
                return;
            }

            const obstacle_type& type = obstacle_types[kind];
            bn::sprite_ptr sprite = kind == OK_DRIFT ?
                        bn::sprite_items::horse_drift.create_sprite(screen_x(world_x), GROUND_Y - 7, type.frame) :
                        bn::sprite_items::horse_obstacles.create_sprite(screen_x(world_x), GROUND_Y - 15, type.frame);
            sprite.set_bg_priority(1);
            sprite.set_z_order(3);
            _obstacles.push_back({ sprite, world_x, kind, false, false });
        }

        void spawn(int metres, bn::fixed difficulty, bn::fixed nominal)
        {
            if(screen_x(_next_obstacle_x) > 150)
            {
                return;
            }

            int extra = 0;
            int roll = sb::rand_range(0, 99);

            if(metres >= DOUBLE_FENCES_FROM_M && roll < 18)
            {
                add_obstacle(_next_obstacle_x, OK_LOW);
                add_obstacle(_next_obstacle_x + DOUBLE_FENCE_SPACING, OK_LOW);
                extra = DOUBLE_FENCE_SPACING;
            }
            else if(metres >= TALL_FENCES_FROM_M && roll < 45)
            {
                add_obstacle(_next_obstacle_x, OK_TALL);
            }
            else if(roll < 65)
            {
                add_obstacle(_next_obstacle_x, OK_LOW);
            }
            else if(roll < 83)
            {
                add_obstacle(_next_obstacle_x, OK_HAY);
            }
            else
            {
                add_obstacle(_next_obstacle_x, OK_DRIFT);
                extra = 8;
            }

            int gap_min = lerp_int(GAP_MIN_START, GAP_MIN_END, difficulty);
            int gap_max = lerp_int(GAP_MAX_START, GAP_MAX_END, difficulty);
            int gap_frames = sb::rand_range(gap_min, gap_max);
            _next_obstacle_x += (nominal * gap_frames).integer() + extra;
        }

        void collide()
        {
            int left = _distance.integer() - HORSE_HIT_HALF_WIDTH;
            int right = _distance.integer() + HORSE_HIT_HALF_WIDTH;

            for(obstacle& o : _obstacles)
            {
                const obstacle_type& type = obstacle_types[o.kind];
                int o_left = o.x - type.width / 2;
                int o_right = o.x + type.width / 2;

                if(! o.hit && _invulnerable == 0 && o_left < right && o_right > left &&
                   _height < type.height - HIT_TOLERANCE)
                {
                    o.hit = true;
                    o.sprite.set_tiles(o.kind == OK_DRIFT ? bn::sprite_items::horse_drift.tiles_item() :
                                                            bn::sprite_items::horse_obstacles.tiles_item(),
                                       type.knocked_frame);
                    --_hearts;
#ifdef HORSE_AUTOPILOT
                    BN_LOG("HIT kind ", o.kind, " at m ", metres(), " speed ", _speed);
#endif
                    _stumble = STUMBLE_FRAMES;
                    _invulnerable = INVULNERABLE_FRAMES;
                    _boost = 0;
                    _streak = 0;
                    add_puff(o.x);

                    if(_hearts > 0)
                    {
                        sb::sfx(bn::sound_items::sfx_hurt);
                    }
                    else
                    {
                        remember_lead();
                    }
                }

                if(! o.passed && o_right < left)
                {
                    o.passed = true;

                    if(! o.hit && _invulnerable == 0)
                    {
                        ++_streak;
                        _boost = bn::min(_boost + CLEAR_BOOST, MAX_BOOST);

        #ifdef HORSE_AUTOPILOT
                        if(_streak % 25 == 0)
                        {
                            BN_LOG("clean ", _streak, " m ", metres(), " speed ", _speed);
                        }
#endif

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

        void update_markers()
        {
            // Spawn the next "100 m" flag when it's about to scroll in.
            int marker_x = _next_marker_m * PIXELS_PER_METRE;

            if(! _markers.full() && screen_x(marker_x) < 150)
            {
                bn::sprite_ptr flag = bn::sprite_items::horse_obstacles.create_sprite(
                            screen_x(marker_x), MARKER_GROUND_Y - 15, 6);
                flag.set_bg_priority(2);
                flag.set_z_order(3);
                _markers.push_back({ flag, {}, {}, marker_x, false });
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
                    sprite.set_z_order(3);
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

                if(! m.passed && sx <= PLAYER_X + 20 && _hearts > 0)
                {
                    m.passed = true;
                    sb::sfx(bn::sound_items::sfx_point);
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

        void update_hud()
        {
            int metres = this->metres();

            if(metres != _shown_metres)
            {
                _shown_metres = metres;
                bn::string<16> text = sb::to_text(metres);
                text.append(" m");
                _distance_label.set(-114, -70, text, sb::text_color::WHITE);
            }

            if(_hearts != _shown_hearts)
            {
                _shown_hearts = _hearts;
                bn::string<8> text;

                for(int i = 0; i < _hearts; ++i)
                {
                    text.append("@");
                }

                if(text.empty())
                {
                    _hearts_label.clear();
                }
                else
                {
                    _hearts_label.set(114, -70, text, sb::text_color::RED, sb::align::RIGHT);
                }
            }

            // Arrow + name when the rival is off screen (ahead or behind).
            int rival_sx = PLAYER_X + (_rival_x - _distance).integer();
            int hint = rival_sx > 150 ? 1 : rival_sx < -150 ? -1 : 0;

            if(hint != _shown_rival_hint)
            {
                _shown_rival_hint = hint;

                if(hint == 0)
                {
                    _rival_hint.clear();
                }
                else
                {
                    bn::string<16> text;

                    if(hint > 0)
                    {
                        text.append(sb::character_name(_rival));
                        text.append(" >");
                        _rival_hint.set(114, -54, text, sb::text_color::WHITE, sb::align::RIGHT);
                    }
                    else
                    {
                        text.append("< ");
                        text.append(sb::character_name(_rival));
                        _rival_hint.set(-114, -54, text, sb::text_color::WHITE);
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
}

// The best record shown on the map, the intro and the high score list (sb_games.h: game_info::best_text).
bn::string<40> sb::horse_best_text(bool compact)
{
    return score_record_text(GAME_HORSE, compact);
}

void sb::horse_run()
{
    // Shown on the start screen (scroll with Up/Down) and in the pause menu. '#' = heading.
    static constexpr bn::string_view howto[] = {
        "#Ride",
        "A: jump. Hold A to jump",
        "higher.",
        "Jump the fences and hay",
        "bales. Clean jumps give",
        "you a burst of speed.",
        "",
        "#Race",
        "3 bumps and the race is",
        "over. Your score is how",
        "far you get.",
    };

    {
        race game;
        play_music(bn::music_items::horse_song);

        if(bn::bg_palettes::fade_intensity() > 0)
        {
            fade_in();
        }

        bool quit = ! game_intro(GAME_HORSE, howto);

        while(! quit)
        {
            game.countdown();

            while(true)
            {
                if(bn::keypad::start_pressed())
                {
                    if(game_pause())
                    {
                        quit = true;
                        break;
                    }
                }

                bool alive = game.step();
                update();

                if(! alive)
                {
                    break;
                }
            }

            if(quit)
            {
                break;
            }

            game.finish();

            if(! game_over(GAME_HORSE, game.metres(), game.result_text()))
            {
                break;
            }

            play_music(bn::music_items::horse_song);
            game.reset();
        }

        fade_out();
    }

    panel_clear();
}
