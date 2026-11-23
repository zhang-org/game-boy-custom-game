// The list of mini games and the places on the town map where they are played.
#ifndef SB_GAMES_H
#define SB_GAMES_H

namespace sb
{
    enum game_id
    {
        GAME_FLAPPY = 0,  // Jingle Flap: fly the reindeer between the chimneys
        GAME_PINGPONG,    // Ping Pong Pals: first to 7 vs. the CPU
        GAME_SLED,        // Sled Dash: dodge trees and rocks, grab presents
        GAME_CATCH,       // Gift Catch: catch presents, avoid coal
        GAME_FISHING,     // Ice Fishing: cast, wait, reel
        GAME_HORSE,       // Pony Gallop: jump the fences
        GAME_COUNT
    };

    struct game_info
    {
        const char* title;   // name of the game, e.g. "Sled Dash"
        const char* place;   // place on the map, e.g. "Big Hill"
        void (*run)();       // plays the game (intro, play, game over, retry) and returns to the map
    };

    const game_info& game(int id);

    // Each game's entry point (src/games/<name>.cpp). They read sb::player_character().
    void flappy_run();
    void pingpong_run();
    void sled_run();
    void catch_run();
    void fishing_run();
    void horse_run();

    // Scenes (src/)
    void title_scene();
    void character_select_scene(bool from_map);
    void world_map_scene();
}

#endif
