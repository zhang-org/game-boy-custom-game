# Chromatic mini games

Homebrew Game Boy Color games for the ModRetro Chromatic, built with GBDK-2020 (C).

## Layout
- `src/common/`: shared between games
  - `characters.h`: the family faces, names and colours
  - `font.h`, `gfx.h`: the font and art helpers
- `src/flappy/`: Flappy Friend
- `src/pingpong/`: Ping Pong Pals
- Each folder under `src/` (except `common`) builds into its own ROM: `build\<folder>.gbc`

## Tools (already installed)
- GBDK-2020 4.5.0 in `C:\gbdk` (this path has no spaces on purpose, because the compiler breaks on paths with spaces)
- mGBA emulator in `C:\Program Files\mGBA`

## Dev loop
1. Edit the code or art.
2. Build everything and play one game:

   ```
   .\build.ps1 -Run pingpong
   ```
   Use `-Run flappy` for Flappy Friend.
3. To test on the real device, put the microSD in the PC and run:

   ```
   .\deploy.ps1 -Drive E
   ```
   This copies all ROMs to `E:\HOMEBREW`. Then put the card in the Everdrive.

## mGBA keys (default)
X = A, Z = B, Enter = Start, Backspace = Select, arrow keys = D-pad.

## Games
- **Flappy Friend**
  - A / B / Up flaps.
  - Left/Right on the title screen picks a character.
- **Ping Pong Pals** (seen from behind your end of the table)
  - Title screen: Up/Down chooses you or your opponent, and Left/Right changes the character.
  - Serve: A tosses, then A or B hits. Hitting at the top of the toss gives a NICE SERVE.
  - Shots: A = topspin (fast), B = slice (low bounce), A+B = lob.
  - Aiming: hold Left/Right while hitting to aim. Up = deep, Down = short.
  - Power shots: press the button early, before the ball arrives, to charge.
  - First to 7 wins. Select turns the music on or off.
  - The music player is shared in `src/common/music.h`. The ping pong song is in `src/pingpong/song.h`.
