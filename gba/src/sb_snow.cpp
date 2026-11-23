#include "sb_snow.h"

#include "bn_math.h"

#include "bn_sprite_items_snow.h"

#include "sb_core.h"

namespace sb
{
    snowfall::snowfall(int count, int bg_priority)
    {
        for(int i = 0; i < count && i < _flakes.max_size(); ++i)
        {
            int size = rand_range(0, 2);
            bn::sprite_ptr sprite = bn::sprite_items::snow.create_sprite(0, 0, size);
            sprite.set_bg_priority(bg_priority);
            bn::fixed speed = bn::fixed(3 - size) / 6 + bn::fixed(rand_range(0, 10)) / 40;
            _flakes.push_back({ sprite, rand_range(-124, 124), rand_range(-84, 84), speed, rand_range(0, 255) });
        }
    }

    void snowfall::update(bn::fixed dx, bn::fixed dy)
    {
        for(flake& f : _flakes)
        {
            f.phase = (f.phase + 1) & 255;
            f.y += f.speed + dy;
            f.x += bn::sin(bn::fixed(f.phase) / 256) / 3 + dx;

            if(f.y > 88)
            {
                f.y -= 176;
                f.x = rand_range(-124, 124);
            }
            else if(f.y < -88)
            {
                f.y += 176;
            }

            if(f.x > 126)
            {
                f.x -= 252;
            }
            else if(f.x < -126)
            {
                f.x += 252;
            }

            f.sprite.set_position(f.x, f.y);
        }
    }

    void snowfall::set_visible(bool visible)
    {
        for(flake& f : _flakes)
        {
            f.sprite.set_visible(visible);
        }
    }
}
