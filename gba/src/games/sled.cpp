// Sled Dash: ride a sled down the Big Hill. Steer left and right to dodge pine trees, rocks and
// snowmen, grab presents for bonus points. The hill gets faster the longer you last.
//
// View: top-down 3/4. The sled stays in the upper part of the screen and the world scrolls UP
// past it, so the player sees what's coming from below.
//
// Score = metres travelled (1 m = PIXELS_PER_METRE px of hill) + PRESENT_POINTS per present.
//
// Art: tools/art/make_sled.py -> graphics/sled/. Music: sled_song.
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
#include "bn_regular_bg_items_sled_hill.h"
#include "bn_sound_items.h"
#include "bn_sprite_items_sled_fx.h"
#include "bn_sprite_items_sled_obstacles.h"
#include "bn_sprite_items_sled_present.h"
#include "bn_sprite_items_sled_puff.h"
#include "bn_sprite_items_sled_sled.h"
#include "bn_sprite_items_sled_track.h"

#include "sb_audio.h"
#include "sb_characters.h"
#include "sb_core.h"
#include "sb_games.h"
#include "sb_snow.h"
#include "sb_text.h"
#include "sb_ui.h"

namespace
{
    // ---------------------------------------------------------------------------- tuning
    // Speeds are in pixels per frame (60 frames per second).

    // Hill speed at the start of a run.
    constexpr bn::fixed START_SPEED = 1;
    // Top hill speed (reached after (MAX - START) / GAIN seconds of riding).
    constexpr bn::fixed MAX_SPEED = 3;
    // How much the hill speed grows every second.
    constexpr bn::fixed SPEED_GAIN_PER_SECOND = bn::fixed(1) / 50;
    // How quickly the actual speed follows the target speed (per frame), speeding up / slowing down.
    constexpr bn::fixed SPEED_EASE_UP = bn::fixed(1) / 40;
    constexpr bn::fixed SPEED_EASE_DOWN = bn::fixed(1) / 16;

    // A button: short speed boost.
    constexpr bn::fixed BOOST_MULTIPLIER = bn::fixed(3) / 2;  // speed x1.5 while boosting
    constexpr int BOOST_FRAMES = 50;                         // how long a boost lasts
    constexpr int BOOST_COOLDOWN_FRAMES = 150;               // wait between boosts (from boost start)
    // B button (hold): brake.
    constexpr bn::fixed BRAKE_MULTIPLIER = bn::fixed(1) / 2;  // speed x0.5 while braking

    // Steering (sideways) physics: a little momentum so it feels like sliding on snow.
    constexpr bn::fixed STEER_ACCEL = bn::fixed(3) / 20;      // sideways speed gained per frame when holding
    constexpr bn::fixed STEER_COUNTER_ACCEL = bn::fixed(1) / 4; // ... when steering against the slide
    constexpr bn::fixed STEER_MAX = 2;                       // top sideways speed
    constexpr bn::fixed STEER_FRICTION = bn::fixed(9) / 10;   // sideways speed kept per frame when not steering
    constexpr bn::fixed LEAN_THRESHOLD = bn::fixed(6) / 10;   // sideways speed that leans the sled

    // Where the sled rides on screen (y, centre of the sled sprite) and how far it can go sideways.
    constexpr int SLED_Y = -36;
    constexpr int SLED_X_LIMIT = 84;
    // Obstacles and presents are placed with their centre within +-this.
    constexpr int SPAWN_X_LIMIT = 80;

    // Hearts and crashing.
    constexpr int START_HEARTS = 3;
    constexpr int INVULNERABLE_FRAMES = 100;                  // blinking time after a crash
    constexpr bn::fixed CRASH_SPEED_FACTOR = bn::fixed(3) / 10; // speed kept right after a crash
    constexpr bn::fixed CRASH_BUMP = bn::fixed(3) / 2;        // sideways push away from what you hit

