// Title screen and character select.
#include "bn_bg_palettes.h"
#include "bn_color.h"
#include "bn_keypad.h"
#include "bn_optional.h"
#include "bn_regular_bg_ptr.h"
#include "bn_sprite_palettes.h"
#include "bn_sprite_ptr.h"
#include "bn_string.h"
#include "bn_vector.h"

#include "bn_music_items.h"
#include "bn_regular_bg_items_title_frame.h"
#include "bn_regular_bg_items_title_logo.h"
#include "bn_regular_bg_items_title_scene.h"
#include "bn_sprite_items_title_camera.h"
#include "bn_sound_items.h"

#include "sb_audio.h"
#include "sb_characters.h"
#include "sb_core.h"
#include "sb_games.h"
#include "sb_snow.h"
#include "sb_text.h"
#include "sb_ui.h"

namespace sb
{
    namespace
    {
        // ---- the opening scene: the family gathers by the big tree for a group photo ----
        // The game's NAME is a picture: tools/art/game_title.txt + tools/art/make_ui.py make it (title_logo).
        constexpr int TITLE_LOGO_Y = 40;        // where the name rests (screen centre = 0)
        constexpr int WALK_DELAY = 9;           // frames between one person and the next starting to walk in
        constexpr int COUNTDOWN_BEAT = 36;      // frames per "3", "2", "1"
        constexpr bn::fixed WALK_SPEED = 1.6;

        // Where everyone stands: the centre x and the feet y, in screen pixels (0, 0 = top-left corner).
        // Slot i is for character i: the first five stand in the back row, the rest in front.
        constexpr int group_slots[character_count][2] = {
            { 120 - 42, 130 }, { 120 - 18, 130 }, { 120 + 6, 130 }, { 120 + 30, 130 }, { 120 + 54, 130 },
            { 120 - 54, 146 }, { 120 - 30, 146 }, { 120 - 6, 146 }, { 120 + 18, 146 }, { 120 + 42, 146 },
        };

        void set_flash(bn::fixed intensity)
        {
            bn::bg_palettes::set_fade(bn::color(31, 31, 31), intensity);
            bn::sprite_palettes::set_fade(bn::color(31, 31, 31), intensity);
        }

        struct person
        {
            bn::sprite_ptr sprite;
            bn::fixed x;
        };

        enum class stage
        {
            WALK, POSE, COUNT, FLASH, TITLE
        };
    }

