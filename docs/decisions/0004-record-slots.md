# 0004: Record slots (replaces 0002)

Same as [0002](0002-record-slots.md), except that Pony Gallop became **Show Jumping** and scores differently, so it moved to new slots. Its old race times in 6-8 aren't read any more.

| Slots | Game | What's stored |
|---|---|---|
| 0-2 | Ping Pong Pals | best match result per difficulty |
| 3-5 | Sled Dash | best time per course (`1000000 - hundredths`) |
| 6-8 | (retired) | Pony Gallop race times: left in old saves, not used |
| 9-11 | Ice Fishing | best catch per difficulty |
| 12-14 | Gift Tower | best score per difficulty |
| 15-17 | Snowball Fight | best score per difficulty |
| 18-20 | Show Jumping | best round per course (`1000000 - (faults * 10000 + hundredths)`: fewest faults, then fastest) |
| 21-22 | free | |
| 23 | (internal) | instructions-read flags, one bit per game |

The rules from 0002 still apply: higher = better, 0 = no record, and a change to the save layout bumps the save version.
