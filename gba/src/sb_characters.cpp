#include "sb_characters.h"

#include "bn_sprite_items_chr_adam.h"
#include "bn_sprite_items_chr_aiden.h"
#include "bn_sprite_items_chr_alyssa.h"
#include "bn_sprite_items_chr_billy.h"
#include "bn_sprite_items_chr_bo.h"
#include "bn_sprite_items_chr_charly.h"
#include "bn_sprite_items_chr_cheng.h"
#include "bn_sprite_items_chr_cherry.h"
#include "bn_sprite_items_chr_ella.h"
#include "bn_sprite_items_chr_lillian.h"

namespace sb
{
    namespace
    {
        struct character_entry
        {
            const bn::sprite_item& sprite;
            const char* name;
        };

        // To add someone: draw graphics/characters/chr_<name>.bmp (+ .json), add a line here and
        // bump character_count in sb_characters.h.
        constexpr character_entry characters[character_count] = {
            { bn::sprite_items::chr_adam, "Adam" },
            { bn::sprite_items::chr_charly, "Charly" },
            { bn::sprite_items::chr_billy, "Billy" },
            { bn::sprite_items::chr_bo, "Bo" },
            { bn::sprite_items::chr_cherry, "Cherry" },
            { bn::sprite_items::chr_lillian, "Lillian" },
            { bn::sprite_items::chr_cheng, "Cheng" },
            { bn::sprite_items::chr_ella, "Ella" },
            { bn::sprite_items::chr_aiden, "Aiden" },
            { bn::sprite_items::chr_alyssa, "Alyssa" },
        };
    }

    const bn::sprite_item& character_sprite(int character)
    {
        return characters[character].sprite;
    }

    const char* character_name(int character)
    {
        return characters[character].name;
    }
}
