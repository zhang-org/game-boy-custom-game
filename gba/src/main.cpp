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

#ifdef SB_TEST_GAME
    // Test builds only (make USERFLAGS=-DSB_TEST_GAME=2): skip the opening, character select and map and
    // go straight into one game (the numbers are sb::game_id). Used with the headless harness.
    while(true)
    {
        sb::game(SB_TEST_GAME).run();
    }
#else
    sb::title_scene();
    sb::character_select_scene(false);

    while(true)
    {
        sb::world_map_scene();
    }
#endif
}
