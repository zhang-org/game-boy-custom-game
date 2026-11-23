#include "sb_games.h"

namespace sb
{
    namespace
    {
        constexpr game_info games[GAME_COUNT] = {
            { "Jingle Flap", "Reindeer Roost", flappy_run },
            { "Ping Pong Pals", "Rec Hall", pingpong_run },
            { "Sled Dash", "Big Hill", sled_run },
            { "Gift Catch", "Toy Workshop", catch_run },
            { "Ice Fishing", "Frozen Pond", fishing_run },
            { "Pony Gallop", "Stable", horse_run },
        };
    }

    const game_info& game(int id)
    {
        return games[id];
    }
}
