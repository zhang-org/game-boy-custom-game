#include "sb_audio.h"

#include "bn_music.h"
#include "bn_optional.h"

#include "sb_save.h"

namespace sb
{
    namespace
    {
        // Overall balance. Sound effects are a bit louder than music so they cut through.
        constexpr bn::fixed music_volume = 0.45;
        constexpr bn::fixed sfx_volume = 0.7;

        bn::optional<bn::music_item> g_song;
    }

    void play_music(const bn::music_item& song)
    {
        if(g_song && *g_song == song && bn::music::playing())
        {
            return;
        }

        g_song = song;

        if(music_enabled())
        {
            song.play(music_volume);
        }
        else if(bn::music::playing())
        {
            bn::music::stop();
        }
    }

    void stop_music()
    {
        g_song.reset();

        if(bn::music::playing())
        {
            bn::music::stop();
        }
    }

    void play_jingle(const bn::music_item& jingle)
    {
        g_song.reset();

        if(music_enabled())
        {
            jingle.play(music_volume, false);
        }
    }

    void sfx(const bn::sound_item& sound)
    {
        sound.play(sfx_volume);
    }

    void toggle_music()
    {
        bool enabled = ! music_enabled();
        set_music_enabled(enabled);

        if(enabled)
        {
            if(g_song)
            {
                g_song->play(music_volume);
            }
        }
        else if(bn::music::playing())
        {
            bn::music::stop();
        }
    }
}