    // Obstacle rows. "Difficulty" goes from 0 (start speed) to 1 (top speed).
    constexpr int FIRST_ROW_DISTANCE = 170;   // pixels of free hill before the first row
    constexpr int ROW_GAP_EASY = 84;          // pixels between rows at difficulty 0 ...
    constexpr int ROW_GAP_HARD = 46;          // ... and at difficulty 1
    constexpr int ROW_GAP_RANDOM = 18;        // plus 0..this random pixels
    constexpr int ROW_MAX_OBSTACLES_EASY = 1; // most obstacles in one row at difficulty 0 ...
    constexpr int ROW_MAX_OBSTACLES_HARD = 3; // ... and at difficulty 1
    constexpr int OBSTACLE_MIN_SPACING = 30;  // min distance between obstacle centres in one row
    // Every row keeps a clear lane (no obstacles) this wide on each side of its centre ...
    constexpr int LANE_HALF_EASY = 44;
    constexpr int LANE_HALF_HARD = 20;
    // ... and the lane only moves as far as the sled can comfortably steer before the next row.
    constexpr int LANE_SHIFT_EASY = 10;              // max lane move per row at difficulty 0 ...
    constexpr int LANE_SHIFT_HARD = 48;              // ... and at difficulty 1
    // Early on, the lane is pulled towards wherever the sled is (0 = not at all, 1 = always on the
    // sled). This fades out as difficulty rises, so beginners who barely steer still do well.
    constexpr bn::fixed LANE_FOLLOW_EASY = bn::fixed(6) / 10;
    constexpr bn::fixed LANE_REACH_FACTOR = bn::fixed(1) / 2; // fraction of the sled's max reach used

    // Presents.
    constexpr int PRESENT_CHANCE_PERCENT = 30; // chance a row also has a present (in its clear lane)
    constexpr int PRESENT_MIN_OFFSET = 14;     // presents sit at least this far from the lane centre,
                                               // so you have to steer a little to grab them
    constexpr int PRESENT_POINTS = 50;         // bonus points per present

    // Scoring.
    constexpr int PIXELS_PER_METRE = 8;        // 1 point per this many pixels travelled

    // Pools (fixed number of sprites; when a pool is full, nothing new spawns).
    constexpr int OBSTACLE_POOL = 16;
    constexpr int PRESENT_POOL = 5;
    constexpr int TRACK_POOL = 14;
    constexpr int PUFF_POOL = 8;
    constexpr int SNOWFLAKES = 14;

    constexpr int TRACK_STEP = 6;              // pixels of travel between ski-track marks
    constexpr int SPAWN_Y = 104;               // new things appear here (below the screen)
    constexpr int DESPAWN_Y = -104;            // and disappear here (above the screen)

    // ------------------------------------------------------------------------ art layout
    constexpr int SPRITE_PRIORITY = 2;         // world sprites (snowfall uses 1, so it's on top)
    constexpr int TRACK_PRIORITY = 3;          // tracks lie on the snow under everything
    constexpr int BG_PRIORITY = 3;

    enum obstacle_type
    {
        OBS_PINE, OBS_SNOWY_PINE, OBS_ROCK, OBS_ROCKS, OBS_SNOWMAN, OBS_TYPES
    };

    // Hitbox of each obstacle relative to its sprite centre (32x32 frames; feet at row 28).
    struct hitbox
    {
        int half_w, top, bottom;
    };

    constexpr hitbox obstacle_boxes[OBS_TYPES] = {
        { 7, 5, 13 },   // pine: lower branches + trunk
        { 7, 5, 13 },   // snowy pine
        { 9, 1, 12 },   // rock
        { 12, 1, 12 },  // two rocks
        { 6, 2, 12 },   // snowman
    };

    // Chance (out of 100) of each type, in order.
    constexpr int obstacle_weights[OBS_TYPES] = { 42, 20, 20, 9, 9 };

    // Sled hitbox relative to SLED_Y (the deck and runners, not the rider's head).
    constexpr hitbox sled_box = { 8, 4, 13 };
    constexpr int FOOT_OFFSET = 12;            // obstacle sprite centre -> its feet (for depth sorting)
    constexpr int SLED_FOOT_OFFSET = 12;
    constexpr int RIDER_DY = -4;               // rider sprite centre relative to the sled sprite

    struct obstacle
    {
        bn::sprite_ptr sprite;
        bn::fixed x, y;
        int type = 0;
        bool active = false;
    };

    struct present
    {
        bn::sprite_ptr sprite;
        bn::fixed x, y;
        bool active = false;
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

    // Depth: things whose feet are lower on screen than the sled's are drawn in front of it.
    // Only a few z values are used on purpose: Butano has a limited number of sprite sort layers
    // (16 in total, one per distinct priority + z order), so per-pixel z orders crash it.
    constexpr int Z_FX = -3;
    constexpr int Z_FRONT = -2;
    constexpr int Z_RIDER = -1;
    constexpr int Z_SLED = 0;
    constexpr int Z_BEHIND = 1;