    void title_scene()
    {
        bn::bg_palettes::set_transparent_color(bn::color(4, 6, 14));
        bn::regular_bg_ptr scene = bn::regular_bg_items::title_scene.create_bg(0, 0);
        scene.set_priority(3);
        bn::optional<bn::regular_bg_ptr> photo_frame;
        // The name is 256 px tall and wraps around, so it waits invisible and starts just above the screen.
        bn::regular_bg_ptr logo = bn::regular_bg_items::title_logo.create_bg(0, -10);
        logo.set_visible(false);
        logo.set_priority(1);
        snowfall snow(24, 1);
        play_music(bn::music_items::map_song);

        bn::vector<person, character_count> family;

        for(int i = 0; i < character_count; ++i)
        {
            bool from_left = i % 2 == 0;
            bn::fixed start_x = from_left ? -24 : 264;
            bn::sprite_ptr sprite = character_sprite(i).create_sprite(start_x - 120, group_slots[i][1] - 96, FRAME_SIDE);
            sprite.set_bg_priority(2);
            sprite.set_z_order(-group_slots[i][1]);
            sprite.set_horizontal_flip(! from_left);
            family.push_back({ sprite, start_x });
        }

        bn::sprite_ptr camera = bn::sprite_items::title_camera.create_sprite(208 - 120, 152 - 96);
        camera.set_bg_priority(2);
        camera.set_z_order(-152);

        label subtitle, press, countdown;
        fade_in();

        stage now = stage::WALK;
        int stage_time = 0;
        bn::fixed logo_y = -10;
        bn::fixed logo_speed = 0;
        bool done = false;

        // Jumps straight to the end of the opening (pressing START during the intro).
        auto finish_intro = [&]() {
            for(int i = 0; i < character_count; ++i)
            {
                family[i].x = group_slots[i][0];
                family[i].sprite.set_x(family[i].x - 120);
                family[i].sprite.set_horizontal_flip(false);
            }

            set_flash(0);
            countdown.clear();
            camera.set_tiles(bn::sprite_items::title_camera.tiles_item(), 0);

            if(! photo_frame)
            {
                photo_frame = bn::regular_bg_items::title_frame.create_bg(0, 0);
                photo_frame->set_priority(2);
            }

            logo_y = TITLE_LOGO_Y;
            logo_speed = 0;
            logo.set_visible(true);
            logo.set_y(logo_y);
            now = stage::TITLE;
            stage_time = 100;
            subtitle.set(0, -22, "Family Winter Games", text_color::YELLOW, align::CENTER);
        };

        while(! done)
        {
            ++stage_time;

            switch(now)
            {
            case stage::WALK:
            {
                bool all_there = true;

                for(int i = 0; i < character_count; ++i)
                {
                    person& p = family[i];

                    if(stage_time < i * WALK_DELAY)
                    {
                        all_there = false;
                        continue;
                    }

                    bn::fixed target = group_slots[i][0];

                    if(p.x != target)
                    {
                        all_there = false;
                        bool right = target > p.x;
                        p.x += right ? WALK_SPEED : -WALK_SPEED;

                        if((right && p.x > target) || (! right && p.x < target))
                        {
                            p.x = target;
                        }

                        int step = (stage_time / 6 + i) % 4;
                        int frame = step == 1 ? FRAME_SIDE_WALK_A : step == 3 ? FRAME_SIDE_WALK_B : FRAME_SIDE;
                        p.sprite.set_tiles(character_sprite(i).tiles_item(), frame);
                        p.sprite.set_horizontal_flip(! right);
                        p.sprite.set_x(p.x - 120);
                    }
                    else
                    {
                        p.sprite.set_tiles(character_sprite(i).tiles_item(), FRAME_DOWN);
                        p.sprite.set_horizontal_flip(false);
                    }
                }

                if(all_there)
                {
                    now = stage::POSE;
                    stage_time = 0;
                }

                break;
            }

            case stage::POSE:
                if(stage_time == 30)
                {
                    now = stage::COUNT;
                    stage_time = 0;
                }

                break;

            case stage::COUNT:
            {
                int beat = stage_time / COUNTDOWN_BEAT;

                if(stage_time % COUNTDOWN_BEAT == 1 && beat < 3)
                {
                    static const char* const numbers[3] = { "3", "2", "1" };
                    countdown.set(88, 66, numbers[beat], text_color::WHITE, align::CENTER);
                    sfx(bn::sound_items::sfx_countdown);
                }

                if(stage_time >= 3 * COUNTDOWN_BEAT + 6)
                {
                    countdown.clear();
                    now = stage::FLASH;
                    stage_time = 0;
                }

                break;
            }

            case stage::FLASH:
                // A bright white flash; at its brightest the picture is taken.
                if(stage_time <= 3)
                {
                    set_flash(bn::fixed(stage_time) / 3);
                }

                if(stage_time == 3)
                {
                    sfx(bn::sound_items::sfx_record);
                    camera.set_tiles(bn::sprite_items::title_camera.tiles_item(), 1);

                    for(int i = 0; i < character_count; ++i)
                    {
                        family[i].sprite.set_tiles(character_sprite(i).tiles_item(), FRAME_CHEER);
                    }

                    photo_frame = bn::regular_bg_items::title_frame.create_bg(0, 0);
                    photo_frame->set_priority(2);
                }
                else if(stage_time > 8)
                {
                    set_flash(bn::max(bn::fixed(0), bn::fixed(1) - bn::fixed(stage_time - 8) / 26));
                }

                if(stage_time == 12)
                {
                    camera.set_tiles(bn::sprite_items::title_camera.tiles_item(), 0);
                }

                if(stage_time >= 36)
                {
                    set_flash(0);
                    now = stage::TITLE;
                    stage_time = 0;
                    logo_speed = 0;
                    logo.set_y(logo_y);
                    logo.set_visible(true);
                    sfx(bn::sound_items::sfx_whoosh);
                }

                break;

            case stage::TITLE:
                // The name drops in from the top and bounces once.
                if(logo_y < TITLE_LOGO_Y || logo_speed != 0)
                {
                    logo_speed += bn::fixed(0.4);
                    logo_y += logo_speed;

                    if(logo_y >= TITLE_LOGO_Y)
                    {
                        logo_y = TITLE_LOGO_Y;
                        logo_speed = logo_speed > 3 ? -logo_speed / 3 : bn::fixed(0);

                        if(logo_speed == 0)
                        {
                            subtitle.set(0, -22, "Family Winter Games", text_color::YELLOW, align::CENTER);
                        }
                    }

                    logo.set_y(logo_y);
                }
                else
                {
                    if(stage_time % 60 == 0)
                    {
                        press.set(0, 70, "Press START", text_color::WHITE, align::CENTER);
                    }
                    else if(stage_time % 60 == 40)
                    {
                        press.clear();
                    }

                    // Everybody bounces happily.
                    for(int i = 0; i < character_count; ++i)
                    {
                        bool up = ((stage_time / 20) + i) % 2 == 0;
                        family[i].sprite.set_tiles(character_sprite(i).tiles_item(), up ? FRAME_CHEER : FRAME_DOWN);
                    }
                }

                break;

            default:
                break;
            }

            snow.update();
            update();

            if(bn::keypad::start_pressed() || bn::keypad::a_pressed())
            {
                if(now == stage::TITLE)
                {
                    sfx(bn::sound_items::sfx_confirm);
                    done = true;
                }
                else
                {
                    finish_intro();
                }
            }
        }

        fade_out();
        press.clear();
        subtitle.clear();
        countdown.clear();
    }

