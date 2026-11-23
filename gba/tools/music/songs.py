"""All the music, written as note text (see modfile.py for the format). Original compositions.

Each song is 8 or 16 bars of 4/4; one step = an eighth note, so a bar is 8 steps.
To change a tune, edit it here and run:  python3 tools/music/make_audio.py
"""

NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
CHORDS = {
    "C": ["C", "E", "G"], "Dm": ["D", "F", "A"], "Em": ["E", "G", "B"], "F": ["F", "A", "C"],
    "G": ["G", "B", "D"], "Am": ["A", "C", "E"], "D": ["D", "F#", "A"], "A": ["A", "C#", "E"],
    "Bm": ["B", "D", "F#"], "Bb": ["A#", "D", "F"], "E": ["E", "G#", "B"], "C7": ["C", "E", "A#"],
}


def _halves(chords):
    """'C Am F G/A' -> list of chords per half bar ('G/A' = G then A in one bar)."""
    out = []
    for tok in chords.split():
        if "/" in tok:
            a, b = tok.split("/")
            out += [a, b]
        else:
            out += [tok, tok]
    return out


def bass(chords, pattern="R - - - 5 - - -", octave=2):
    """Bass line from chords. Pattern tokens per half bar: R root, 5 fifth, O root an octave up, - hold, . rest."""
    steps = pattern.split()
    out = []
    for i, ch in enumerate(_halves(chords)):
        root = CHORDS[ch][0]
        fifth = CHORDS[ch][2]
        r_oct = octave
        f_oct = octave if NOTES.index(fifth) > NOTES.index(root) else octave + 1
        half = steps[(i % 2) * 4:(i % 2) * 4 + 4] if len(steps) == 8 else steps[:4]
        for s in half:
            out.append({"R": f"{root}{r_oct}", "5": f"{fifth}{f_oct}", "O": f"{root}{r_oct + 1}"}.get(s, s))
    return " ".join(out)


def arp(chords, octave=4, pattern="1 - 2 - 3 - 2 -"):
    """Broken chords for a bell/pluck: 1/2/3 = chord tones (going up), - hold, . rest."""
    steps = pattern.split()
    out = []
    for i, ch in enumerate(_halves(chords)):
        tones = CHORDS[ch]
        notes = []
        o = octave
        prev = -1
        for t in tones:
            if NOTES.index(t) < prev:
                o += 1
            prev = NOTES.index(t)
            notes.append(f"{t}{o}")
        half = steps[(i % 2) * 4:(i % 2) * 4 + 4]
        for s in half:
            out.append(notes[int(s) - 1] if s in "123" else s)
    return " ".join(out)


def times(text, n):
    return " ".join([text] * n)


# ------------------------------------------------------------------------------------ town map
_map_chords = "C Am F G C Am Dm G F G Em Am F G C C"
MAP = {
    "bpm": 104,
    "tracks": [
        ("flute", 42,
         "E4 - G4 - E4 D4 C4 - | A3 - C4 - E4 - - - | F4 - E4 D4 C4 - A3 - | G3 - - - B3 - D4 - |"
         "E4 - G4 - C5 - B4 A4 | G4 - E4 - C4 - - - | D4 - F4 - A4 - G4 F4 | E4 - D4 - - - . . |"
         "A3 - C4 - F4 - E4 - | D4 - B3 - G3 - - - | G3 - B3 - E4 - D4 - | C4 - E4 - A4 - - - |"
         "A4 - G4 F4 E4 - F4 - | G4 - F4 E4 D4 - B3 - | C4 - - - E4 - G4 - | C5 - - - - - . . |"),
        ("bell", 22, arp(_map_chords, 4, "1 - - - 3 - - -")),
        ("bass", 44, bass(_map_chords, "R - - - 5 - - -", 2)),
        ("drums", 26, times("k . j . s . j j", 16)),
    ],
}

# ------------------------------------------------------------------------------------ jingles
JINGLE_OVER = {
    "bpm": 132,
    "tracks": [
        ("pluck", 48, "G4 - E4 - C4 - D4 - B3 - - - C4 - - - . . . ."),
        ("bass", 40, "C3 - - - G2 - - - G2 - - - C3 - - - . . . ."),
    ],
}
JINGLE_RECORD = {
    "bpm": 150,
    "tracks": [
        ("lead", 40, "C4 E4 G4 C5 - G4 C5 - E5 - - - D5 E5 G5 - - - - - . . . ."),
        ("bell", 34, "C5 - - - - - - - G4 - - - B4 - - - C5 - - - - - . ."),
        ("bass", 44, "C3 - - - G2 - - - C3 - - - G2 - - - C3 - - - - - . ."),
        ("drums", 34, "k . s . k . s . k s k s k . s . j . . . j . . ."),
    ],
}

