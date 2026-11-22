// Placeholder: replaced by the real game.
#include "bn_string_view.h"

#include "sb_core.h"
#include "sb_games.h"
#include "sb_ui.h"

void sb::pingpong_run()
{
    constexpr bn::string_view howto[] = { "Coming soon!" };

    while(game_intro(GAME_PINGPONG, howto))
    {
        if(! game_over(GAME_PINGPONG, 0))
        {
            break;
        }
    }
}
