#include "sb_core.h"

#include "bn_bg_palettes.h"
#include "bn_color.h"
#include "bn_core.h"
#include "bn_keypad.h"
#include "bn_sprite_palettes.h"

#include "sb_audio.h"
#include "sb_save.h"
#include "sb_text.h"

namespace sb
{
    namespace
    {
        bn::random g_rng;
        int g_frames = 0;
        int g_player = 0;
        int g_toast_timer = 0;
        bool g_faded = false;
        label g_toast;

        void set_fade(bn::fixed intensity)
        {
            bn::bg_palettes::set_fade(bn::color(0, 0, 0), intensity);
            bn::sprite_palettes::set_fade(bn::color(0, 0, 0), intensity);
        }
    }

    void update()
    {
        if(bn::keypad::select_pressed())
        {
            toggle_music();
            g_toast.set(116, 72, music_enabled() ? "} On" : "} Off", text_color::WHITE, align::RIGHT);
            g_toast_timer = 75;
        }

        if(g_toast_timer > 0)
        {
            --g_toast_timer;

            if(g_toast_timer == 0)
            {
                g_toast.clear();
            }
        }

        g_rng.update();
        ++g_frames;
        bn::core::update();
    }

    void wait_frames(int n)
    {
        for(int i = 0; i < n; ++i)
        {
            update();
        }
    }

    bn::random& rng()
    {
        return g_rng;
    }

    int rand_range(int min, int max)
    {
        return min + g_rng.get_int(max - min + 1);
    }

    void fade_out(int frames)
    {
        for(int i = 1; i <= frames; ++i)
        {
            set_fade(bn::fixed(i) / frames);
            update();
        }

        g_faded = true;
    }

    void fade_in(int frames)
    {
        g_faded = false;

        for(int i = frames - 1; i >= 0; --i)
        {
            set_fade(bn::fixed(i) / frames);
            update();
        }
    }

    bool is_faded()
    {
        return g_faded;
    }

    int frame_count()
    {
        return g_frames;
    }

    int player_character()
    {
        return g_player;
    }

    void set_player_character(int character)
    {
        g_player = character;
        set_last_character(character);
    }
}