# ------------------------------------------------------------------------------------ games
# Jingle Flap: a bright, quick sleigh-ride tune in G (16 bars), with jingle bells on the off-beats.
_flappy_chords = "G D Em C G D C D Em C G D C D G G"
FLAPPY = {
    "bpm": 160,
    "tracks": [
        ("flute", 40,
         "D5 - B4 - G4 - B4 D5 | F#5 - E5 D5 A4 - - - | G4 - B4 - E5 - D5 B4 | C5 - E5 - G5 - E5 - |"
         "D5 - B4 - G4 A4 B4 - | A4 - F#4 - D4 - A4 - | G4 - E4 - C5 - B4 A4 | A4 - - - D5 - . . |"
         "E5 - E5 - B4 - E5 - | G5 - E5 - C5 - E5 - | D5 - B4 - D5 - G5 - | F#5 - E5 - D5 - C5 - |"
         "E5 - G5 - E5 - C5 - | D5 - F#5 - A5 - F#5 - | G5 - D5 - B4 - D5 - | G5 - - - . . . . |"),
        ("bell", 18, arp(_flappy_chords, 4, "1 3 2 3 1 3 2 3")),
        ("bass", 46, bass(_flappy_chords, "R . O . 5 . O .", 2)),
        ("drums", 30, times("k j h j s j h j", 16)),
    ],
}

_pong_chords = "G Em C D G Em Am/D G"
PINGPONG = {
    "bpm": 140,
    "tracks": [
        ("pluck", 50,
         "G4 B4 D5 B4 G4 - D5 - | E5 - B4 - G4 B4 E5 - | E5 D5 C5 - G4 - C5 - | D5 - F#4 - A4 - D5 - |"
         "B4 D5 G5 D5 B4 - G4 - | E5 G5 E5 B4 G4 - B4 - | A4 C5 E5 - F#4 A4 D5 - | G4 - B4 - G4 . . . |"),
        ("square", 18, arp(_pong_chords, 3, "1 . 2 . 3 . 2 .")),
        ("bass", 46, bass(_pong_chords, "R . 5 . R . 5 .", 2)),
        ("drums", 28, times("k h s h k k s h", 8)),
    ],
}

_sled_chords = "D A Bm G D A G/A D"
SLED = {
    "bpm": 168,
    "tracks": [
        ("lead", 38,
         "F#4 A4 D5 A4 F#4 A4 D5 E5 | E5 C#5 A4 C#5 E5 - . . | D5 B4 F#4 B4 D5 F#5 - . | G4 B4 D5 G5 - D5 B4 - |"
         "A4 D5 F#5 - E5 D5 C#5 D5 | E5 - A4 - C#5 - E5 - | D5 - B4 - E5 - C#5 - | D5 - A4 - D4 - . . |"),
        ("bell", 18, arp(_sled_chords, 4, "1 . 3 . 2 . 3 .")),
        ("bass", 48, bass(_sled_chords, "R R 5 5 R R 5 5", 2)),
        ("drums", 32, times("k h s h k k s j", 8)),
    ],
}

_catch_chords = "C F G C Am F G C"
CATCH = {
    "bpm": 132,
    "tracks": [
        ("bell", 40,
         "C5 - E5 - G5 - E5 - | F5 - A4 - C5 - F5 - | D5 - G4 - B4 - D5 - | E5 - C5 - G4 - . . |"
         "A4 - C5 - E5 - A5 - | F5 - E5 - D5 - C5 - | B4 - D5 - G5 - F5 - | E5 - C5 - C4 . . . |"),
        ("square", 18, arp(_catch_chords, 3, "1 2 3 2 1 2 3 2")),
        ("bass", 46, bass(_catch_chords, "R . 5 . O . 5 .", 2)),
        ("drums", 30, times("k . j . s . j .", 8)),
    ],
}

_fish_chords = "Am F C G Am F G Am"
FISHING = {
    "bpm": 92,
    "tracks": [
        ("flute", 40,
         "E4 - A4 - C5 - B4 A4 | A4 - - - F4 - - - | G4 - C5 - E5 - D5 C5 | B4 - - - G4 - - - |"
         "E4 - A4 - C5 - E5 - | D5 - C5 - A4 - - - | B4 - D5 - G4 - B4 - | A4 - - - - - . . |"),
        ("bell", 20, arp(_fish_chords, 4, "1 - - - 2 - 3 -")),
        ("bass", 40, bass(_fish_chords, "R - - - 5 - - -", 2)),
        ("drums", 14, times("j . . . h . . .", 8)),
    ],
}

_horse_chords = "G C G D G C D G"
HORSE = {
    "bpm": 152,
    "tracks": [
        ("lead", 38,
         "D5 - B4 G4 - B4 D5 - | E5 - C5 G4 - C5 E5 - | D5 - B4 G4 - B4 D5 G5 | F#5 - D5 A4 - F#4 A4 - |"
         "B4 - G4 D4 - G4 B4 - | C5 - E5 G5 - E5 C5 - | A4 - F#4 D5 - C5 A4 F#4 | G4 - - - G4 . . . |"),
        ("pluck", 22, arp(_horse_chords, 3, "1 . 2 3 . 2 1 .")),
        ("bass", 46, bass(_horse_chords, "R . . 5 . . R .", 2)),
        ("drums", 30, times("k . h k . h s .", 8)),
    ],
}

SONGS = {
    "map_song": MAP,
    "jingle_over": JINGLE_OVER,
    "jingle_record": JINGLE_RECORD,
    "flappy_song": FLAPPY,
    "pingpong_song": PINGPONG,
    "sled_song": SLED,
    "catch_song": CATCH,
    "fishing_song": FISHING,
    "horse_song": HORSE,
}
