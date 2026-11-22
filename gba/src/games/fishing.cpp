// Placeholder: replaced by the real game.
#include "bn_string_view.h"

#include "sb_core.h"
#include "sb_games.h"
#include "sb_ui.h"

void sb::fishing_run()
{
    constexpr bn::string_view howto[] = { "Coming soon!" };

    while(game_intro(GAME_FISHING, howto))
    {
        if(! game_over(GAME_FISHING, 0))
        {
            break;
        }
    }
}
