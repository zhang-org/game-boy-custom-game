// The walkable town map: the main menu of the game.
// Walk onto a place's red doormat to see its game and high score, press A to play.
#include "bn_bg_palettes.h"
#include "bn_camera_ptr.h"
#include "bn_color.h"
#include "bn_keypad.h"
#include "bn_optional.h"
#include "bn_regular_bg_ptr.h"
#include "bn_sprite_ptr.h"
#include "bn_string.h"
#include "bn_vector.h"

#include "bn_music_items.h"
#include "bn_regular_bg_items_world_map.h"
#include "bn_sound_items.h"

#include "sb_audio.h"
#include "sb_characters.h"
#include "sb_core.h"
#include "sb_games.h"
#include "sb_save.h"
#include "sb_snow.h"
#include "sb_text.h"
#include "sb_ui.h"
#include "sb_world_layout.h"

namespace sb
{
    namespace
    {
        constexpr int CELL = 16;
        constexpr int WALK_SPEED = 2;     // pixels per frame while walking (16 px per cell)
        constexpr int RUN_SPEED = 4;      // pixels per frame while holding B
        constexpr int HALF_MAP = 256;     // the map image is 512x512, centred on (0, 0)

        enum direction
        {
            DIR_DOWN, DIR_UP, DIR_LEFT, DIR_RIGHT
        };

        // What the townsfolk say. One line per 'N' spot in the layout, in reading order.
        constexpr const char* npc_lines[][2] = {
            { "The reindeer at the Roost", "love to fly. Try Jingle Flap!" },
            { "The Toy Workshop needs help", "catching falling presents!" },
            { "The Big Hill is perfect for", "sledding. Watch out for trees!" },
            { "The pony at the Stable is", "ready for a race!" },
            { "Isn't the big tree pretty?", "Merry Christmas!" },
            { "Psst! Press SELECT to turn", "the music on or off." },
            { "They say a golden fish lives", "in the Frozen Pond..." },
            { "The Rec Hall has a ping pong", "table. Can you beat the CPU?" },
            { "Press START to change who", "you're playing as." },
            { "I love the snow!", "Let's build a snowman later." },
        };

        constexpr int max_npcs = 10;

        // Remembered between visits so you come back where you left off.
        int g_cell_x = -1;
        int g_cell_y = -1;
        int g_dir = DIR_DOWN;

        enum class action
        {
            NONE, PLAY, CHANGE_CHARACTER
        };

        char layout_at(int x, int y)
        {
            if(x < 0 || y < 0 || x >= world_columns || y >= world_rows)
            {
                return 'T';
            }

            return world_layout[y][x];
        }

        bool solid_tile(char c)
        {
            return c == 'T' || c == '~' || c == '#' || c == 'o' || c == 'B';
        }

        int place_at(int x, int y)
        {
            char c = layout_at(x, y);
            return c >= '1' && c <= '6' ? c - '1' : -1;
        }

        bn::fixed world_x(int cell_x)
        {
            return cell_x * CELL + CELL / 2 - HALF_MAP;
        }

        bn::fixed world_y(int cell_y)
        {
            // 16x32 character sprites: feet at the bottom of the cell.
            return cell_y * CELL - HALF_MAP;
        }

        struct npc
        {
            int x, y, character, spot;
            bn::sprite_ptr sprite;
        };

        void show_high_scores()
        {
            constexpr int x = 1, y = 1, w = 28, h = 18;
            panel(x, y, w, h);
            bn::vector<label, 14> labels;
            labels.emplace_back();
            labels.back().set(0, -64, "{ High Scores {", text_color::BLUE, align::CENTER);

            for(int i = 0; i < GAME_COUNT; ++i)
            {
                int ty = -40 + i * 16;
                labels.emplace_back();
                labels.back().set(-104, ty, game(i).title);
                labels.emplace_back();

                if(best_score(i) > 0)
                {
                    bn::string<32> text(to_text(best_score(i)));
                    text.append("  ");
                    text.append(best_name(i));
                    labels.back().set(104, ty, text, text_color::RED, align::RIGHT);
                }
                else
                {
                    labels.back().set(104, ty, "---", text_color::DARK, align::RIGHT);
                }
            }

            labels.emplace_back();
            labels.back().set(0, 62, "A: OK", text_color::DARK, align::CENTER);
            update();

            while(! bn::keypad::a_pressed() && ! bn::keypad::b_pressed() && ! bn::keypad::start_pressed())
            {
                update();
            }

            sfx(bn::sound_items::sfx_back);
            panel_clear();
        }
    }

