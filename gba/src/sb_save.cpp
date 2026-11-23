#include "sb_save.h"

#include "bn_sram.h"

#include "sb_characters.h"
#include "sb_games.h"
#include "sb_text.h"

namespace sb
{
    namespace
    {
        // The last letter is the save version. Older saves are converted in save_init():
        //   '1'/'2' (no extra records): everything is kept except the Ping Pong record (its scoring changed).
        constexpr char save_tag[8] = { 'S', 'N', 'O', 'W', 'B', 'R', 'Y', '3' };
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
            high_score extra[extra_record_count];
            unsigned checksum;
        };

        // The layout of versions 1 and 2, only read to convert old saves.
        struct save_data_old
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

        unsigned checksum_of(const void* data, int size)
        {
            const unsigned char* bytes = static_cast<const unsigned char*>(data);
            unsigned sum = 0x1234567;

            for(int i = 0; i < size; ++i)
            {
                sum = (sum << 5) + sum + bytes[i];
            }

            return sum;
        }

        template<typename Data>
        bool valid(const Data& data, char min_version, char max_version)
        {
            for(int i = 0; i < 7; ++i)
            {
                if(data.tag[i] != save_tag[i])
                {
                    return false;
                }
            }

            if(data.tag[7] < min_version || data.tag[7] > max_version)
            {
                return false;
            }

            if(data.checksum != checksum_of(&data, int(sizeof(Data) - sizeof(unsigned))))
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

        void clear_entry(high_score& entry)
        {
            entry.score = 0;
            entry.character = -1;

            for(char& c : entry.name)
            {
                c = 0;
            }
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
                clear_entry(entry);
            }

            for(high_score& entry : g_data.extra)
            {
                clear_entry(entry);
            }

            g_data.music_on = 1;
            g_data.last_character = 0;
            g_data.sequence = 0;
        }

        bool submit(high_score& entry, int score, int character)
        {
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
    }

    void save_init()
    {
        save_data slots[2];
        bn::sram::read_offset(slots[0], slot_offsets[0]);
        bn::sram::read_offset(slots[1], slot_offsets[1]);

        bool valid0 = valid(slots[0], save_tag[7], save_tag[7]);
        bool valid1 = valid(slots[1], save_tag[7], save_tag[7]);

        if(valid0 && (! valid1 || slots[0].sequence >= slots[1].sequence))
        {
            g_data = slots[0];
            return;
        }

        if(valid1)
        {
            g_data = slots[1];
            return;
        }

        // An older save? Convert it.
        save_data_old old_slots[2];
        bn::sram::read_offset(old_slots[0], slot_offsets[0]);
        bn::sram::read_offset(old_slots[1], slot_offsets[1]);
        bool old0 = valid(old_slots[0], '1', '2');
        bool old1 = valid(old_slots[1], '1', '2');
        set_defaults();

        if(old0 || old1)
        {
            const save_data_old& old = old0 && (! old1 || old_slots[0].sequence >= old_slots[1].sequence) ?
                                       old_slots[0] : old_slots[1];

            for(int i = 0; i < max_games; ++i)
            {
                g_data.best[i] = old.best[i];
            }

            clear_entry(g_data.best[GAME_PINGPONG]);
            g_data.music_on = old.music_on;
            g_data.last_character = old.last_character;
            g_data.sequence = old.sequence;
        }

        save_write();
        save_write();
    }

    void save_write()
    {
        ++g_data.sequence;
        g_data.checksum = checksum_of(&g_data, int(sizeof(save_data) - sizeof(unsigned)));
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
        return submit(g_data.best[game], score, character);
    }

    int extra_score(int slot)
    {
        return g_data.extra[slot].score;
    }

    const char* extra_name(int slot)
    {
        return g_data.extra[slot].name;
    }

    bool submit_extra(int slot, int score, int character)
    {
        return submit(g_data.extra[slot], score, character);
    }

    bn::string<40> best_record_text(int game, bool compact)
    {
        const char* by = compact ? "  " : " by ";

        bn::string<40> text;

        if(game == GAME_PINGPONG)
        {
            // The hardest difficulty with a record (a win beats any loss).
            int best_slot = -1;

            for(int slot = 0; slot < 3; ++slot)
            {
                int value = extra_score(slot);

                if(value > 0 && (best_slot < 0 || pingpong_result_won(value) ||
                                 ! pingpong_result_won(extra_score(best_slot))))
                {
                    best_slot = slot;
                }
            }

            if(best_slot >= 0)
            {
                text.append(compact && best_slot == 1 ? "Med" : pingpong_level_name(best_slot));
                text.append(" ");
                text.append(pingpong_result_text(extra_score(best_slot)));
                text.append(by);
                text.append(extra_name(best_slot));
            }

            return text;
        }

        if(best_score(game) > 0)
        {
            text.append(to_text(best_score(game)));
            text.append(by);
            text.append(best_name(game));
        }

        return text;
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
