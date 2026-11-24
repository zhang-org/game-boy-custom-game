# 0003: How the cartridge save memory (SRAM) is split up

The GBA cartridge has 32 KB of battery-backed SRAM. To stop games overwriting each other's data, each user gets a fixed range:

| Bytes | Used by | What |
|---|---|---|
| 0 - 2047 | `sb_save.cpp` | The main save: two copies (0 and 1024) of `save_data`, which must stay under 1024 bytes. |
| 2048 - 4095 | free | |
| 4096 - 18687 | `games/sled.cpp` | Sled Dash ghosts: one `ghost_run` per course (Easy, Medium, Hard), `GHOST_SRAM_STRIDE` = 4864 bytes apart, each with its own tag (`SBGHOST1`) and checksum. |
| 18688 - 32767 | free | |

Rules:
- A new user takes a free range and adds a row here, in a new decision file that replaces this one.
- Every block has a tag and a checksum, so a fresh cartridge (all 0xFF) or old data is ignored rather than misread.
