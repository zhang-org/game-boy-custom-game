#include "sb_ui.h"

#include "bn_bg_tiles.h"
#include "bn_keypad.h"
#include "bn_optional.h"
#include "bn_regular_bg_item.h"
#include "bn_regular_bg_map_cell_info.h"
#include "bn_regular_bg_map_item.h"
#include "bn_regular_bg_map_ptr.h"
#include "bn_regular_bg_ptr.h"
#include "bn_string.h"

#include "bn_bg_palette_items_box_palette.h"
#include "bn_music_items.h"
#include "bn_regular_bg_tiles_items_box_tiles.h"
#include "bn_sound_items.h"

#include "sb_audio.h"
#include "sb_core.h"
#include "sb_games.h"
#include "sb_save.h"
#include "sb_text.h"

namespace sb
{
    namespace
    {
        constexpr int columns = 32;
        constexpr int rows = 32;

        enum box_tile
        {
            T_EMPTY = 0, T_TL, T_TOP, T_TR, T_LEFT, T_FILL, T_RIGHT, T_BL, T_BOTTOM, T_BR, T_CREAM, T_HOLLY
        };

        struct panel_map
        {
            alignas(int) bn::regular_bg_map_cell cells[columns * rows] = {};
            bn::regular_bg_map_item map_item;

            panel_map() :
                map_item(cells[0], bn::size(columns, rows))
            {
            }
        };

        panel_map g_map;
        bn::optional<bn::regular_bg_ptr> g_bg;
        bn::optional<bn::regular_bg_map_ptr> g_bg_map;

        void set_cell(int x, int y, int tile)
        {
            if(x < 0 || y < 0 || x >= columns || y >= rows)
            {
                return;
            }

            bn::regular_bg_map_cell& cell = g_map.cells[g_map.map_item.cell_index(x, y)];
            bn::regular_bg_map_cell_info info(cell);
            info.set_tile_index(tile);
            info.set_palette_id(0);
            info.set_horizontal_flip(false);
            info.set_vertical_flip(false);
            cell = info.cell();
        }

        void commit()
        {
            g_bg_map->reload_cells_ref();
        }

        void clear_rect(int x, int y, int w, int h)
        {
            for(int yy = y; yy < y + h; ++yy)
            {
                for(int xx = x; xx < x + w; ++xx)
                {
                    set_cell(xx, yy, T_EMPTY);
                }
            }

            commit();
        }

        // Screen position (Butano coordinates) of the middle of a text line that starts at a tile.
        int text_x(int tile_x)
        {
            return tile_x * 8 - 120;
        }

        int text_y(int tile_y)
        {
            return tile_y * 8 - 80 + 8;
        }

        bool confirm_pressed()
        {
            return bn::keypad::a_pressed();
        }
    }

    void ui_init()
    {
        if(g_bg)
        {
            return;
        }

        bn::bg_tiles::set_allow_offset(false);
        bn::regular_bg_item item(bn::regular_bg_tiles_items::box_tiles, bn::bg_palette_items::box_palette,
                                 g_map.map_item);
        // A 256x256 background at (8, 48) puts map tile (0, 0) at the top-left corner of the screen.
        g_bg = item.create_bg(8, 48);
        g_bg->set_priority(0);
        g_bg_map = g_bg->map();
        bn::bg_tiles::set_allow_offset(true);
    }

    void ui_release()
    {
        g_bg_map.reset();
        g_bg.reset();
    }

    void panel(int x, int y, int w, int h)
    {
        ui_init();

        for(int yy = y; yy < y + h; ++yy)
        {
            for(int xx = x; xx < x + w; ++xx)
            {
                int tile = T_FILL;
                bool top = yy == y;
                bool bottom = yy == y + h - 1;
                bool left = xx == x;
                bool right = xx == x + w - 1;

                if(top)
                {
                    tile = left ? T_HOLLY : right ? T_TR : T_TOP;
                }
                else if(bottom)
                {
                    tile = left ? T_BL : right ? T_BR : T_BOTTOM;
                }
                else if(left)
                {
                    tile = T_LEFT;
                }
                else if(right)
                {
                    tile = T_RIGHT;
                }

                set_cell(xx, yy, tile);
            }
        }

        commit();
    }

    void panel_clear()
    {
        if(g_bg)
        {
            clear_rect(0, 0, columns, rows);
        }
    }