    int z_for(int foot_y)
    {
        return foot_y > SLED_Y + SLED_FOOT_OFFSET ? Z_FRONT : Z_BEHIND;
    }

    bn::fixed lerp(int a, int b, bn::fixed t)
    {
        return bn::fixed(a) + (bn::fixed(b - a) * t);
    }

    class sled_game
    {
    public:
        sled_game() :
            _bg(bn::regular_bg_items::sled_hill.create_bg(0, 0)),
            _sled(bn::sprite_items::sled_sled.create_sprite(0, SLED_Y, 0)),
            _rider(sb::character_sprite(sb::player_character()).create_sprite(0, SLED_Y + RIDER_DY, sb::FRAME_SIT)),
            _fx(bn::sprite_items::sled_fx.create_sprite(0, 0, 0)),
            _snow(SNOWFLAKES, 1)
        {
            _bg.set_priority(BG_PRIORITY);
            _sled.set_bg_priority(SPRITE_PRIORITY);
            _rider.set_bg_priority(SPRITE_PRIORITY);
            _fx.set_bg_priority(SPRITE_PRIORITY);
            _fx.set_visible(false);

            for(int i = 0; i < OBSTACLE_POOL; ++i)
            {
                bn::sprite_ptr sprite = bn::sprite_items::sled_obstacles.create_sprite(0, 0, 0);
                sprite.set_bg_priority(SPRITE_PRIORITY);
                sprite.set_visible(false);
                _obstacles.push_back({ sprite, 0, 0, 0, false });
            }

            for(int i = 0; i < PRESENT_POOL; ++i)
            {
                bn::sprite_ptr sprite = bn::sprite_items::sled_present.create_sprite(0, 0, 0);
                sprite.set_bg_priority(SPRITE_PRIORITY);
                sprite.set_visible(false);
                _presents.push_back({ sprite, 0, 0, false });
            }

            for(int i = 0; i < TRACK_POOL; ++i)
            {
                bn::sprite_ptr sprite = bn::sprite_items::sled_track.create_sprite(0, 0, 0);
                sprite.set_bg_priority(TRACK_PRIORITY);
                sprite.set_visible(false);
                _tracks.push_back({ sprite, 0, false });
            }

            for(int i = 0; i < PUFF_POOL; ++i)
            {
                bn::sprite_ptr sprite = bn::sprite_items::sled_puff.create_sprite(0, 0, 0);
                sprite.set_bg_priority(SPRITE_PRIORITY);
                sprite.set_visible(false);
                _puffs.push_back({ sprite, 0, 0, 0, 0, false });
            }

            reset();
        }

        void reset()
        {
            _x = 0;
            _vx = 0;
            _speed = 0;
            _frames = 0;
            _hearts = START_HEARTS;
            _invulnerable = 0;
            _boost = 0;
            _boost_cooldown = 0;
            _metres = 0;
            _metre_travel = 0;
            _presents_got = 0;
            _next_row = FIRST_ROW_DISTANCE;
            _next_track = TRACK_STEP;
            _lane = 0;
            _countdown = 3 * COUNT_STEP;
            _dying = 0;
            _fx_timer = 0;
            _popup_timer = 0;
            _bg_y = 0;
            _shown_score = -1;
            _shown_hearts = -1;

            for(obstacle& o : _obstacles)
            {
                o.active = false;
                o.sprite.set_visible(false);
            }

            for(present& p : _presents)
            {
                p.active = false;
                p.sprite.set_visible(false);
            }

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
            _popup.clear();
            _count_label.clear();
            _bg.set_y(0);
            place_sled();
        }

        int score() const
        {
            return _metres + _presents_got * PRESENT_POINTS;
        }

        int metres() const
        {
            return _metres;
        }

        int presents_got() const
        {
            return _presents_got;
        }

        void show_hud()
        {
            _shown_score = -1;
            _shown_hearts = -1;
            update_hud();
        }

        void hide_hud()
        {
            _score_label.clear();
            _hearts_label.clear();
            _lost_label.clear();
            _popup.clear();
            _count_label.clear();
        }

