#include "sb_controls.h"

#include "bn_keypad.h"
#include "bn_optional.h"
#include "bn_regular_bg_ptr.h"
#include "bn_sprite_ptr.h"
#include "bn_string.h"
#include "bn_string_view.h"
#include "bn_vector.h"

#include "bn_regular_bg_items_controls_frame.h"
#include "bn_sound_items.h"
#include "bn_sprite_items_controls_buttons.h"

#include "sb_audio.h"
#include "sb_core.h"
#include "sb_save.h"
#include "sb_text.h"
#include "sb_ui.h"

namespace sb
{
    namespace
    {
        // ---- layout (screen pixels; must match tools/art/make_controls.py)
        struct point
        {
            int x, y;
        };

        constexpr point CTRL_A = { 80, 117 };
        constexpr point CTRL_B = { 69, 128 };
        constexpr point CTRL_DPAD = { 27, 118 };
        constexpr point CTRL_START = { 48, 144 };

        // controls_buttons frames
        enum button_frame
        {
            F_A_LIT = 0, F_A_PRESSED, F_B_LIT, F_B_PRESSED, F_DPAD_LIT, F_DPAD_LEFT, F_DPAD_RIGHT,
            F_START_LIT, F_START_PRESSED, F_ICON_A, F_ICON_A_GLOW, F_ICON_B, F_ICON_B_GLOW,
            F_ICON_DPAD, F_ICON_DPAD_GLOW, F_ICON_START, F_ICON_START_GLOW, F_DIVIDER
        };

        constexpr int LABELS_TOP = 23;          // first label line (top of its 16 px row)
        constexpr int LABEL_STEP = 14;
        constexpr int TIP_X = 168;              // the tip is centred here ...
        constexpr int TIP_WIDTH = 126;          // ... and wrapped to this width
        constexpr int PANEL_BOTTOM = 156;

        int bx(int screen_x)
        {
            return screen_x - 120;
        }

        int by(int screen_y)
        {
            return screen_y - 80;
        }

        // A label line whose 16 px row starts at screen y `top`.
        int text_mid(int top)
        {
            return by(top) + 8;
        }

        // Splits `text` into lines no wider than `width` pixels.
        bn::vector<bn::string<48>, 8> wrap(const char* text, int width)
        {
            bn::vector<bn::string<48>, 8> lines;
            bn::string<48> line;
            bn::string_view rest(text);

            while(! rest.empty())
            {
                int space = 0;

                while(space < rest.size() && rest[space] != ' ')
                {
                    ++space;
                }

                bn::string_view word = rest.substr(0, space);
                rest.remove_prefix(bn::min(space + 1, rest.size()));

                bn::string<48> attempt(line);

                if(! attempt.empty())
                {
                    attempt.push_back(' ');
                }

                attempt.append(word);

                if(! line.empty() && text_width(attempt) > width && ! lines.full())
                {
                    lines.push_back(line);
                    line = word;
                }
                else
                {
                    line = attempt;
                }
            }

            if(! line.empty() && ! lines.full())
            {
                lines.push_back(line);
            }

            return lines;
        }

        // Everything on screen for one page.
        struct page_view
        {
            bn::vector<bn::sprite_ptr, 4> device;           // the lit buttons on the Miyoo
            unsigned device_keys[4] = {};
            bn::vector<bn::sprite_ptr, 6> icons;            // label icons
            unsigned icon_keys[6] = {};
            bn::vector<bn::sprite_ptr, 4> divider;
            bn::vector<label, 20> labels;
            label title, number, left_arrow, right_arrow, hint;

            bn::sprite_ptr make(int frame, int screen_x, int screen_y)
            {
                bn::sprite_ptr s = bn::sprite_items::controls_buttons.create_sprite(bx(screen_x), by(screen_y), frame);
                s.set_bg_priority(1);
                return s;
            }

            void show(const controls_page& page, int index, int count, const char* hint_text)
            {
                device.clear();
                icons.clear();
                divider.clear();
                labels.clear();

                // the lit controls on the Miyoo
                const struct { unsigned key; point at; int frame; } lit[] = {
                    { KEY_A, CTRL_A, F_A_LIT }, { KEY_B, CTRL_B, F_B_LIT },
                    { KEY_DPAD, CTRL_DPAD, F_DPAD_LIT }, { KEY_START, CTRL_START, F_START_LIT },
                };

                for(const auto& l : lit)
                {
                    if(page.lit & l.key)
                    {
                        device_keys[device.size()] = l.key;
                        device.push_back(make(l.frame, l.at.x, l.at.y));
                    }
                }

                // title bar: "< Serve      A: Play  B: Map      1/4 >"
                left_arrow.set(bx(4), text_mid(0), "<", text_color::RED);
                title.set(bx(14), text_mid(0), page.title, text_color::BLUE);
                bn::string<8> n = to_text(index + 1);
                n.append("/");
                n.append(to_text(count));
                number.set(bx(229), text_mid(0), n, text_color::DARK, align::RIGHT);
                // the hint goes in the middle of the space left between the title and the page number
                int gap_left = 14 + text_width(page.title);
                int gap_right = 229 - text_width(n);
                hint.set(bx((gap_left + gap_right) / 2), text_mid(0), hint_text, text_color::DARK, align::CENTER);
                right_arrow.set(bx(236), text_mid(0), ">", text_color::RED, align::RIGHT);

                // the labels
                int y = LABELS_TOP;
                int indent = 118;

                for(const controls_label& l : page.labels)
                {
                    int x;

                    if(l.key == KEY_DPAD)
                    {
                        y += 2;
                        icon_keys[icons.size()] = l.key;
                        icons.push_back(make(F_ICON_DPAD, 106 + 8, y + 8));
                        x = indent = 130;
                    }
                    else if(l.key)
                    {
                        int frame = l.key == KEY_A ? F_ICON_A : l.key == KEY_B ? F_ICON_B : F_ICON_START;
                        icon_keys[icons.size()] = l.key;
                        icons.push_back(make(frame, l.key == KEY_START ? 104 + 4 : 104 + 5, y + 8));
                        x = indent = 118;
                    }
                    else
                    {
                        x = indent;
                    }

                    if(l.name && l.name[0])
                    {
                        labels.emplace_back();
                        labels.back().set(bx(x), text_mid(y), l.name, text_color::RED);
                        x += text_width(l.name) + 4;
                    }

                    if(l.text && l.text[0])
                    {
                        labels.emplace_back();
                        labels.back().set(bx(x), text_mid(y), l.text, text_color::DARK);
                    }

                    y += LABEL_STEP;
                }

                // a line, then the tip
                y += 3;

                for(int i = 0; i < 4; ++i)
                {
                    divider.push_back(make(F_DIVIDER, 122 + i * 32, y));
                }

                y += 1;
                auto lines = wrap(page.tip, TIP_WIDTH);
                int step = y + (lines.size() - 1) * 13 + 13 <= PANEL_BOTTOM ? 13 : 11;

                for(const auto& line : lines)
                {
                    if(labels.full())
                    {
                        break;
                    }

                    labels.emplace_back();
                    labels.back().set(bx(TIP_X), text_mid(y), line, text_color::BLUE, align::CENTER);
                    y += step;
                }
            }