    int menu(int x, int y, const bn::span<const bn::string_view>& items, int start, bool allow_cancel)
    {
        int count = items.size();
        int widest = 0;

        for(const bn::string_view& item : items)
        {
            widest = bn::max(widest, text_width(item));
        }

        int w = (widest + 7) / 8 + 4;
        int h = count * 2 + 2;

        // Keep the whole box on the 30x20 tile screen.
        x = bn::max(0, bn::min(x, 30 - w));
        y = bn::max(0, bn::min(y, 20 - h));
        panel(x, y, w, h);

        bn::vector<label, 8> labels;

        for(int i = 0; i < count && i < 8; ++i)
        {
            labels.emplace_back();
            labels.back().set(text_x(x + 2), text_y(y + 1 + i * 2), items[i]);
        }

        label cursor;
        int selected = start;
        int result = -1;
        bool done = false;

        while(! done)
        {
            cursor.set(text_x(x) + 8, text_y(y + 1 + selected * 2), ">", text_color::RED);
            update();

            if(bn::keypad::up_pressed())
            {
                selected = (selected + count - 1) % count;
                sfx(bn::sound_items::sfx_select);
            }
            else if(bn::keypad::down_pressed())
            {
                selected = (selected + 1) % count;
                sfx(bn::sound_items::sfx_select);
            }
            else if(confirm_pressed())
            {
                sfx(bn::sound_items::sfx_confirm);
                result = selected;
                done = true;
            }
            else if(allow_cancel && bn::keypad::b_pressed())
            {
                sfx(bn::sound_items::sfx_back);
                done = true;
            }
        }

        clear_rect(x, y, w, h);
        return result;
    }

    void message(const bn::string_view& line1, const bn::string_view& line2, const bn::string_view& line3)
    {
        constexpr int x = 1, y = 13, w = 28, h = 7;
        panel(x, y, w, h);

        label l1, l2, l3, arrow;
        l1.set(text_x(x + 1) + 4, text_y(y + 1) - 2, line1);
        l2.set(text_x(x + 1) + 4, text_y(y + 3) - 4, line2);
        l3.set(text_x(x + 1) + 4, text_y(y + 5) - 6, line3);

        // Ignore the button press that opened this message.
        update();

        for(int t = 0; ; ++t)
        {
            if((t / 16) % 2 == 0)
            {
                arrow.set(text_x(x + w - 2), text_y(y + 5) - 4, "_", text_color::RED);
            }
            else
            {
                arrow.clear();
            }

            update();

            if(bn::keypad::a_pressed() || bn::keypad::b_pressed())
            {
                sfx(bn::sound_items::sfx_select);
                break;
            }
        }

        clear_rect(x, y, w, h);
    }

    namespace
    {
        // The instructions of the game being played (for "How to play" in the pause menu).
        bn::span<const bn::string_view> g_howto;
        int g_howto_game = -1;

        constexpr int HOWTO_VISIBLE = 6;        // lines shown at once
        constexpr int HOWTO_LINE_Y = -30;       // screen y of the first visible line
        constexpr int HOWTO_LINE_STEP = 13;

        // A scrolling block of instructions inside the box at tile (x, y, w). Lines starting with '#' are
        // headings (blue). Returns how far down it is scrolled.
        struct howto_text
        {
            bn::span<const bn::string_view> lines;
            int x;
            int top = 0;
            label shown[HOWTO_VISIBLE];
            label up_arrow, down_arrow;

            int max_top() const
            {
                return bn::max(0, int(lines.size()) - HOWTO_VISIBLE);
            }

            bool at_bottom() const
            {
                return top >= max_top();
            }

            void draw()
            {
                for(int i = 0; i < HOWTO_VISIBLE; ++i)
                {
                    int index = top + i;

                    if(index < lines.size())
                    {
                        bn::string_view line = lines[index];
                        bool heading = ! line.empty() && line[0] == '#';

                        if(heading)
                        {
                            line.remove_prefix(1);
                        }

                        if(line.empty())
                        {
                            shown[i].clear();
                        }
                        else
                        {
                            shown[i].set(text_x(x + 1) + 4, HOWTO_LINE_Y + i * HOWTO_LINE_STEP, line,
                                         heading ? text_color::BLUE : text_color::DARK);
                        }
                    }
                    else
                    {
                        shown[i].clear();
                    }
                }
            }

            // Up/Down scroll (holding repeats). Returns true if it moved.
            bool update_scroll(int t)
            {
                int old = top;
                bool repeat = t % 6 == 0;

                if(bn::keypad::down_pressed() || (bn::keypad::down_held() && repeat))
                {
                    top = bn::min(top + 1, max_top());
                }
                else if(bn::keypad::up_pressed() || (bn::keypad::up_held() && repeat))
                {
                    top = bn::max(top - 1, 0);
                }

                if(old != top)
                {
                    sfx(bn::sound_items::sfx_select);
                    draw();
                }

                // Blinking arrows show that there is more above / below.
                bool blink = (t / 16) % 2 == 0;
                int ax = text_x(x) + 8 * 23;

                if(top > 0 && blink)
                {
                    up_arrow.set(ax, HOWTO_LINE_Y - 4, "^", text_color::RED);
                }
                else
                {
                    up_arrow.clear();
                }

                if(! at_bottom() && blink)
                {
                    down_arrow.set(ax, HOWTO_LINE_Y + (HOWTO_VISIBLE - 1) * HOWTO_LINE_STEP + 4, "_", text_color::RED);
                }
                else
                {
                    down_arrow.clear();
                }

                return old != top;
            }
        };
    }

