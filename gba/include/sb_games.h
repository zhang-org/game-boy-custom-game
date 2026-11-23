// The list of mini games and the places on the town map where they are played.
#ifndef SB_GAMES_H
#define SB_GAMES_H

#include "bn_string.h"

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

    // Ping Pong results (src/games/pingpong.cpp). A result is saved as one number that sorts correctly
    // (any win beats any loss, then the bigger margin); these turn it back into text like "5-2".
    int pingpong_result_value(int my_points, int cpu_points);
    bool pingpong_result_won(int value);
    bn::string<8> pingpong_result_text(int value);
    const char* pingpong_level_name(int level);

    // Sled Dash records (src/games/sled.cpp): best time per course, kept in sb_save extra records.
    int sled_record_slot();                     // first extra record slot (one per course: Easy, Medium, Hard)
    bn::string<12> sled_record_text(int value); // e.g. "41.3s"
    const char* sled_course_name(int course);

    // Scenes (src/)
    void title_scene();
    void character_select_scene(bool from_map);
    void world_map_scene();
}

#endif
