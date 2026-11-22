// Text drawing with our own 8x16 font (graphics/ui/font.bmp, made by tools/art/make_ui.py).
//
// Icons in the font (type them as characters):
//   '@' heart   '{' star   '}' music note   '|' present   '~' snowflake
//   '>' right arrow   '<' left arrow   '^' up arrow   '_' down arrow
#ifndef SB_TEXT_H
#define SB_TEXT_H

#include "bn_sprite_ptr.h"
#include "bn_sprite_text_generator.h"
#include "bn_string_view.h"
#include "bn_vector.h"

namespace sb
{
    enum class text_color
    {
        DARK,    // dark grey with light shadow: use on white text boxes (default)
        WHITE,   // white with dark shadow: use for HUD text over game scenes
        YELLOW,  // highlights, "NEW RECORD!"
        RED,
        BLUE,
        GREEN
    };

    enum class align
    {
        LEFT, CENTER, RIGHT
    };

    // The shared text generator (our font). Prefer the sb::label helper below.
    bn::sprite_text_generator& text_generator();

    // Pixel width of a string in our font.
    int text_width(const bn::string_view& text);

    // A piece of text on screen. Keeps its sprites alive until cleared or destroyed.
    //   sb::label score_label;
    //   score_label.set(-112, -72, "Score: 10", sb::text_color::WHITE);
    // x, y are the left (or centre/right, depending on alignment) edge and the vertical middle.
    class label
    {
    public:
        void set(int x, int y, const bn::string_view& text, text_color color = text_color::DARK,
                 align alignment = align::LEFT);
        void clear();
        void set_visible(bool visible);
        bool empty() const { return _sprites.empty(); }

    private:
        bn::vector<bn::sprite_ptr, 16> _sprites;
    };

    // Writes a number into a short string (for labels).
    bn::string<16> to_text(int value);
}

#endif