        // One frame of play. Returns true when the run is over (all hearts lost and the sled stopped).
        bool step()
        {
            ++_frames;

            if(_countdown > 0)
            {
                update_countdown();
                _snow.update();
                return false;
            }

            if(_dying > 0)
            {
                --_dying;
                _speed = _speed * bn::fixed(9) / 10;
                _vx = _vx * bn::fixed(9) / 10;
                scroll_world();
                update_fx();
                _snow.update(0, -_speed / 2);
                place_sled();
                return _dying == 0;
            }

            update_speed();
            steer();
            scroll_world();
            spawn_rows();
            spawn_tracks();
            check_collisions();
            update_fx();
            _snow.update(0, -_speed * bn::fixed(7) / 10);
            place_sled();
            update_hud();
            return false;
        }

    private:
        static constexpr int COUNT_STEP = 36;  // frames per "3", "2", "1", "GO!"

        bn::regular_bg_ptr _bg;
        bn::sprite_ptr _sled;
        bn::sprite_ptr _rider;
        bn::sprite_ptr _fx;
        sb::snowfall _snow;
        bn::vector<obstacle, OBSTACLE_POOL> _obstacles;
        bn::vector<present, PRESENT_POOL> _presents;
        bn::vector<track, TRACK_POOL> _tracks;
        bn::vector<puff, PUFF_POOL> _puffs;
        sb::label _score_label, _hearts_label, _lost_label, _popup, _count_label;

        bn::fixed _x, _vx, _speed, _metre_travel, _next_row, _next_track, _bg_y;
        int _lane = 0;
        int _frames = 0;
        int _hearts = START_HEARTS;
        int _invulnerable = 0;
        int _boost = 0;
        int _boost_cooldown = 0;
        int _metres = 0;
        int _presents_got = 0;
        int _countdown = 0;
        int _dying = 0;
        int _fx_timer = 0;
        int _fx_frame = 0;
        int _popup_timer = 0;
        int _shown_score = -1;
        int _shown_hearts = -1;
        int _ride_frames = 0;  // frames since GO (drives the speed ramp)

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
                _ride_frames = 0;
                _speed = START_SPEED;
                show_hud();
            }
        }

        bn::fixed difficulty() const
        {
            bn::fixed base = base_speed();
            return (base - START_SPEED) / (MAX_SPEED - START_SPEED);
        }

        bn::fixed base_speed() const
        {
            bn::fixed base = START_SPEED + SPEED_GAIN_PER_SECOND * bn::min(_ride_frames, 60 * 600) / 60;
            return bn::min(base, MAX_SPEED);
        }

        void update_speed()
        {
            ++_ride_frames;

            if(_ride_frames == 40)
            {
                _count_label.clear();  // hide "GO!"
            }

            if(_boost_cooldown > 0)
            {
                --_boost_cooldown;
            }

            if(_boost > 0)
            {
                --_boost;
            }
            else if(bn::keypad::a_pressed() && _boost_cooldown == 0)
            {
                _boost = BOOST_FRAMES;
                _boost_cooldown = BOOST_COOLDOWN_FRAMES;
                sb::sfx(bn::sound_items::sfx_whoosh);
            }

            bn::fixed target = base_speed();

            if(_boost > 0)
            {
                target *= BOOST_MULTIPLIER;
            }

            if(bn::keypad::b_held())
            {
                target *= BRAKE_MULTIPLIER;
            }

            if(_speed < target)
            {
                _speed = bn::min(target, _speed + SPEED_EASE_UP);
            }
            else
            {
                _speed = bn::max(target, _speed - SPEED_EASE_DOWN);
            }

            // Distance -> metres.
            _metre_travel += _speed;

            while(_metre_travel >= PIXELS_PER_METRE)
            {
                _metre_travel -= PIXELS_PER_METRE;
                ++_metres;
            }

            if(_invulnerable > 0)
            {
                --_invulnerable;
            }

            // Snow spray when boosting (a trail behind both runners) or braking.
            if(_boost > 0 && _frames % 3 == 0)
            {
                spawn_puff(_x - 13, -1);
                spawn_puff(_x + 13, 1);
            }
            else if(bn::keypad::b_held() && _speed > 1 && _frames % 5 == 0)
            {
                spawn_puff(_x - 11, -1);
                spawn_puff(_x + 11, 1);
            }
        }