    void world_map_scene()
    {
        action next = action::NONE;
        int next_game = 0;

        if(g_cell_x < 0)
        {
            for(int y = 0; y < world_rows; ++y)
            {
                for(int x = 0; x < world_columns; ++x)
                {
                    if(world_layout[y][x] == 'P')
                    {
                        g_cell_x = x;
                        g_cell_y = y;
                    }
                }
            }
        }

        {
            bn::bg_palettes::set_transparent_color(bn::color(30, 30, 31));
            bn::camera_ptr camera = bn::camera_ptr::create(0, 0);
            bn::regular_bg_ptr map = bn::regular_bg_items::world_map.create_bg(0, 0);
            map.set_priority(3);
            map.set_camera(camera);

            const bn::sprite_item& player_item = character_sprite(player_character());
            bn::sprite_ptr player = player_item.create_sprite(world_x(g_cell_x), world_y(g_cell_y));
            player.set_camera(camera);
            player.set_bg_priority(2);

            // The rest of the family stands around town.
            bn::vector<npc, max_npcs> npcs;
            int spot = 0;
            int who = 0;

            for(int y = 0; y < world_rows; ++y)
            {
                for(int x = 0; x < world_columns; ++x)
                {
                    if(world_layout[y][x] != 'N')
                    {
                        continue;
                    }

                    if(who == player_character())
                    {
                        ++who;
                    }

                    if(who < character_count && npcs.size() < max_npcs)
                    {
                        bn::sprite_ptr sprite = character_sprite(who).create_sprite(world_x(x), world_y(y));
                        sprite.set_camera(camera);
                        sprite.set_bg_priority(2);
                        npcs.push_back({ x, y, who, spot, sprite });
                        ++who;
                    }

                    ++spot;
                }
            }

            auto npc_at = [&npcs](int x, int y) -> npc* {
                for(npc& n : npcs)
                {
                    if(n.x == x && n.y == y)
                    {
                        return &n;
                    }
                }

                return nullptr;
            };

            auto blocked = [&](int x, int y) {
                return solid_tile(layout_at(x, y)) || npc_at(x, y) != nullptr;
            };

            snowfall snow(14, 1);
            label info_place, info_game, info_best, info_hint;
            int shown_place = -2;

            int cell_x = g_cell_x;
            int cell_y = g_cell_y;
            int dir = g_dir;
            int move_left = 0;      // pixels left in the current step
            int step_count = 0;
            int bump_cooldown = 0;
            bn::fixed pos_x = world_x(cell_x);
            bn::fixed pos_y = world_y(cell_y);
            bn::fixed cam_x = 0, cam_y = 0;

            auto place_camera = [&]() {
                bn::fixed old_x = cam_x, old_y = cam_y;
                cam_x = bn::clamp(pos_x, bn::fixed(-HALF_MAP + 120), bn::fixed(HALF_MAP - 120));
                cam_y = bn::clamp(pos_y + 8, bn::fixed(-HALF_MAP + 80), bn::fixed(HALF_MAP - 80));
                camera.set_position(cam_x, cam_y);
                return bn::fixed_point(cam_x - old_x, cam_y - old_y);
            };

            place_camera();
            play_music(bn::music_items::map_song);
            fade_in();

            while(next == action::NONE)
            {
                // --- input and movement
                if(move_left == 0)
                {
                    int dx = 0, dy = 0;

                    if(bn::keypad::left_held())
                    {
                        dx = -1;
                        dir = DIR_LEFT;
                    }
                    else if(bn::keypad::right_held())
                    {
                        dx = 1;
                        dir = DIR_RIGHT;
                    }
                    else if(bn::keypad::up_held())
                    {
                        dy = -1;
                        dir = DIR_UP;
                    }
                    else if(bn::keypad::down_held())
                    {
                        dy = 1;
                        dir = DIR_DOWN;
                    }

                    if(dx || dy)
                    {
                        if(blocked(cell_x + dx, cell_y + dy))
                        {
                            if(bump_cooldown == 0)
                            {
                                sfx(bn::sound_items::sfx_bump);
                                bump_cooldown = 20;
                            }
                        }
                        else
                        {
                            cell_x += dx;
                            cell_y += dy;
                            move_left = CELL;
                            ++step_count;
                        }
                    }
                    else if(bn::keypad::a_pressed())
                    {
                        int place = place_at(cell_x, cell_y);

                        if(place >= 0)
                        {
                            sfx(bn::sound_items::sfx_door);
                            next = action::PLAY;
                            next_game = place;
                        }
                        else
                        {
                            int fx = cell_x + (dir == DIR_LEFT ? -1 : dir == DIR_RIGHT ? 1 : 0);
                            int fy = cell_y + (dir == DIR_UP ? -1 : dir == DIR_DOWN ? 1 : 0);

                            if(npc* n = npc_at(fx, fy))
                            {
                                // Turn to face the player while talking.
                                int face = dir == DIR_DOWN ? FRAME_UP : dir == DIR_UP ? FRAME_DOWN : FRAME_SIDE;
                                n->sprite.set_tiles(character_sprite(n->character).tiles_item(), face);
                                n->sprite.set_horizontal_flip(dir == DIR_RIGHT);
                                info_place.clear();
                                info_game.clear();
                                info_best.clear();
                                info_hint.clear();
                                shown_place = -2;
                                panel_clear();
                                bn::string<24> who_line(character_name(n->character));
                                who_line.append(":");
                                const char* const* line = npc_lines[n->spot % 10];
                                message(who_line, line[0], line[1]);
                                n->sprite.set_tiles(character_sprite(n->character).tiles_item(), FRAME_DOWN);
                                n->sprite.set_horizontal_flip(false);
                            }
                        }
                    }
                    else if(bn::keypad::start_pressed())
                    {
                        sfx(bn::sound_items::sfx_select);
                        info_place.clear();
                        info_game.clear();
                        info_best.clear();
                        info_hint.clear();
                        shown_place = -2;
                        panel_clear();
                        constexpr bn::string_view items[] = { "Change character", "High scores", "Close" };
                        int choice = menu(15, 1, items, 0, true);

                        if(choice == 0)
                        {
                            next = action::CHANGE_CHARACTER;
                        }
                        else if(choice == 1)
                        {
                            show_high_scores();
                        }
                    }
                }

                if(bump_cooldown > 0)
                {
                    --bump_cooldown;
                }

                if(move_left > 0)
                {
                    int speed = bn::keypad::b_held() ? RUN_SPEED : WALK_SPEED;
                    speed = bn::min(speed, move_left);
                    move_left -= speed;
                    pos_x += dir == DIR_LEFT ? -speed : dir == DIR_RIGHT ? speed : 0;
                    pos_y += dir == DIR_UP ? -speed : dir == DIR_DOWN ? speed : 0;
                }

                // --- player sprite
                int base = dir == DIR_DOWN ? FRAME_DOWN : dir == DIR_UP ? FRAME_UP : FRAME_SIDE;
                int frame = base;

                if(move_left > 0 && move_left <= CELL / 2 + 4)
                {
                    frame = base + 1 + (step_count & 1);
                }

                player.set_tiles(player_item.tiles_item(), frame);
                player.set_horizontal_flip(dir == DIR_LEFT);
                player.set_position(pos_x, pos_y);
                bn::fixed_point cam_delta = place_camera();

                // --- place info box
                int place = move_left == 0 ? place_at(cell_x, cell_y) : -1;

                if(place != shown_place && next == action::NONE)
                {
                    shown_place = place;
                    panel_clear();

                    if(place >= 0)
                    {
                        // Show the box at the bottom if the player is near the top of the screen.
                        bool bottom = pos_y - cam_y < -16;
                        int ty = bottom ? 13 : 0;
                        panel(1, ty, 28, 7);
                        int y0 = ty * 8 - 80;
                        info_place.set(-104, y0 + 12, game(place).place, text_color::BLUE);
                        info_game.set(104, y0 + 12, game(place).title, text_color::GREEN, align::RIGHT);
                        bn::string<48> best("{ Best: ");

                        if(best_score(place) > 0)
                        {
                            best.append(to_text(best_score(place)));
                            best.append(" by ");
                            best.append(best_name(place));
                        }
                        else
                        {
                            best = "{ No record yet. Be the first!";
                        }

                        info_best.set(-104, y0 + 28, best, text_color::RED);
                        info_hint.set(0, y0 + 44, "A: Play!", text_color::DARK, align::CENTER);
                    }
                    else
                    {
                        info_place.clear();
                        info_game.clear();
                        info_best.clear();
                        info_hint.clear();
                    }
                }

                snow.update(-cam_delta.x(), -cam_delta.y());
                update();
            }

            g_cell_x = cell_x;
            g_cell_y = cell_y;
            g_dir = dir;
            fade_out();
            info_place.clear();
            info_game.clear();
            info_best.clear();
            info_hint.clear();
            panel_clear();
        }

        // The map's sprites and background are gone now, so the next scene has all the memory.
        if(next == action::PLAY)
        {
            game(next_game).run();
            panel_clear();
        }
        else if(next == action::CHANGE_CHARACTER)
        {
            character_select_scene(true);
        }
    }
}
