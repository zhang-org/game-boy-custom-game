// Controls pages: how to play, shown as pictures instead of a long text.
//
// Each page shows a Miyoo Mini Plus (the buttons the page is about light up, and sink in while they're
// "pressed"), a close-up of the game on the Miyoo's screen acting out what the buttons do, a short list of
// labels and a tip. Left / Right flips the pages.
//
//   * Before playing: game_intro_pages() (A starts, B goes back to the map). The first time, A goes through
//     every page before it starts.
//   * In the pause menu, "How to play" shows the same pages once a game has registered them with
//     set_controls_pages() (game_intro_pages() does that for you; clear it with set_controls_pages({}, nullptr)
//     when the game ends).
//
// The game does the animation (controls_animation): it gets the Miyoo's screen rectangle (SCREEN_* below,
// Butano coordinates) and places its own backgrounds and sprites so the right part of the game shows through
// it. Anything outside the rectangle is hidden behind the page, as long as its sprites use BG priority 2 or 3
// and its backgrounds priority 2 or 3 (the page is at priority 1, its text at 0).
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

    // Shows the pages. With `intro`, A starts (returns true) and B goes back to the map (returns false);
    // `must_read` makes A go to the next page until the last one. Without `intro`, A / B / Start close it.
    bool controls_pages(bn::span<const controls_page> pages, controls_animation& animation, bool intro,
                        bool must_read);

    // The start screen of a game with controls pages (instead of game_intro()): registers them for the pause
    // menu too. Returns true to play, false to go back to the map.
    bool game_intro_pages(int game, bn::span<const controls_page> pages, controls_animation& animation);

    // The pages "How to play" in the pause menu shows (none: the game's how-to text, if it has one).
    void set_controls_pages(bn::span<const controls_page> pages, controls_animation* animation);

    // Shows the registered pages, if any (used by the pause menu). Returns false if there are none.
    bool show_controls_pages();
}

#endif
