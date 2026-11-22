// The family characters. Art: graphics/characters/chr_<name>.bmp (16x32 frames, see
// tools/art/make_characters.py for the frame order and how to change someone's look).
#ifndef SB_CHARACTERS_H
#define SB_CHARACTERS_H

#include "bn_sprite_item.h"

namespace sb
{
    constexpr int character_count = 10;

    // Frames in every character sheet. SIDE frames face right: use set_horizontal_flip(true) for left.
    enum character_frame
    {
        FRAME_DOWN = 0, FRAME_DOWN_WALK_A, FRAME_DOWN_WALK_B,
        FRAME_UP, FRAME_UP_WALK_A, FRAME_UP_WALK_B,
        FRAME_SIDE, FRAME_SIDE_WALK_A, FRAME_SIDE_WALK_B,
        FRAME_SIT,       // sitting, facing the viewer (sled)
        FRAME_SIDE_SIT,  // sitting, facing right (riding, fishing)
        FRAME_CHEER,     // arms up, happy
        FRAME_COUNT
    };

    // 16x32 sprite sheet for a character. Create sprites with
    //   sb::character_sprite(id).create_sprite(x, y, sb::FRAME_DOWN)
    // and change frame with sprite.set_tiles(sb::character_sprite(id).tiles_item(), frame).
    const bn::sprite_item& character_sprite(int character);

    // First name shown on screen (short: max 7 letters).
    const char* character_name(int character);
}

#endif
