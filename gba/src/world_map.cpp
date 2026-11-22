// TEMPORARY: menu stand-in until the walkable town map is in.
#include "bn_bg_palettes.h"
#include "bn_color.h"
#include "bn_string_view.h"

#include "bn_music_items.h"

#include "sb_audio.h"
#include "sb_core.h"
#include "sb_games.h"
#include "sb_ui.h"

void sb::world_map_scene()
{
    bn::bg_palettes::set_transparent_color(bn::color(20, 26, 31));
    play_music(bn::music_items::map_song);
    fade_in();
    constexpr bn::string_view items[] = { "Jingle Flap", "Ping Pong Pals", "Sled Dash", "Gift Catch", "Ice Fishing",
                                          "Pony Gallop", "Change character" };
    int choice = menu(2, 2, items, 0, false);
    fade_out();
    panel_clear();

    if(choice == 6)
    {
        character_select_scene(true);
    }
    else
    {
        fade_in();
        game(choice).run();
        fade_out();
        panel_clear();
    }
}