    bool game_intro(int game_index, const bn::span<const bn::string_view>& howto)
    {
        g_howto = howto;
        g_howto_game = game_index;

        const game_info& info = game(game_index);
        constexpr int x = 2, y = 1, w = 26, h = 18;
        panel(x, y, w, h);

        if(is_faded())
        {
            fade_in();
        }

        label title, best, start;
        title.set(0, text_y(y + 1), info.title, text_color::BLUE, align::CENTER);

        bn::string<48> best_text;
        bn::string<40> record = best_record_text(game_index);

        if(! record.empty())
        {
            best_text.append("{ Best: ");
            best_text.append(record);
        }
        else
        {
            best_text.append(info.place);
        }

        best.set(0, text_y(y + 3) - 6, best_text, record.empty() ? text_color::GREEN : text_color::RED, align::CENTER);

        howto_text text;
        text.lines = howto;
        text.x = x;
        text.draw();

        // The very first time, read it all: A only works once you have scrolled to the bottom.
        bool must_read = ! howto_seen(game_index);
        update();
        bool result = false;

        for(int t = 0; ; ++t)
        {
            text.update_scroll(t);
            bool can_start = ! must_read || text.at_bottom();

            if(t % 40 == 0)
            {
                start.set(0, text_y(y + 16) - 2, can_start ? "A: Start    B: Map" : "_ Scroll down to read  B: Map",
                          text_color::DARK, align::CENTER);
            }
            else if(t % 40 == 30 && can_start)
            {
                start.clear();
            }

            update();

            if(can_start && (bn::keypad::a_pressed() || bn::keypad::start_pressed()))
            {
                sfx(bn::sound_items::sfx_confirm);

                if(must_read)
                {
                    set_howto_seen(game_index);
                }

                result = true;
                break;
            }

            if(bn::keypad::b_pressed())
            {
                sfx(bn::sound_items::sfx_back);
                break;
            }
        }

        clear_rect(x, y, w, h);
        return result;
    }

    // "How to play" from the pause menu: the same instructions, scrollable, B or A to close.
    void show_howto()
    {
        if(g_howto.empty())
        {
            return;
        }

        constexpr int x = 2, y = 1, w = 26, h = 18;
        panel(x, y, w, h);
        label title, hint;
        title.set(0, text_y(y + 1), "How to play", text_color::BLUE, align::CENTER);
        hint.set(0, text_y(y + 16) - 2, "^_ Scroll    B: Back", text_color::DARK, align::CENTER);

        howto_text text;
        text.lines = g_howto;
        text.x = x;
        text.draw();
        update();

        for(int t = 0; ; ++t)
        {
            text.update_scroll(t);
            update();

            if(bn::keypad::b_pressed() || bn::keypad::a_pressed() || bn::keypad::start_pressed())
            {
                sfx(bn::sound_items::sfx_back);
                break;
            }
        }

        clear_rect(x, y, w, h);
    }

    bool game_pause()
    {
        sfx(bn::sound_items::sfx_select);

        while(true)
        {
            constexpr int x = 9, y = 3, w = 12, h = 3;
            panel(x, y, w, h);
            label title;
            title.set(0, text_y(y) + 4, "Paused", text_color::BLUE, align::CENTER);

            constexpr bn::string_view items[] = { "Continue", "How to play", "Quit to map" };
            int choice = menu(8, 7, items, 0, true);
            title.clear();
            clear_rect(x, y, w, h);

            if(choice == 1)
            {
                show_howto();
                continue;
            }

            update();
            return choice == 2;
        }
    }

    bool game_over(int game_index, int score, const bn::string_view& detail)
    {
        bool record = submit_score(game_index, score, player_character());
        play_jingle(record ? bn::music_items::jingle_record : bn::music_items::jingle_over);

        constexpr int x = 3, y = 1, w = 24, h = 12;
        panel(x, y, w, h);

        label title, detail_label, score_label, best_label, record_label;
        title.set(0, text_y(y + 1), "Game Over", text_color::BLUE, align::CENTER);

        if(! detail.empty())
        {
            detail_label.set(0, text_y(y + 3), detail, text_color::DARK, align::CENTER);
        }

        bn::string<32> score_text("Score: ");
        score_text.append(to_text(score));
        score_label.set(0, text_y(y + 5), score_text, text_color::DARK, align::CENTER);

        bn::string<48> best_text("{ Best: ");
        bn::string<40> best_record = best_record_text(game_index);

        if(! best_record.empty())
        {
            best_text.append(best_record);
        }
        else
        {
            best_text = "No record yet";
        }

        best_label.set(0, text_y(y + 7), best_text, text_color::RED, align::CENTER);

        // Let the result sink in before the menu appears (and swallow buttons mashed during play).
        for(int t = 0; t < 50; ++t)
        {
            if(record && t % 20 == 0)
            {
                record_label.set(0, text_y(y + 9), "{ NEW RECORD! {", text_color::YELLOW, align::CENTER);
            }

            update();
        }

        if(record)
        {
            sfx(bn::sound_items::sfx_record);
        }

        constexpr bn::string_view items[] = { "Play again", "Back to map" };
        int choice = 0;

        while(true)
        {
            choice = menu(8, 13, items, 0, true);

            if(choice >= 0)
            {
                break;
            }

            choice = 1;
            break;
        }

        clear_rect(x, y, w, h);
        return choice == 0;
    }
}
