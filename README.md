# Chromatic mini games

Homebrew Game Boy Color games for the ModRetro Chromatic, built with GBDK-2020 (C).

## Layout
- `src/common/`: shared between games
  - `characters.h`: the family faces, names and colours
  - `font.h`, `gfx.h`: the font and art helpers
- `src/flappy/`: Flappy Friend
- `src/pingpong/`: Ping Pong Pals
- Each folder under `src/` (except `common`) builds into its own ROM: `build\<folder>.gbc`

## Working together
Two of us work in this repo with separate Claude Code agents. The rules are in [CLAUDE.md](CLAUDE.md), and every agent loads them automatically. Short version: every change goes on a personal branch (`adam/...` or `babel/...`), then a PR into `main`. Open PRs mark work as taken, and messages go in PR notes or `Heads-up:` issues.

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
