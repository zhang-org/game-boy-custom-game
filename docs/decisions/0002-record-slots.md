# 0002: Which game uses which extra record slot

Date: 2026-11-23 · Status: accepted

## Decision
Save version 4 (`SNOWBRY4`) has 24 extra record slots (`sb::extra_record_count`). Each game that keeps more than one record (usually one per difficulty, Easy / Medium / Hard) owns a fixed block:

| Slots | Game | What's stored |
|---|---|---|
| 0-2 | Ping Pong Pals | best match result per difficulty |
| 3-5 | Sled Dash | best time per course (`1000000 - hundredths`) |
| 6-8 | Pony Gallop | best race per difficulty |
| 9-11 | Ice Fishing | best catch per difficulty |
| 12-14 | Gift Tower (Gift Catch) | best score per difficulty |
| 15-17 | Snowball Fight | best score per difficulty |
| 18-22 | free | |
| 23 | (internal) | instructions-read flags, one bit per game |

- A saved value must be **higher = better** (store `BASE - time` for times), and 0 means "no record".
- A game shows its record through `game_info::best_text` (`sb_games.h`), written in the game's own file. `sb::level_record_text()` handles the usual "hardest level that has a record" case.
- The old `best[game]` score is still kept for every game, but a game with its own `best_text` doesn't have to use it.

## Why
Version 3 had 8 slots, and 0-5 and 7 were already taken, so the four games being reworked (steps 5-8 of issue #5) wouldn't fit. Fixing the blocks up front means each game's PR only touches its own file, and the PRs don't conflict with each other.

## Changing it
Adding slots changes the save layout: bump the last letter of `save_tag` in `gba/src/sb_save.cpp` and convert the previous version in `save_init()`, the way version 3 is converted to 4. If a game changes how it scores, clear its slots when converting. Add a new decision file that replaces this one.
