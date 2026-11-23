// Save data in SRAM: the best score for every game (plus who set it) and settings.
// Two copies are kept so a power-off in the middle of saving can't wipe the scores.
#ifndef SB_SAVE_H
#define SB_SAVE_H

#include "bn_string.h"

namespace sb
{
    // Reads SRAM. Call once at boot (main.cpp does this).
    void save_init();

    // Best score for a game (0 if nobody has played yet) and the name of who set it ("" if none).
    int best_score(int game);
    const char* best_name(int game);

    // Records a finished game's score. Returns true (and saves) if it beat the old record.
    bool submit_score(int game, int score, int character);

    // Extra records, for games that keep more than one (Ping Pong: one per difficulty, slots 0..2).
    constexpr int extra_record_count = 8;
    int extra_score(int slot);
    const char* extra_name(int slot);
    bool submit_extra(int slot, int score, int character);

    // The best record of a game as text for the map, intro and high score screens, e.g. "1234 by Adam"
    // (Ping Pong: "Hard 5-2 by Adam"). Empty if nobody has a record yet. `compact` drops the "by" and
    // shortens words, for the high score list.
    bn::string<40> best_record_text(int game, bool compact = false);

    bool music_enabled();
    void set_music_enabled(bool enabled);   // saved

    int last_character();
    void set_last_character(int character); // saved

    // Writes the current data to SRAM (the setters above already call it).
    void save_write();
}

#endif