        void steer()
        {
            bool left = bn::keypad::left_held();
            bool right = bn::keypad::right_held();

            if(left && ! right)
            {
                _vx -= _vx > 0 ? STEER_COUNTER_ACCEL : STEER_ACCEL;
            }
            else if(right && ! left)
            {
                _vx += _vx < 0 ? STEER_COUNTER_ACCEL : STEER_ACCEL;
            }
            else
            {
                _vx *= STEER_FRICTION;

                if(bn::abs(_vx) < bn::fixed(1) / 32)
                {
                    _vx = 0;
                }
            }

            _vx = bn::clamp(_vx, -STEER_MAX, STEER_MAX);
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

            // Spray snow off the outside runner when carving hard.
            if((left || right) && bn::abs(_vx) > 1 && _frames % 4 == 0)
            {
                int side = _vx < 0 ? 1 : -1;
                spawn_puff(_x + side * 11, side);
            }
        }

        void scroll_world()
        {
            _bg_y -= _speed;

            if(_bg_y < -256)
            {
                _bg_y += 256;
            }

            _bg.set_y(_bg_y);

            for(obstacle& o : _obstacles)
            {
                if(! o.active)
                {
                    continue;
                }

                o.y -= _speed;

                if(o.y < DESPAWN_Y)
                {
                    o.active = false;
                    o.sprite.set_visible(false);
                    continue;
                }

                o.sprite.set_position(o.x, o.y);
                o.sprite.set_z_order(z_for(o.y.integer() + FOOT_OFFSET));
            }

            for(present& p : _presents)
            {
                if(! p.active)
                {
                    continue;
                }

                p.y -= _speed;

                if(p.y < DESPAWN_Y)
                {
                    p.active = false;
                    p.sprite.set_visible(false);
                    continue;
                }

                // Presents bob gently.
                int bob = ((_frames / 12) % 2);
                p.sprite.set_position(p.x, p.y - bob);
                p.sprite.set_z_order(z_for(p.y.integer() + 6));
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

                t.sprite.set_y(t.y);
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
                p.sprite.set_position(p.x, p.y);
                p.sprite.set_tiles(bn::sprite_items::sled_puff.tiles_item(), p.age / 6);
            }

            _next_row -= _speed;
            _next_track -= _speed;
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
                    p.sprite.set_position(p.x, p.y);
                    p.sprite.set_tiles(bn::sprite_items::sled_puff.tiles_item(), 0);
                    p.sprite.set_z_order(Z_BEHIND);  // spray drifts up the hill, behind the sled
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

            for(track& t : _tracks)
            {
                if(! t.active)
                {
                    t.active = true;
                    t.y = SLED_Y + 10;
                    bn::fixed slope = _speed > 0 ? _vx / _speed : bn::fixed(0);
                    int frame = slope < bn::fixed(-3) / 10 ? 1 : slope > bn::fixed(3) / 10 ? 2 : 0;
                    t.sprite.set_tiles(bn::sprite_items::sled_track.tiles_item(), frame);
                    t.sprite.set_position(_x, t.y);
                    t.sprite.set_visible(true);
                    return;
                }
            }
        }

        // ------------------------------------------------------------------- spawning
        int pick_type()
        {
            int roll = sb::rand_range(0, 99);

            for(int i = 0; i < OBS_TYPES; ++i)
            {
                roll -= obstacle_weights[i];

                if(roll < 0)
                {
                    return i;
                }
            }

            return OBS_PINE;
        }

