#include "bn_core.h"
#include "bn_bg_palettes.h"
#include "bn_color.h"
int main()
{
    bn::core::init();
    bn::bg_palettes::set_transparent_color(bn::color(31, 0, 0));
    while(true) { bn::core::update(); }
}