            // Pressed buttons sink in, and their label icons glow.
            void press(const controls_state& state)
            {
                for(int i = 0; i < device.size(); ++i)
                {
                    unsigned key = device_keys[i];
                    bool down = state.pressed & key;
                    int frame;

                    switch(key)
                    {
                    case KEY_A:
                        frame = down ? F_A_PRESSED : F_A_LIT;
                        break;
                    case KEY_B:
                        frame = down ? F_B_PRESSED : F_B_LIT;
                        break;
                    case KEY_DPAD:
                        frame = state.dpad < 0 ? F_DPAD_LEFT : state.dpad > 0 ? F_DPAD_RIGHT : F_DPAD_LIT;
                        break;
                    default:
                        frame = down ? F_START_PRESSED : F_START_LIT;
                        break;
                    }

                    device[i].set_tiles(bn::sprite_items::controls_buttons.tiles_item(), frame);
                }

                for(int i = 0; i < icons.size(); ++i)
                {
                    unsigned key = icon_keys[i];
                    bool on = (state.pressed & key) || (key == KEY_DPAD && state.dpad != 0);
                    int frame = key == KEY_A ? F_ICON_A : key == KEY_B ? F_ICON_B :
                                key == KEY_DPAD ? F_ICON_DPAD : F_ICON_START;
                    icons[i].set_tiles(bn::sprite_items::controls_buttons.tiles_item(), frame + (on ? 1 : 0));
                }
            }
        };

        bn::span<const controls_page> g_pages;
        controls_animation* g_animation = nullptr;
    }

    bool controls_pages(bn::span<const controls_page> pages, controls_animation& animation, bool intro,
                        bool must_read)
    {
        if(pages.empty())
        {
            return true;
        }

        panel_clear();
        bn::regular_bg_ptr frame = bn::regular_bg_items::controls_frame.create_bg(8, 48);
        frame.set_priority(1);
        animation.open();

        int page = 0;
        bool result = false;
        bool redraw = true;
        page_view view;

        for(int t = 0; ; ++t)
        {
            if(redraw)
            {
                redraw = false;
                t = 0;
                bool last = page == pages.size() - 1;
                const char* hint = ! intro ? "B: Back" : must_read && ! last ? "A: Next  B: Map" : "A: Play  B: Map";
                view.show(pages[page], page, pages.size(), hint);
            }

            view.press(animation.frame(page, t));

            if(t == 0 && is_faded())
            {
                update();
                fade_in();
            }

            update();

            if(bn::keypad::left_pressed() || bn::keypad::right_pressed())
            {
                int n = pages.size();
                page = (page + (bn::keypad::left_pressed() ? n - 1 : 1)) % n;
                sfx(bn::sound_items::sfx_select);
                redraw = true;
            }
            else if(intro && (bn::keypad::a_pressed() || bn::keypad::start_pressed()))
            {
                if(must_read && page < pages.size() - 1)
                {
                    ++page;
                    sfx(bn::sound_items::sfx_select);
                    redraw = true;
                }
                else
                {
                    sfx(bn::sound_items::sfx_confirm);
                    result = true;
                    break;
                }
            }
            else if(bn::keypad::b_pressed() || (! intro && (bn::keypad::a_pressed() || bn::keypad::start_pressed())))
            {
                sfx(bn::sound_items::sfx_back);
                break;
            }
        }

        animation.close();
        return result;
    }

    bool game_intro_pages(int game, bn::span<const controls_page> pages, controls_animation& animation)
    {
        set_controls_pages(pages, &animation);
        bool must_read = ! howto_seen(game);
        bool play = controls_pages(pages, animation, true, must_read);

        if(play && must_read)
        {
            set_howto_seen(game);
        }

        return play;
    }

    void set_controls_pages(bn::span<const controls_page> pages, controls_animation* animation)
    {
        g_pages = animation ? pages : bn::span<const controls_page>();
        g_animation = animation;
    }

    bool show_controls_pages()
    {
        if(g_pages.empty() || ! g_animation)
        {
            return false;
        }

        controls_pages(g_pages, *g_animation, false, false);
        return true;
    }
}
