#include "sb_games.h"

namespace sb
{
    namespace
    {
        constexpr game_info games[GAME_COUNT] = {
            { "Jingle Flap", "Reindeer Roost", flappy_run },
            { "Ping Pong Pals", "Rec Hall", pingpong_run },
            { "Sled Dash", "Big Hill", sled_run },
            { "Gift Tower", "Toy Workshop", catch_run, catch_best_text },
            { "Ice Fishing", "Frozen Pond", fishing_run, fishing_best_text },
            { "Show Jumping", "Stable", horse_run, horse_best_text },
            { "Snowball Fight", "Snow Fort", snowball_run, snowball_best_text },
        };
    }

    const game_info& game(int id)
    {
        return games[id];
    }
}