    void character_select_scene(bool from_map)
    {
        bn::bg_palettes::set_transparent_color(bn::color(14, 20, 29));
        snowfall snow(16, 3);
        panel_clear();

        constexpr int columns = 5;
        bn::vector<bn::sprite_ptr, character_count> sprites;

        auto cell_x = [](int i) { return -96 + (i % columns) * 48; };
        auto cell_y = [](int i) { return -22 + (i / columns) * 50; };

        for(int i = 0; i < character_count; ++i)
        {
            // A little box for every character (tiles: 6 wide, 6 tall).
            int tx = (cell_x(i) + 120) / 8 - 3 + 1;
            int ty = (cell_y(i) + 80) / 8 - 3;
            panel(tx - 1, ty - 1, 6, 6);
            bn::sprite_ptr sprite = character_sprite(i).create_sprite(cell_x(i), cell_y(i) - 2, FRAME_DOWN);
            sprite.set_bg_priority(0);
            sprites.push_back(sprite);
        }

        label title, name, hint, arrow;
        title.set(0, -68, "Who's playing?", text_color::WHITE, align::CENTER);
        hint.set(0, 70, from_map ? "A: Choose   B: Back" : "A: Choose", text_color::WHITE, align::CENTER);

        int selected = player_character();
        int shown = -1;

        fade_in();

        for(int t = 0; ; ++t)
        {
            if(shown != selected)
            {
                shown = selected;
                bn::string<24> text(character_name(selected));
                name.set(0, 50, text, text_color::YELLOW, align::CENTER);
            }

            arrow.set(cell_x(selected), cell_y(selected) - 30 + ((t / 10) % 2), "_", text_color::RED, align::CENTER);

            for(int i = 0; i < character_count; ++i)
            {
                int frame = FRAME_DOWN;

                if(i == selected)
                {
                    int step = (t / 8) % 4;
                    frame = step == 1 ? FRAME_DOWN_WALK_A : step == 3 ? FRAME_DOWN_WALK_B : FRAME_DOWN;
                }

                sprites[i].set_tiles(character_sprite(i).tiles_item(), frame);
            }

            snow.update();
            update();

            int old = selected;

            if(bn::keypad::left_pressed())
            {
                selected = (selected % columns == 0) ? selected + columns - 1 : selected - 1;
            }
            else if(bn::keypad::right_pressed())
            {
                selected = (selected % columns == columns - 1) ? selected - columns + 1 : selected + 1;
            }
            else if(bn::keypad::up_pressed() || bn::keypad::down_pressed())
            {
                selected = (selected + columns) % character_count;
            }

            if(old != selected)
            {
                sfx(bn::sound_items::sfx_select);
            }

            if(bn::keypad::a_pressed() || bn::keypad::start_pressed())
            {
                sfx(bn::sound_items::sfx_confirm);
                set_player_character(selected);
                sprites[selected].set_tiles(character_sprite(selected).tiles_item(), FRAME_CHEER);
                wait_frames(30);
                break;
            }

            if(from_map && bn::keypad::b_pressed())
            {
                sfx(bn::sound_items::sfx_back);
                break;
            }
        }

        fade_out();
        panel_clear();
    }
}
