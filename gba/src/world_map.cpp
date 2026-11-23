// The walkable town map: the main menu of the game.
// Walk onto a place's red doormat to see its game and high score, press A to play.
#include "bn_bg_palettes.h"
#include "bn_blending.h"
#include "bn_camera_ptr.h"
#include "bn_color.h"
#include "bn_keypad.h"
#include "bn_optional.h"
#include "bn_regular_bg_ptr.h"
#include "bn_span.h"
#include "bn_sprite_ptr.h"
#include "bn_string.h"
#include "bn_string_view.h"
#include "bn_vector.h"

#include "bn_music_items.h"
#include "bn_regular_bg_items_world_map.h"
#include "bn_regular_bg_items_world_top.h"
#include "bn_sound_items.h"
#include "bn_sprite_items_world_fx.h"
#include "bn_sprite_items_world_horse.h"
#include "bn_sprite_items_world_prints.h"
#include "bn_sprite_items_world_reindeer.h"

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

        // What the townsfolk say. One line per spot in world_npc_spots (sb_world_layout.h), in the same
        // order (reading order: top to bottom, left to right).
        constexpr const char* npc_lines[world_npc_spot_count][2] = {
            { "The reindeer at the Roost", "love to fly. Try Jingle Flap!" },   // (7, 8)
            { "The Toy Workshop needs help", "catching falling presents!" },    // (20, 8)
            { "The Big Hill is perfect for", "sledding. Watch out for trees!" },// (24, 9)
            { "Isn't the big tree pretty?", "Merry Christmas!" },               // (18, 12)
            { "I just love ponies! Go see", "the one at the Stable." },          // (4, 16)    Ella
            { "I could ride all day. Try", "Pony Gallop at the Stable!" },       // (7, 16)    Alyssa
            { "Press START to change who", "you're playing as." },              // (7, 19)
            { "The Rec Hall has a ping pong", "table. Can you beat the CPU?" }, // (24, 19)
            { "They say a golden fish lives", "in the Frozen Pond..." },        // (11, 21)   Billy
            { "Psst! Press SELECT to turn", "the music on or off." },           // (20, 25)
        };

        constexpr int max_npcs = world_npc_spot_count;

        // Draw order. The town picture has two layers: the ground, and a "top" layer with the trees, houses,
        // lamps and snowmen. Sprites normally sit between the two (so they are in front of the ground but
        // behind the top layer). A person standing lower on the screen than a tree is in front of it, so
        // they hop in front of the top layer (BG priority 2 -> they win the tie with the top layer).
        constexpr int PRIORITY_IN_FRONT = 2;
        constexpr int PRIORITY_BEHIND = 3;
        constexpr int TOP_LAYER_PRIORITY = 2;
        constexpr int GROUND_PRIORITY = 3;

        // The feet of the 16x32 sprites sit at the bottom of their cell, i.e. 16 px below the sprite's centre.
        constexpr int FEET_OFFSET = 16;

        // True when a person standing at (centre x, sprite centre y) is behind something on the top layer.
        bool is_behind_something(const bn::fixed& x, const bn::fixed& y)
        {
            int x0 = x.right_shift_integer() + HALF_MAP - 5;
            int x1 = x0 + 10;
            int feet = y.right_shift_integer() + HALF_MAP + FEET_OFFSET;

            for(const world_object& o : world_objects)
            {
                if(x0 < o.x1 && x1 > o.x0 && feet > o.top && feet < o.base)
                {
                    return true;
                }
            }

            return false;
        }

        // Puts a sprite in the right place in the draw order for where it stands.
        void sort_sprite(bn::sprite_ptr& sprite, bool behind)
        {
            sprite.set_bg_priority(behind ? PRIORITY_BEHIND : PRIORITY_IN_FRONT);
            // Lower on the screen = in front of the others (z order: lower is drawn on top).
            int feet = sprite.y().right_shift_integer() + HALF_MAP + FEET_OFFSET;
            sprite.set_z_order(-(feet / CELL));
        }

        // The horse in the paddock and the reindeer outside the Roost.
        constexpr int HORSE_X = 10 * CELL - HALF_MAP;      // centre of the paddock (cells 9..10 x 13..14)
        constexpr int HORSE_Y = 14 * CELL - 6 - HALF_MAP;  // a little higher, so you stand in front of it
        constexpr int FOOTPRINT_POOL = 10;                 // how many snow footprints exist at once
        constexpr int FOOTPRINT_FRESH_STEPS = 5;           // after this many more steps they turn pale ...
        constexpr int FOOTPRINT_GONE_STEPS = 10;           // ... and after this many they disappear

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
            return c == 'T' || c == '~' || c == '#' || c == 'o' || c == 'B' || c == 'H';
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

        struct footprint
        {
            bn::sprite_ptr sprite;
            int born;           // the step count when it was made
            bool wide;
        };

        bool same_text(const char* a, const char* b)
        {
            while(*a && *a == *b)
            {
                ++a;
                ++b;
            }

            return *a == *b;
        }

        // Gives every person standing around town a spot: people with a favourite place stand near it,
        // everyone else fills the other spots in order. -1 = nobody there (that is you).
        void assign_people(int player, int (&who_at_spot)[world_npc_spot_count])
        {
            bool used[character_count] = {};
            used[player] = true;

            for(int s = 0; s < world_npc_spot_count; ++s)
            {
                who_at_spot[s] = -1;
                const char* fav = world_npc_spots[s].favourite;

                if(fav)
                {
                    for(int c = 0; c < character_count; ++c)
                    {
                        if(same_text(character_name(c), fav) && ! used[c])
                        {
                            who_at_spot[s] = c;
                            used[c] = true;
                        }
                    }
                }
            }

            int next = 0;

            for(int s = 0; s < world_npc_spot_count; ++s)
            {
                if(world_npc_spots[s].favourite)
                {
                    continue;
                }

                while(next < character_count && used[next])
                {
                    ++next;
                }

                if(next < character_count)
                {
                    who_at_spot[s] = next;
                    used[next] = true;
                }
            }
        }

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
            map.set_priority(GROUND_PRIORITY);
            map.set_camera(camera);
            bn::regular_bg_ptr top_layer = bn::regular_bg_items::world_top.create_bg(0, 0);
            top_layer.set_priority(TOP_LAYER_PRIORITY);
            top_layer.set_camera(camera);

            const bn::sprite_item& player_item = character_sprite(player_character());
            bn::sprite_ptr player = player_item.create_sprite(world_x(g_cell_x), world_y(g_cell_y));
            player.set_camera(camera);
            sort_sprite(player, is_behind_something(player.x(), player.y()));

            // The rest of the family stands around town (each near the place they like best).
            bn::vector<npc, max_npcs> npcs;
            int who_at_spot[world_npc_spot_count];
            assign_people(player_character(), who_at_spot);

            for(int s = 0; s < world_npc_spot_count; ++s)
            {
                if(who_at_spot[s] >= 0)
                {
                    int x = world_npc_spots[s].x;
                    int y = world_npc_spots[s].y;
                    bn::sprite_ptr sprite = character_sprite(who_at_spot[s]).create_sprite(world_x(x), world_y(y));
                    sprite.set_camera(camera);
                    sort_sprite(sprite, is_behind_something(sprite.x(), sprite.y()));
                    npcs.push_back({ x, y, who_at_spot[s], s, sprite });
                }
            }

            // The pony grazing in the paddock, and the reindeer outside the Roost.
            bn::sprite_ptr horse = bn::sprite_items::world_horse.create_sprite(HORSE_X, HORSE_Y);
            horse.set_camera(camera);
            sort_sprite(horse, false);
            bn::sprite_ptr reindeer_a = bn::sprite_items::world_reindeer.create_sprite(164 - HALF_MAP, 68 - HALF_MAP);
            bn::sprite_ptr reindeer_b = bn::sprite_items::world_reindeer.create_sprite(174 - HALF_MAP, 82 - HALF_MAP);

            reindeer_a.set_camera(camera);
            sort_sprite(reindeer_a, false);
            reindeer_b.set_camera(camera);
            sort_sprite(reindeer_b, false);

            // Footprints in the snow, and your reflection in the pond.
            bn::vector<footprint, FOOTPRINT_POOL> footprints;
            bn::blending::set_transparency_alpha(bn::fixed(0.55));
            bn::sprite_ptr reflection = player_item.create_sprite(0, 0);
            reflection.set_camera(camera);
            reflection.set_vertical_flip(true);
            reflection.set_blending_enabled(true);
            reflection.set_bg_priority(PRIORITY_BEHIND);
            reflection.set_z_order(500);
            reflection.set_visible(false);

            // Apple and heart for the pony (hidden until needed).
            bn::sprite_ptr effect = bn::sprite_items::world_fx.create_sprite(0, 0);
            effect.set_camera(camera);
            effect.set_bg_priority(1);
            effect.set_z_order(-100);
            effect.set_visible(false);

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

            // --- little animations: the pony grazes, the reindeer look around
            int horse_override = -1;     // >= 0: show this horse frame (while it is being fed)

            auto animate_animals = [&]() {
                constexpr int horse_pattern[16] = { 0, 0, 0, 0, 0, 1, 1, 2, 1, 1, 2, 1, 1, 0, 0, 0 };
                constexpr int deer_pattern[12] = { 0, 0, 0, 1, 1, 1, 1, 0, 0, 2, 2, 0 };
                int t = frame_count();
                int hf = horse_override >= 0 ? horse_override : horse_pattern[(t / 20) % 16];
                horse.set_tiles(bn::sprite_items::world_horse.tiles_item(), hf);
                reindeer_a.set_tiles(bn::sprite_items::world_reindeer.tiles_item(), deer_pattern[(t / 24) % 12]);
                reindeer_b.set_tiles(bn::sprite_items::world_reindeer.tiles_item(), deer_pattern[(t / 24 + 5) % 12]);
            };

            auto clear_info = [&]() {
                info_place.clear();
                info_game.clear();
                info_best.clear();
                info_hint.clear();
                shown_place = -2;
                panel_clear();
            };

            auto idle = [&](int frames) {
                for(int i = 0; i < frames; ++i)
                {
                    animate_animals();
                    snow.update();
                    update();
                }
            };

            // Walk up to the pony and press A.
            auto talk_to_horse = [&]() {
                clear_info();
                sfx(bn::sound_items::sfx_select);
                constexpr bn::string_view items[] = { "Feed an apple", "Pat the pony", "Never mind" };
                int choice = menu(15, 1, items, 0, true);

                if(choice == 0)
                {
                    // The apple flies from you to the pony ...
                    effect.set_tiles(bn::sprite_items::world_fx.tiles_item(), 0);
                    effect.set_visible(true);

                    for(int i = 0; i <= 20; ++i)
                    {
                        bn::fixed k = bn::fixed(i) / 20;
                        effect.set_position(pos_x + (HORSE_X + 8 - pos_x) * k, pos_y - 8 + (HORSE_Y - 6 - pos_y + 8) * k - bn::fixed(18) * k * (1 - k) * 4);
                        idle(1);
                    }

                    // ... the pony munches ...
                    effect.set_visible(false);
                    sfx(bn::sound_items::sfx_catch);

                    for(int i = 0; i < 70; ++i)
                    {
                        horse_override = (i / 8) % 2 ? 2 : 1;
                        idle(1);
                    }

                    horse_override = 0;
                    // ... and a heart floats up.
                    effect.set_tiles(bn::sprite_items::world_fx.tiles_item(), 1);
                    effect.set_visible(true);
                    sfx(bn::sound_items::sfx_confirm);

                    for(int i = 0; i < 45; ++i)
                    {
                        effect.set_position(HORSE_X + 6, HORSE_Y - 22 - i / 3);
                        idle(1);
                    }

                    effect.set_visible(false);
                    horse_override = -1;
                    message("Pony:", "Munch munch! Yum!", "It loves you! @");
                }
                else if(choice == 1)
                {
                    effect.set_tiles(bn::sprite_items::world_fx.tiles_item(), 1);
                    effect.set_visible(true);
                    sfx(bn::sound_items::sfx_select);
                    horse_override = 0;

                    for(int i = 0; i < 40; ++i)
                    {
                        effect.set_position(HORSE_X + 6, HORSE_Y - 22 - i / 3);
                        idle(1);
                    }

                    effect.set_visible(false);
                    horse_override = -1;
                    message("Pony:", "Neigh! It nuzzles your hand.");
                }
            };

            place_camera();
            animate_animals();
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
                            // Leave a footprint behind if we are walking on fresh snow.
                            char here = layout_at(cell_x, cell_y);

                            if(here == '.' || here == 'N')      // fresh snow only (not the cobbled path, not doormats)
                            {
                                int px = world_x(cell_x).right_shift_integer() + ((step_count & 1) ? 3 : -3) * (dx ? 0 : 1);
                                int py = world_y(cell_y).right_shift_integer() + 10 + ((step_count & 1) ? 3 : -3) * (dx ? 1 : 0);

                                if(footprints.full())
                                {
                                    footprints.erase(footprints.begin());
                                }

                                bn::sprite_ptr print = bn::sprite_items::world_prints.create_sprite(px, py, dx ? 1 : 0);
                                print.set_camera(camera);
                                print.set_bg_priority(PRIORITY_BEHIND);
                                print.set_z_order(600);
                                footprints.push_back({ print, step_count, dx != 0 });
                            }

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

                            if(layout_at(fx, fy) == 'H')
                            {
                                talk_to_horse();
                            }
                            else if(npc* n = npc_at(fx, fy))
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
                                const char* const* line = npc_lines[n->spot];
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
                sort_sprite(player, is_behind_something(pos_x, pos_y));
                bn::fixed_point cam_delta = place_camera();

                // Footprints get paler, then vanish, as you walk on.
                for(int i = 0; i < footprints.size(); )
                {
                    int age = step_count - footprints[i].born;

                    if(age >= FOOTPRINT_GONE_STEPS)
                    {
                        footprints.erase(footprints.begin() + i);
                        continue;
                    }

                    int base_frame = footprints[i].wide ? 1 : 0;
                    footprints[i].sprite.set_tiles(bn::sprite_items::world_prints.tiles_item(),
                                                   age >= FOOTPRINT_FRESH_STEPS ? base_frame + 2 : base_frame);
                    ++i;
                }

                // Your reflection shows up when you stand on the shore with the pond right below you.
                bool at_pond = layout_at(cell_x, cell_y + 1) == '~' && layout_at(cell_x, cell_y + 2) == '~';
                reflection.set_visible(at_pond);

                if(at_pond)
                {
                    reflection.set_tiles(player_item.tiles_item(), frame);
                    reflection.set_horizontal_flip(dir == DIR_LEFT);
                    reflection.set_position(pos_x, pos_y + 2 * FEET_OFFSET);
                }

                animate_animals();

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
                        // (Only a few text colours can be on screen at once: the GBA has just 16 sprite palettes.)
                        info_game.set(104, y0 + 12, game(place).title, text_color::BLUE, align::RIGHT);
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
