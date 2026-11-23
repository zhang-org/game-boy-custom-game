#include "sb_save.h"

#include "bn_sram.h"

#include "sb_characters.h"
#include "sb_games.h"

namespace sb
{
    namespace
    {
        // The last letter is the save version. When a game's scoring changes, bump it and clear that
        // game's old record in migrate() (old numbers would be unbeatable or meaningless).
        constexpr char save_tag[8] = { 'S', 'N', 'O', 'W', 'B', 'R', 'Y', '2' };
        constexpr int slot_offsets[2] = { 0, 1024 };
        constexpr int max_games = 8;

        struct high_score
        {
            int score;
            int character;
            char name[8];
        };

        struct save_data
        {
            char tag[8];
            int sequence;
            high_score best[max_games];
            int music_on;
            int last_character;
            unsigned checksum;
        };

        static_assert(GAME_COUNT <= max_games);
        static_assert(sizeof(save_data) < 1024);

        save_data g_data;

        unsigned checksum(const save_data& data)
        {
            const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&data);
            unsigned sum = 0x1234567;
            int size = int(sizeof(save_data) - sizeof(unsigned));

            for(int i = 0; i < size; ++i)
            {
                sum = (sum << 5) + sum + bytes[i];
            }

            return sum;
        }

        bool valid(const save_data& data)
        {
            for(int i = 0; i < 7; ++i)
            {
                if(data.tag[i] != save_tag[i])
                {
                    return false;
                }
            }

            if(data.tag[7] < '1' || data.tag[7] > save_tag[7])
            {
                return false;
            }

            if(data.checksum != checksum(data))
            {
                return false;
            }

            for(const high_score& entry : data.best)
            {
                if(entry.score < 0 || entry.name[7] != 0)
                {
                    return false;
                }
            }

            return data.last_character >= 0 && data.last_character < character_count;
        }

        void set_defaults()
        {
            g_data = save_data();

            for(int i = 0; i < 8; ++i)
            {
                g_data.tag[i] = save_tag[i];
            }

            for(high_score& entry : g_data.best)
            {
                entry.score = 0;
                entry.character = -1;

                for(char& c : entry.name)
                {
                    c = 0;
                }
            }

            g_data.music_on = 1;
            g_data.last_character = 0;
            g_data.sequence = 0;
        }

        void clear_record(int game)
        {
            high_score& entry = g_data.best[game];
            entry.score = 0;
            entry.character = -1;

            for(char& c : entry.name)
            {
                c = 0;
            }
        }

        // Brings an older save up to date. Returns true if anything changed.
        bool migrate()
        {
            if(g_data.tag[7] == save_tag[7])
            {
                return false;
            }

            if(g_data.tag[7] < '2')
            {
                clear_record(GAME_PINGPONG);    // v2: ping pong scores are now a win streak
            }

            g_data.tag[7] = save_tag[7];
            return true;
        }
    }

    void save_init()
    {
        save_data slots[2];
        bn::sram::read_offset(slots[0], slot_offsets[0]);
        bn::sram::read_offset(slots[1], slot_offsets[1]);

        bool valid0 = valid(slots[0]);
        bool valid1 = valid(slots[1]);

        if(valid0 && (! valid1 || slots[0].sequence >= slots[1].sequence))
        {
            g_data = slots[0];
        }
        else if(valid1)
        {
            g_data = slots[1];
        }
        else
        {
            set_defaults();
            save_write();
            save_write();
            return;
        }

        if(migrate())
        {
            save_write();
            save_write();
        }
    }

    void save_write()
    {
        ++g_data.sequence;
        g_data.checksum = checksum(g_data);
        bn::sram::write_offset(g_data, slot_offsets[g_data.sequence & 1]);
    }

    int best_score(int game)
    {
        return g_data.best[game].score;
    }

    const char* best_name(int game)
    {
        return g_data.best[game].name;
    }

    bool submit_score(int game, int score, int character)
    {
        high_score& entry = g_data.best[game];

        if(score <= entry.score)
        {
            return false;
        }

        entry.score = score;
        entry.character = character;

        const char* name = character_name(character);
        int i = 0;

        for(; i < 7 && name[i]; ++i)
        {
            entry.name[i] = name[i];
        }

        for(; i < 8; ++i)
        {
            entry.name[i] = 0;
        }

        save_write();
        return true;
    }

    bool music_enabled()
    {
        return g_data.music_on;
    }

    void set_music_enabled(bool enabled)
    {
        g_data.music_on = enabled;
        save_write();
    }

    int last_character()
    {
        return g_data.last_character;
    }

    void set_last_character(int character)
    {
        if(g_data.last_character != character)
        {
            g_data.last_character = character;
            save_write();
        }
    }
}
