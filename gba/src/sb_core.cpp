#include "sb_core.h"

#include "bn_bg_palettes.h"
#include "bn_color.h"
#include "bn_core.h"
#include "bn_keypad.h"
#include "bn_sprite_palettes.h"

#ifdef SB_RESOURCE_LOG
    #include "bn_bg_tiles.h"
    #include "bn_log.h"
    #include "bn_sprite_affine_mats.h"
    #include "bn_sprite_tiles.h"
    #include "bn_sprites.h"
#endif

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

#ifdef SB_RESOURCE_LOG
        // Test builds only (make USERFLAGS=-DSB_RESOURCE_LOG): logs every new peak of the hardware limits
        // (sprites of 128, sprite palettes of 16, sprite tiles of 1024, affine matrices of 32, BG tiles of
        // 2048) and of the CPU used per frame (1 = a whole frame; more than 1 = slowdown). The CPU peak skips
        // the frames right after a scene is built (loading is allowed to be slow).
        struct resource_peaks
        {
            int sprites = 0;
            int palette_colors = 0;
            int sprite_tiles = 0;
            int affine_mats = 0;
            int bg_tiles = 0;
            bn::fixed cpu;
        };

        resource_peaks g_peaks;
        int g_steady_frames = 0;
        int g_scene_start = 0;

        // Called when a scene fades out: one summary line for the scene, then start counting again.
        void log_scene_end()
        {
            BN_LOG("SCENE f", g_scene_start, "-", g_frames, ": sprites ", g_peaks.sprites, "/128 palettes ",
                   (g_peaks.palette_colors + 15) / 16, "/16 sprite_tiles ", g_peaks.sprite_tiles, "/1024 affine ",
                   g_peaks.affine_mats, "/32 bg_tiles ", g_peaks.bg_tiles, "/2048 cpu ", g_peaks.cpu);
            g_peaks = resource_peaks();
            g_steady_frames = 0;
            g_scene_start = g_frames;
        }

        void log_resources()
        {
            int sprites = bn::sprites::used_items_count();
            int colors = bn::sprite_palettes::used_colors_count();
            int tiles = bn::sprite_tiles::used_tiles_count();
            int mats = bn::sprite_affine_mats::used_count();
            int bg_tiles = bn::bg_tiles::used_tiles_count();
            bn::fixed cpu = bn::core::last_cpu_usage();

            if(cpu > bn::fixed(0.98))
            {
                g_steady_frames = 0;    // probably a loading frame: don't count it, or the next few
            }

            bool steady = ++g_steady_frames > 8;

            if(sprites > g_peaks.sprites || colors > g_peaks.palette_colors || tiles > g_peaks.sprite_tiles ||
               mats > g_peaks.affine_mats || bg_tiles > g_peaks.bg_tiles || (steady && cpu > g_peaks.cpu))
            {
                g_peaks.sprites = bn::max(sprites, g_peaks.sprites);
                g_peaks.palette_colors = bn::max(colors, g_peaks.palette_colors);
                g_peaks.sprite_tiles = bn::max(tiles, g_peaks.sprite_tiles);
                g_peaks.affine_mats = bn::max(mats, g_peaks.affine_mats);
                g_peaks.bg_tiles = bn::max(bg_tiles, g_peaks.bg_tiles);

                if(steady)
                {
                    g_peaks.cpu = bn::max(cpu, g_peaks.cpu);
                }

                BN_LOG("PEAK f", g_frames, " sprites ", g_peaks.sprites, "/128 palettes ",
                       (g_peaks.palette_colors + 15) / 16, "/16 sprite_tiles ", g_peaks.sprite_tiles,
                       "/1024 affine ", g_peaks.affine_mats, "/32 bg_tiles ", g_peaks.bg_tiles, "/2048 cpu ",
                       g_peaks.cpu);
            }
        }
#endif
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

#ifdef SB_RESOURCE_LOG
        log_resources();
#endif
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

#ifdef SB_RESOURCE_LOG
        log_scene_end();
#endif
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
