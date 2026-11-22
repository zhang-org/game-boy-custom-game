// Gently falling snow made of small sprites. Create one in a scene and call update() every frame.
//   sb::snowfall snow(24);         // 24 flakes
//   ... snow.update();
#ifndef SB_SNOW_H
#define SB_SNOW_H

#include "bn_fixed.h"
#include "bn_sprite_ptr.h"
#include "bn_vector.h"

namespace sb
{
    class snowfall
    {
    public:
        // bg_priority: 1 = in front of game backgrounds (default), 3 = behind sprites.
        explicit snowfall(int count = 20, int bg_priority = 1);

        // dx/dy: extra drift in pixels per frame (e.g. to follow a scrolling camera).
        void update(bn::fixed dx = 0, bn::fixed dy = 0);

        void set_visible(bool visible);

    private:
        struct flake
        {
            bn::sprite_ptr sprite;
            bn::fixed x, y, speed;
            int phase;
        };

        bn::vector<flake, 40> _flakes;
    };
}

#endif
