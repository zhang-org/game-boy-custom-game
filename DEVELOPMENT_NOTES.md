# How we build Game Boy games for the Chromatic

Notes from our first two prototypes, Flappy Friend (Flappy-style) and Ping Pong Pals (table tennis seen from behind the player, Mario Tennis style). They cover the toolchain, the workflow, and the lessons we learned the hard way, so a new game can start from scratch without repeating the mistakes.

---

## 1. Target hardware

- **ModRetro Chromatic.** It's Game Boy Color compatible, so we build normal GBC ROMs.
- **Everdrive-style flash cart** that loads ROMs from a microSD card.
- We build "GBC-enhanced" ROMs (header flag `0x80`), which still run on an original Game Boy, just without colour.
- Plain 32 KB ROMs (no memory bank controller, or MBC) have been enough so far. We'll need an MBC once a game outgrows 32 KB, and also for battery saves (see section 7).

## 2. Toolchain

| Tool | What it's for | Where it's installed |
|---|---|---|
| [GBDK-2020](https://github.com/gbdk-2020/gbdk-2020) 4.5.0 | C compiler + Game Boy libraries (`lcc`) | `C:\gbdk` |
| [mGBA](https://mgba.io) 0.10.5 | Emulator for testing, plus tile / sprite / palette viewers | `C:\Program Files\mGBA` |

We picked GBDK because it's C, which is easier to iterate on than assembly. It also has good GBC colour support and ships `png2asset` for turning PNG art into tiles.

Setup on a new Windows PC:

```powershell
# GBDK: download gbdk-win64.zip from the GitHub releases page and unzip it to C:\gbdk
# mGBA (the winget id is the author's name, not "mGBA.mGBA"):
winget install --id JeffreyPfau.mGBA -e
```

## 3. Project layout

```
build.ps1          build every game; optionally launch one in mGBA
deploy.ps1         copy all ROMs to the microSD card
src/
  common/          code shared by every game
    characters.h   family characters: 16x16 faces + names + palettes
    font.h         5x7 font (0-9, A-Z, < >)
    gfx.h          helpers: text-art -> tiles, load font, glyph lookup
    music.h        tiny music player
  <game>/          one folder per game -> build/<game>.gbc
    main.c
    art.h
    song.h
build/             output ROMs (don't commit)
```

Every folder in `src/` except `common` becomes its own ROM. Shared code goes in header-only files (`static` functions), because each game is compiled as a single `main.c`.

## 4. The dev loop

1. Edit the code or art.
2. Build everything and open one game in mGBA (takes about 2 seconds):
   ```powershell
   .\build.ps1 -Run pingpong
   ```
3. Play and tweak. All the tuning values are `#define`s at the top of `main.c`, such as gravity, ball speed and CPU difficulty, so changing them is quick.
4. Every few sessions, test on the real Chromatic. Emulators can hide timing and video-memory bugs.
   ```powershell
   .\deploy.ps1 -Drive E      # copies build\*.gbc to E:\HOMEBREW
   ```

mGBA's default keys: X = A, Z = B, Enter = Start, Backspace = Select. Tools → Game state views shows the tiles, sprites and palettes, which helps when graphics look wrong.

## 5. Gotchas we hit (and how we fixed them)

| Problem | Fix |
|---|---|
| `lcc` breaks on paths with spaces | Install GBDK at `C:\gbdk`. The build script `cd`s into the project and uses relative paths only. |
| The build fails with "can't create build\x.gbc" | mGBA locks the open ROM. `build.ps1` closes mGBA before compiling. |
| The build fails because a variable name clashes with a GBDK function (e.g. `cpu_slow`) | GBDK has many global names (`cpu_slow`, `cpu_fast`, `rand` ...). Pick more specific names. |
| Background and sprite tiles overwrite each other | By default, background (BG) tiles and sprite tiles share video memory. Give them separate index ranges (e.g. BG 0–150, sprites 160+, font 200+). |
| 8x16 sprites show garbled tiles | In 8x16 mode, tile pairs are top/bottom: a 16x16 image is TL, BL, TR, BR. `load_sprite16()` in `gfx.h` handles this. |
| Colours wrong on GBC | Each BG tile's palette is set in VRAM bank 1 (`VBK_REG = 1`, write the attributes, `VBK_REG = 0`). Sprite palette = low 3 bits of `set_sprite_prop()`. |
| `warning 158: overflow in implicit constant conversion` | Harmless: tile numbers above 127 in arithmetic. Ignore it. |
| `winget install mGBA.mGBA` not found | The package id is `JeffreyPfau.mGBA`. |

## 6. Techniques that worked well

### Art as text
Sprites and tiles are written as text grids right in the C files (`'.'` = transparent, `1–3` = palette colours) and converted to tiles at boot. This makes it easy to hand-edit faces in a text editor or have Claude draw them. For real portraits later, use PNG + GBDK's `png2asset`.

### Family characters
- Each person is a 16x16 face with two frames (idle, plus a mouth-open "action" frame), plus their own 4-colour palette: skin, hair, outline. Every game shares this through `characters.h`.
- In 16x16, people are recognisable by **hair shape and colour, glasses, a beard, or a signature accessory**, not facial detail.
- A "back of the head" view can be generated automatically by recolouring the skin and inner details to hair colour.

### Text
A tiny 5x7 font is loaded into background tiles, and `put_text()` writes strings onto the tile map. A HUD can go on the **window layer** (a second, non-scrolling background) so it stays put while the background scrolls.

### Scrolling worlds (Flappy)
- The background map is 32 tiles wide and wraps around. Scroll it with `SCX_REG` and draw each new column just before it scrolls into view.
- Collisions are worked out mathematically from world coordinates, not by reading the tiles back.

### Pseudo-3D (Ping Pong)
- **Physics:** the ball has 3D coordinates (x, depth z, height h) in fixed-point (1/16 units).
- **Drawing:** a simple projection maps depth to screen y and width scale, and a shadow sprite on the table sells the height.
- **The table:** it's drawn pixel by pixel at boot, then identical 8x8 tiles are merged.
- **Shots:** instead of free physics, each shot picks a landing spot and a flight time, then solves for the velocity. It nudges the arc up if the ball would clip the net. This makes shots controllable and fun.

### Sound
Raw sound-chip register writes are enough. Our channel split:
- **Ch2:** sound effects
- **Ch1:** bass
- **Ch3 (wave):** melody
- **Ch4 (noise):** drums (and the occasional sound effect)

`music.h` plays songs written as note-name arrays (one entry per eighth note, with hold and rest markers). Select toggles the music.

## 7. Game design lessons from playtesting

- **Start too easy.** Our first version of Ping Pong was impossible. What helped:
  - **Auto-swing** when the ball is in reach, so the buttons only choose shot type and power.
  - Slower ball and a slower CPU.
- **Random CPU mistakes feel unfair. Earned mistakes feel great.** The final CPU scores each incoming shot:
  - **Inputs:** power, plus how far it must run compared with the time it has.
  - **Medium score:** it sends a slow, safe return, which gives you a chance to attack.
  - **High score:** it might also miss.
- **Reward skill visibly.** For example, a "NICE SERVE" for hitting at the top of the toss is faster and rattles the CPU.
- Tune from the `#define`s at the top of the file, one change at a time, then replay.

## 8. Working with Claude Code

- Describe the game in plain words. Claude writes the C, builds it, and opens it in mGBA. Then you playtest and say what feels wrong ("too hard", "make it slower", "the CPU is too perfect").
- Ask for changes in terms of *feel*. Specific numbers are optional, since all the tuning knobs are named `#define`s.
- Claude can't see the emulator window, so the human playtest is the real test.

## 9. Ideas for next time

- **A menu ROM** that holds several mini games in one file (Pico Park style collection). It needs an MBC (e.g. MBC5) once it passes 32 KB.
- **Battery save** for high scores (MBC + SRAM).
- **Real portraits:** PNG faces drawn in Aseprite or Piskel, converted with `png2asset`.
- **Two-player games:** the Game Boy link cable, or pass-and-play turns.
