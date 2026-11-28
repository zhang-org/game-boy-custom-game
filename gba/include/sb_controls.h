// Controls pages: how to play, shown as pictures instead of a long text.
//
// Each page shows a Miyoo Mini Plus (the buttons the page is about light up, and sink in while they're
// "pressed"), a close-up of the game on the Miyoo's screen acting out what the buttons do, a short list of
// labels and a tip. Left / Right flips the pages.
//
//   * A goes to the next page; on the last page it goes on to the game. Start goes straight to the game.
//     B goes back a page; on the first page it leaves (to the map, or back to the pause menu).
//   * Before playing: game_intro_pages() shows them the first time a game is played (B goes back to the map),
//     and skips them after that.
//   * In the pause menu, "How to play" shows the same pages once a game has registered them with
//     set_controls_pages() (game_intro_pages() does that for you; clear it with set_controls_pages({}, nullptr)
//     when the game ends). There, B goes back to the pause menu.
//
// The game does the animation (controls_animation): it gets the Miyoo's screen rectangle (SCREEN_* below,
// Butano coordinates) and places its own backgrounds and sprites so the right part of the game shows through
// it. Anything outside the rectangle is hidden behind the page, as long as its sprites use BG priority 2 or 3
// and its backgrounds priority 2 or 3 (the page is at priority 1, its text at 0).
//
// SORT LAYERS: Butano allows only 16 different (BG priority, z order) pairs among all sprites at once, and the
// game's own sprites are still there (hidden) while the pages are open. So the animation should reuse pairs
// the game already uses, not new ones ("No more sprite sort layers available" otherwise). The page itself
// uses (1, 0) and the text (0, 0).
//
// Art: tools/art/make_controls.py -> graphics/ui/controls_frame (the page) and controls_buttons (lit buttons,
// label icons). The numbers below must match that script.
#ifndef SB_CONTROLS_H
#define SB_CONTROLS_H

#include "bn_span.h"

namespace sb
{
    // The Miyoo's screen on the page, in screen pixels (top-left 0, 0).
    constexpr int CONTROLS_SCREEN_X = 13;
    constexpr int CONTROLS_SCREEN_Y = 28;
    constexpr int CONTROLS_SCREEN_W = 72;
    constexpr int CONTROLS_SCREEN_H = 56;

    // The controls a page can light up / press (a bit mask).
    enum controls_key : unsigned
    {
        KEY_A = 1,
        KEY_B = 2,
        KEY_DPAD = 4,
        KEY_START = 8,
    };

    // One line in the labels list: the button's icon (0 for a follow-on line under the one above), an optional
    // red name ("A"), and the text.
    struct controls_label
    {
        unsigned key;
        const char* name;
        const char* text;
    };

    struct controls_page
    {
        const char* title;
        bn::span<const controls_label> labels;
        const char* tip;        // wrapped to fit, in blue under the labels
        unsigned lit;           // the controls this page is about (KEY_* mask)
    };

    // What the animation is pressing in a frame.
    struct controls_state
    {
        unsigned pressed = 0;   // KEY_* mask
        int dpad = 0;           // -1 left, 0 none, 1 right
    };

    // The game's side of the pages.
    class controls_animation
    {

    public:
        virtual ~controls_animation() = default;

        // The pages are opening: hide whatever of the game shouldn't show.
        virtual void open() = 0;

        // Draws frame `t` (0, 1, 2 ...) of page `page` through the Miyoo's screen and says what is pressed.
        // t starts again at 0 when the page changes.
        virtual controls_state frame(int page, int t) = 0;

        // The pages are closing: put the game back as it was.
        virtual void close() = 0;
    };

    // Shows the pages. Returns true to go on to the game (A on the last page, or Start), false for B on the
    // first page (back to the map with `intro`, back to the pause menu without).
    bool controls_pages(bn::span<const controls_page> pages, controls_animation& animation, bool intro);

    // The start screen of a game with controls pages (instead of game_intro()): shows them the first time the
    // game is played, and registers them for the pause menu. Returns true to play, false to go back to the map.
    bool game_intro_pages(int game, bn::span<const controls_page> pages, controls_animation& animation);

    // The pages "How to play" in the pause menu shows (none: the game's how-to text, if it has one).
    void set_controls_pages(bn::span<const controls_page> pages, controls_animation* animation);

    // Shows the registered pages, if any (used by the pause menu): 1 = go back to the game, 0 = back to the
    // pause menu, -1 = there are none.
    int show_controls_pages();
}

#endif
