#include "sb_text.h"

#include "bn_sprite_font.h"
#include "bn_string.h"

#include "bn_sprite_items_font.h"
#include "bn_sprite_palette_items_font_pal_blue.h"
#include "bn_sprite_palette_items_font_pal_dark.h"
#include "bn_sprite_palette_items_font_pal_green.h"
#include "bn_sprite_palette_items_font_pal_red.h"
#include "bn_sprite_palette_items_font_pal_white.h"
#include "bn_sprite_palette_items_font_pal_yellow.h"

#include "sb_font_widths.h"

namespace sb
{
    namespace
    {
        constexpr bn::sprite_font font(bn::sprite_items::font, bn::utf8_characters_map_ref(), font_widths);

        const bn::sprite_palette_item& palette_for(text_color color)
        {
            switch(color)
            {
            case text_color::WHITE:
                return bn::sprite_palette_items::font_pal_white;
            case text_color::YELLOW:
                return bn::sprite_palette_items::font_pal_yellow;
            case text_color::RED:
                return bn::sprite_palette_items::font_pal_red;
            case text_color::BLUE:
                return bn::sprite_palette_items::font_pal_blue;
            case text_color::GREEN:
                return bn::sprite_palette_items::font_pal_green;
            default:
                return bn::sprite_palette_items::font_pal_dark;
            }
        }
    }

    bn::sprite_text_generator& text_generator()
    {
        static bn::sprite_text_generator generator(font);
        return generator;
    }

    int text_width(const bn::string_view& text)
    {
        return text_generator().width(text);
    }

    void label::set(int x, int y, const bn::string_view& text, text_color color, align alignment)
    {
        _sprites.clear();

        bn::sprite_text_generator& generator = text_generator();
        generator.set_palette_item(palette_for(color));
        generator.set_bg_priority(0);

        switch(alignment)
        {
        case align::CENTER:
            generator.set_center_alignment();
            break;
        case align::RIGHT:
            generator.set_right_alignment();
            break;
        default:
            generator.set_left_alignment();
            break;
        }

        generator.generate(x, y, text, _sprites);
        generator.set_left_alignment();
    }

    void label::clear()
    {
        _sprites.clear();
    }

    void label::set_visible(bool visible)
    {
        for(bn::sprite_ptr& sprite : _sprites)
        {
            sprite.set_visible(visible);
        }
    }

    bn::string<16> to_text(int value)
    {
        return bn::to_string<16>(value);
    }
}
