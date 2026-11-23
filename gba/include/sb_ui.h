// Text boxes, menus and the shared screens every mini game uses (intro, pause, game over).
// The boxes are drawn on a dedicated BG layer at priority 0 that sits above everything else.
#ifndef SB_UI_H
#define SB_UI_H

#include "bn_span.h"
#include "bn_string_view.h"

namespace sb
{
    // Creates the UI layer if needed. Called automatically by the functions below.
    void ui_init();

    // Draws a box in tile units (8x8 px; the screen is 30x20 tiles, (0, 0) is the top-left tile).
    void panel(int tile_x, int tile_y, int tiles_w, int tiles_h);

    // Removes every box.
    void panel_clear();

    // Hides the UI layer (and frees its background) until the next panel() call.
    // Call before leaving a scene if you need all 4 background layers.
    void ui_release();

    // Shows a vertical menu inside a box and waits for a choice. Returns the chosen index,
    // or -1 if `allow_cancel` is true and the player presses B.
    int menu(int tile_x, int tile_y, const bn::span<const bn::string_view>& items, int start = 0,
             bool allow_cancel = true);

    // Shows a message box at the bottom of the screen with up to 3 lines and waits for A (or B).
    void message(const bn::string_view& line1, const bn::string_view& line2 = "",
                 const bn::string_view& line3 = "");

    // ---- Shared mini game screens ----

    // Title card + how-to-play box shown over the game's (already drawn) scene.
    // `howto` has up to 4 short lines (about 26 characters each).
    // Returns true when the player presses A to start, false if they press B (back to the map).
    bool game_intro(int game, const bn::span<const bn::string_view>& howto);

    // Call when the player presses Start during play. Shows "Paused" with Continue / Quit.
    // Returns true if the player chose to quit to the map.
    bool game_pause();

    // Records the score, shows the result box (score, best score + name, NEW RECORD!) and asks
    // "Play again" / "Back to map". `detail` is an optional extra line (e.g. "You won 7-4!").
    // Returns true to play again.
    bool game_over(int game, int score, const bn::string_view& detail = "");
}

#endif
