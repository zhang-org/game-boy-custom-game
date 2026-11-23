// Snowberry shared framework. Every scene and mini game uses these helpers.
//
// Screen: 240x160. Butano coordinates are centred: (0, 0) is the middle of the screen,
// x runs -120..119 and y runs -80..79.
//
// Layering rules (keep them so text boxes always show on top):
//   * BG priority 0 is reserved for the UI panel layer (text boxes, see sb_ui.h).
//   * Text sprites use BG priority 0.
//   * Game backgrounds use priorities 1..3, game sprites 1..3 (Butano's default is 3).
//   * At most 3 regular backgrounds of your own at a time (the UI panel uses the 4th).
#ifndef SB_CORE_H
#define SB_CORE_H

#include "bn_fixed.h"
#include "bn_random.h"

namespace sb
{
    // Call once per frame instead of bn::core::update(). Handles the global Select = music
    // toggle (with a small on-screen notice) and keeps the random generator moving.
    void update();

    // Runs sb::update() n times.
    void wait_frames(int n);

    // Shared random generator. Use it for gameplay randomness.
    bn::random& rng();

    // Random integer in [min, max] (inclusive).
    int rand_range(int min, int max);

    // Fades the whole screen to black (out) or from black (in) over `frames` frames.
    void fade_out(int frames = 16);
    void fade_in(int frames = 16);

    // True after fade_out() until the next fade_in().
    bool is_faded();

    // Frames since boot.
    int frame_count();

    // The character the player picked (index into sb_characters.h).
    int player_character();
    void set_player_character(int character);
}

#endif
