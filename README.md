# Snowberry (GBA Christmas game)

A Game Boy Advance collection of six winter mini games starring the family, played on a Miyoo Mini Plus (or any GBA emulator). Everything lives in [gba/](gba/). It's built with [Butano](https://github.com/GValiente/butano) (C++, zlib license), which is included as a git submodule in `gba/butano`.

## GBA game setup

**Mac**
1. Install devkitPro: download `devkitpro-pacman-installer.pkg` from https://github.com/devkitPro/pacman/releases/latest, then install it from Terminal (Finder blocks it because it isn't notarized):
   ```
   sudo installer -pkg ~/Downloads/devkitpro-pacman-installer.pkg -target /
   sudo dkp-pacman -Sy
   sudo dkp-pacman -S gba-dev        # press Enter to install all
   ```
2. Add to `~/.zshrc`:
   ```
   export DEVKITPRO=/opt/devkitpro
   export DEVKITARM=$DEVKITPRO/devkitARM
   export PATH=$DEVKITPRO/tools/bin:$PATH
   ```
3. Python 3 (already on most Macs) and mGBA (`brew install --cask mgba`).

**Windows**
1. Install devkitPro with the graphical installer from https://github.com/devkitPro/installer/releases (tick **GBA Development**). Keep the default folder `C:\devkitPro`.
2. Install Python 3 from python.org and tick "Add python.exe to PATH".
3. mGBA: `winget install --id JeffreyPfau.mGBA -e`
4. The project folder's path must not contain a space (the build tools break on it). If yours does (e.g. `Documents\GB Game`), give the folder a drive letter first and build from there: `subst G: "C:\Users\you\Documents\GB Game"` then `G:\gba\build.ps1`.

**Both:** after cloning, fetch Butano once: `git submodule update --init` (the build scripts also do this for you).

## Building and playing
```
cd gba
./build.sh -r          # Mac: build gba/snowberry.gba and open it in mGBA
.\build.ps1 -Run       # Windows
```
The first build compiles Butano and takes a minute or two; after that it takes a few seconds.

**On the Miyoo Mini Plus (Onion OS):** copy `gba/snowberry.gba` to `Roms/GBA/` on the SD card. High scores are saved in the game's save file (SRAM), so they survive turning it off. If anything looks or sounds off with the default gpSP core, switch the game to the mGBA core (in Onion's game options).

**Controls:** D-pad walk/move, A action, B back (hold B to run on the map), Start menu/pause, Select music on/off. mGBA keys: X = A, Z = B, Enter = Start, Backspace = Select.

## Changing the art and music
- All art is in `gba/graphics/` as indexed BMP files (16 colours for sprites, first palette colour = transparent). Redraw any of them in Aseprite or Usenti, keeping the same size.
- The starting art was drawn by the Python scripts in `gba/tools/art/` (characters, town map, font and text boxes, one script per mini game). Re-running a script overwrites its BMPs, so once you hand-draw a file, stop regenerating it.
- Characters: `gba/graphics/characters/chr_<name>.bmp`, 12 frames of 16x32 (frame order in `gba/include/sb_characters.h`). To change someone's hair, clothes or hat, edit their line in `gba/tools/art/make_characters.py` and run it.
- Music: songs are written as note text in `gba/tools/music/songs.py`; sound effects are recipes in `gba/tools/music/make_audio.py`. Run `python3 gba/tools/music/make_audio.py` to regenerate `gba/audio/`.
- The town layout (where things are, what you can walk on) is in `gba/tools/art/make_world.py`. It draws two pictures (the ground and a "top" layer with trees, houses and lamps, so people can walk behind them) and writes `gba/include/sb_world_layout.h`.
- The horse, reindeer and snow footprints on the map are sprites: `gba/tools/art/make_world_sprites.py`. The opening scene (family photo by the tree) is `gba/tools/art/make_title.py` and `gba/src/scenes.cpp`.
- **The game's name** (title screen): edit the one line in `gba/tools/art/game_title.txt`, then run `python3 gba/tools/art/make_ui.py` and rebuild. The name inside the ROM file is `ROMTITLE` in `gba/Makefile` (12 capital letters at most).
- Every mini game keeps its difficulty numbers as named constants at the top of `gba/src/games/<game>.cpp`.

## Testing without watching the screen (optional, Mac)
`gba/tools/harness/` has a headless mGBA test runner that presses buttons from a script and saves screenshots. See the comments in `harness.c` and `build_harness.sh`.

---

# Chromatic mini games (Game Boy Color prototypes)

Homebrew Game Boy Color games for the ModRetro Chromatic, built with GBDK-2020 (C).

## Layout
- `src/common/`: shared between games
  - `characters.h`: the family faces, names and colours
  - `font.h`, `gfx.h`: the font and art helpers
- `src/flappy/`: Flappy Friend
- `src/pingpong/`: Ping Pong Pals
- Each folder under `src/` (except `common`) builds into its own ROM: `build\<folder>.gbc`

## Working together
Two of us work in this repo with separate Claude Code agents. The rules are in [CLAUDE.md](CLAUDE.md), and every agent loads them automatically. Short version: every change goes on a personal branch (`adam/...` or `charly/...`), then a PR into `main`. Open PRs mark work as taken, and messages go in PR notes or `Heads-up:` issues.

## Tools
**Windows:**
- GBDK-2020 4.5.0 in `C:\gbdk` (this path has no spaces on purpose, because the compiler breaks on paths with spaces)
- mGBA emulator in `C:\Program Files\mGBA`

**Mac (Apple Silicon):**
```
gh release download 4.5.0 -R gbdk-2020/gbdk-2020 -p gbdk-macos-arm64.tar.gz -D /tmp
tar -xzf /tmp/gbdk-macos-arm64.tar.gz -C ~        # creates ~/gbdk
xattr -dr com.apple.quarantine ~/gbdk              # lets macOS run the unsigned compiler
brew install --cask mgba
```

## Dev loop
1. Edit the code or art.
2. Build everything and play one game:

   ```
   .\build.ps1 -Run pingpong      # Windows
   ./build.sh -r pingpong         # Mac
   ```
   Use `flappy` for Flappy Friend.
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
