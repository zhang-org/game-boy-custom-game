// Placeholder: replaced by the real game.
#include "bn_string_view.h"

#include "sb_core.h"
#include "sb_games.h"
#include "sb_ui.h"

void sb::flappy_run()
{
    constexpr bn::string_view howto[] = { "Coming soon!" };

    while(game_intro(GAME_FLAPPY, howto))
    {
        if(! game_over(GAME_FLAPPY, 0))
        {
            break;
        }
    }
}
