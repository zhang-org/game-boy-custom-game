// Snowberry: a collection of winter mini games starring the family.
#include "bn_core.h"

#include "sb_core.h"
#include "sb_games.h"
#include "sb_save.h"

int main()
{
    bn::core::init();
    sb::save_init();
    sb::set_player_character(sb::last_character());

    sb::title_scene();
    sb::character_select_scene(false);

    while(true)
    {
        sb::world_map_scene();
    }
}