        void spawn_rows()
        {
            if(_next_row > 0)
            {
                return;
            }

            bn::fixed d = difficulty();
            int gap = lerp(ROW_GAP_EASY, ROW_GAP_HARD, d).round_integer() + sb::rand_range(0, ROW_GAP_RANDOM);
            int lane_half = lerp(LANE_HALF_EASY, LANE_HALF_HARD, d).round_integer();
            int max_count = lerp(ROW_MAX_OBSTACLES_EASY, ROW_MAX_OBSTACLES_HARD, d).round_integer();

            // Move the clear lane, but only as far as the sled can comfortably reach in time.
            // Frames until the next row reaches the sled, at the fastest the hill can go right now.
            bn::fixed fastest = bn::max(base_speed() * BOOST_MULTIPLIER, bn::fixed(1));
            int frames = (bn::fixed(gap) / fastest).integer();
            int reach = (bn::fixed(frames) * STEER_MAX * LANE_REACH_FACTOR).integer();
            int shift = bn::min(lerp(LANE_SHIFT_EASY, LANE_SHIFT_HARD, d).round_integer(), reach);
            int lane_limit = SPAWN_X_LIMIT - lane_half / 2;
            bn::fixed lane = _lane + sb::rand_range(-shift, shift);
            lane += (_x - lane) * LANE_FOLLOW_EASY * (1 - d);
            _lane = bn::clamp(lane.round_integer(), -lane_limit, lane_limit);

            int count = sb::rand_range(1, max_count);
            int placed_x[ROW_MAX_OBSTACLES_HARD];
            int placed = 0;
            int y = SPAWN_Y + sb::rand_range(0, 6);

            for(int i = 0; i < count; ++i)
            {
                int type = pick_type();
                int half = obstacle_boxes[type].half_w;

                for(int attempt = 0; attempt < 8; ++attempt)
                {
                    int x = sb::rand_range(-SPAWN_X_LIMIT, SPAWN_X_LIMIT);

                    if(bn::abs(x - _lane) < lane_half + half)
                    {
                        continue;
                    }

                    bool clear = true;

                    for(int j = 0; j < placed; ++j)
                    {
                        if(bn::abs(x - placed_x[j]) < OBSTACLE_MIN_SPACING)
                        {
                            clear = false;
                        }
                    }

                    if(! clear)
                    {
                        continue;
                    }

                    if(spawn_obstacle(type, x, y + sb::rand_range(0, 4)))
                    {
                        placed_x[placed] = x;
                        ++placed;
                    }

                    break;
                }
            }

            if(sb::rand_range(0, 99) < PRESENT_CHANCE_PERCENT)
            {
                int offset = sb::rand_range(PRESENT_MIN_OFFSET, bn::max(lane_half - 6, PRESENT_MIN_OFFSET));
                int x = _lane + (sb::rand_range(0, 1) ? offset : -offset);

                if(x < -SPAWN_X_LIMIT || x > SPAWN_X_LIMIT)
                {
                    x = _lane * 2 - x;  // mirror to the other side of the lane
                }
                spawn_present(x, y + 4);
            }

            _next_row += gap;
        }

        bool spawn_obstacle(int type, int x, int y)
        {
            for(obstacle& o : _obstacles)
            {
                if(! o.active)
                {
                    o.active = true;
                    o.type = type;
                    o.x = x;
                    o.y = y;
                    o.sprite.set_tiles(bn::sprite_items::sled_obstacles.tiles_item(), type);
                    o.sprite.set_horizontal_flip(type != OBS_SNOWMAN && sb::rand_range(0, 1) == 1);
                    o.sprite.set_position(o.x, o.y);
                    o.sprite.set_visible(true);
                    return true;
                }
            }

            return false;
        }

        void spawn_present(int x, int y)
        {
            for(present& p : _presents)
            {
                if(! p.active)
                {
                    p.active = true;
                    p.x = x;
                    p.y = y;
                    p.sprite.set_tiles(bn::sprite_items::sled_present.tiles_item(), sb::rand_range(0, 2));
                    p.sprite.set_position(p.x, p.y);
                    p.sprite.set_visible(true);
                    return;
                }
            }
        }

        // ----------------------------------------------------------------- collisions
        void check_collisions()
        {
            int sl = (_x - sled_box.half_w).integer();
            int sr = (_x + sled_box.half_w).integer();
            int st = SLED_Y + sled_box.top;
            int sb_ = SLED_Y + sled_box.bottom;

            for(present& p : _presents)
            {
                if(! p.active)
                {
                    continue;
                }

                int px = p.x.integer();
                int py = p.y.integer();

                if(px + 8 >= sl && px - 8 <= sr && py + 6 >= st - 4 && py - 6 <= sb_)
                {
                    p.active = false;
                    p.sprite.set_visible(false);
                    ++_presents_got;
                    sb::sfx(bn::sound_items::sfx_catch);
                    show_fx(px, py, 0);
                    _popup_timer = 40;
                    bn::string<8> text("+");
                    text.append(sb::to_text(PRESENT_POINTS));
                    _popup.set(_x.integer(), SLED_Y - 30, text, sb::text_color::YELLOW, sb::align::CENTER);
                }
            }

            if(_invulnerable > 0)
            {
                return;
            }

            for(obstacle& o : _obstacles)
            {
                if(! o.active)
                {
                    continue;
                }

                const hitbox& box = obstacle_boxes[o.type];
                int ox = o.x.integer();
                int oy = o.y.integer();

                if(ox + box.half_w >= sl && ox - box.half_w <= sr && oy + box.bottom >= st && oy + box.top <= sb_)
                {
                    crash(ox, oy + box.top);
                    return;
                }
            }
        }

