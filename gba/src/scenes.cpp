// Title screen and character select.
#include "bn_bg_palettes.h"
#include "bn_color.h"
#include "bn_keypad.h"
#include "bn_optional.h"
#include "bn_regular_bg_ptr.h"
#include "bn_sprite_ptr.h"
#include "bn_string.h"
#include "bn_vector.h"

#include "bn_music_items.h"
#include "bn_regular_bg_items_title_logo.h"
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
    void title_scene()
    {
        bn::bg_palettes::set_transparent_color(bn::color(4, 6, 14));
        bn::regular_bg_ptr logo = bn::regular_bg_items::title_logo.create_bg(0, 48);
        logo.set_priority(1);
        snowfall snow(32, 2);
        play_music(bn::music_items::map_song);

        // A parade of the whole family walking along the bottom of the screen.
        bn::vector<bn::sprite_ptr, character_count> walkers;

        for(int i = 0; i < character_count; ++i)
        {
            bn::sprite_ptr sprite = character_sprite(i).create_sprite(-140 + i * 28, 52, FRAME_SIDE);
            sprite.set_bg_priority(1);
            walkers.push_back(sprite);
        }

        label subtitle, press;
        subtitle.set(0, -4, "Family Winter Games", text_color::YELLOW, align::CENTER);
        fade_in();

        for(int t = 0; ; ++t)
        {
            if(t % 60 == 0)
            {
                press.set(0, 24, "Press START", text_color::WHITE, align::CENTER);
            }
            else if(t % 60 == 40)
            {
                press.clear();
            }

            for(int i = 0; i < walkers.size(); ++i)
            {
                bn::sprite_ptr& sprite = walkers[i];
                bn::fixed x = sprite.x() + bn::fixed(1) / 2;

                if(x > 140)
                {
                    x -= 280;
                }

                sprite.set_x(x);
                int step = ((t / 8) + i) % 4;
                int frame = step == 1 ? FRAME_SIDE_WALK_A : step == 3 ? FRAME_SIDE_WALK_B : FRAME_SIDE;
                sprite.set_tiles(character_sprite(i).tiles_item(), frame);
            }

            snow.update();
            update();

            if(bn::keypad::start_pressed() || bn::keypad::a_pressed())
            {
                sfx(bn::sound_items::sfx_confirm);
                break;
            }
        }

        fade_out();
        press.clear();
        subtitle.clear();
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

        if(! from_map)
        {
            fade_in();
        }

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
