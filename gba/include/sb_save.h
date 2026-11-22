// Save data in SRAM: the best score for every game (plus who set it) and settings.
// Two copies are kept so a power-off in the middle of saving can't wipe the scores.
#ifndef SB_SAVE_H
#define SB_SAVE_H

namespace sb
{
    // Reads SRAM. Call once at boot (main.cpp does this).
    void save_init();

    // Best score for a game (0 if nobody has played yet) and the name of who set it ("" if none).
    int best_score(int game);
    const char* best_name(int game);

    // Records a finished game's score. Returns true (and saves) if it beat the old record.
    bool submit_score(int game, int score, int character);

    bool music_enabled();
    void set_music_enabled(bool enabled);   // saved

    int last_character();
    void set_last_character(int character); // saved

    // Writes the current data to SRAM (the setters above already call it).
    void save_write();
}

#endif