        void crash(int ox, int oy)
        {
            --_hearts;
            sb::sfx(bn::sound_items::sfx_crash);
            _speed *= CRASH_SPEED_FACTOR;
            _boost = 0;
            _vx = _x < ox ? -CRASH_BUMP : CRASH_BUMP;
            _invulnerable = INVULNERABLE_FRAMES;
            show_fx(((_x.integer() + ox) / 2), (SLED_Y + 8 + oy) / 2, 2);

            for(int i = 0; i < 3; ++i)
            {
                spawn_puff(_x - 10 + i * 10, i - 1);
            }

            update_hud();

            if(_hearts <= 0)
            {
                _dying = 70;
                _invulnerable = 0;
            }
        }

        void show_fx(int x, int y, int frame)
        {
            _fx_frame = frame;
            _fx.set_tiles(bn::sprite_items::sled_fx.tiles_item(), frame);
            _fx.set_position(x, y);
            _fx.set_z_order(Z_FX);
            _fx.set_visible(true);
            _fx_timer = 24;
        }

        void update_fx()
        {
            if(_fx_timer > 0)
            {
                --_fx_timer;
                _fx.set_y(_fx.y() - _speed);

                if(_fx_timer == 10 && _fx_frame == 0)
                {
                    _fx.set_tiles(bn::sprite_items::sled_fx.tiles_item(), 1);  // sparkle shrinks
                }

                if(_fx_timer == 0)
                {
                    _fx.set_visible(false);
                }
            }

            if(_popup_timer > 0)
            {
                --_popup_timer;

                if(_popup_timer == 0)
                {
                    _popup.clear();
                }
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

            // A little rattle when going fast.
            int rattle = (_speed > 2 && (_frames / 3) % 2 == 0) ? 1 : 0;
            _sled.set_tiles(bn::sprite_items::sled_sled.tiles_item(), frame);
            _sled.set_position(_x, SLED_Y + rattle);
            _rider.set_position(_x + rider_dx, SLED_Y + RIDER_DY + rattle);
            _sled.set_z_order(Z_SLED);
            _rider.set_z_order(Z_RIDER);

            bool visible = _invulnerable == 0 || (_invulnerable / 4) % 2 == 0;
            _sled.set_visible(visible);
            _rider.set_visible(visible);
        }

        void update_hud()
        {
            int s = score();

            if(s != _shown_score && (_frames % 3 == 0 || _shown_score < 0))
            {
                _shown_score = s;
                bn::string<24> text("Score ");
                text.append(sb::to_text(s));
                _score_label.set(-114, -71, text, sb::text_color::WHITE);
            }

            if(_hearts != _shown_hearts)
            {
                _shown_hearts = _hearts;
                bn::string<8> full, empty;

                for(int i = 0; i < START_HEARTS; ++i)
                {
                    (i < _hearts ? full : empty).push_back('@');
                }

                if(full.empty())
                {
                    _hearts_label.clear();
                }
                else
                {
                    _hearts_label.set(-114, -56, full, sb::text_color::RED);
                }

                if(empty.empty())
                {
                    _lost_label.clear();
                }
                else
                {
                    _lost_label.set(-114 + sb::text_width(full), -56, empty, sb::text_color::DARK);
                }
            }
        }
    };
}

void sb::sled_run()
{
    constexpr bn::string_view howto[] = {
        "< > Steer down the hill",
        "Dodge trees and rocks",
        "Grab presents for points",
        "A: Boost    B: Brake",
    };

    bn::bg_palettes::set_transparent_color(bn::color(29, 30, 31));

    {
        sled_game game;
        play_music(bn::music_items::sled_song);
        fade_in();

        if(game_intro(GAME_SLED, howto))
        {
            while(true)
            {
                game.reset();
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
                        if(game_pause())
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
                bn::string<32> detail = to_text(game.metres());
                detail.append(" m and ");
                detail.append(to_text(game.presents_got()));
                detail.append(game.presents_got() == 1 ? " present" : " presents");

                if(! game_over(GAME_SLED, game.score(), detail))
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
