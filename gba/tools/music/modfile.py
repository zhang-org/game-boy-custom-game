"""Writes 4-channel ProTracker .mod files from songs written as note text.

Instruments are synthesised here (no sampled audio from anywhere else), so all music is our own.

A song is a dict:
    {
        "bpm": 120,                  # quarter notes per minute; one step = one eighth note
        "tracks": [                  # up to 4 tracks, played together
            ("lead", 40, "C5 - E5 - G5 . . . | ..."),
            ("bass", 48, "C3 - - - G2 - - - | ..."),
            ("drums", 40, "k . h . s . h . | ..."),
        ],
    }
Tokens (separated by spaces, '|' is ignored and just helps you count bars):
    C5, F#4 ...  play a note (octaves 3..5 for most instruments, 2..4 for "bass")
    -            keep holding the previous note
    .            silence (cuts the note)
    k s h j      drums (only on a "drums" track): kick, snare, hi-hat, jingle bells
All tracks should have the same number of steps; shorter ones are padded with silence.
"""
import math
import random
import struct

RATE = 8287          # playback rate of a sample at MOD note C-2 (period 428)
BASE_FREQ = RATE / 32.0  # a 32-sample single-cycle loop plays this pitch at C-2 (we call it "C4")

NOTE_INDEX = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}
PERIODS = [856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
           428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
           214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113]


def _clip(v):
    return max(-128, min(127, int(round(v))))


def _cycle(fn, n=32, amp=100):
    return bytes((_clip(fn(i / n) * amp)) & 0xFF for i in range(n))


def _oneshot(fn, seconds, amp=110):
    n = int(RATE * seconds)
    return bytes((_clip(fn(i / RATE) * amp)) & 0xFF for i in range(n))


def _square(duty):
    return lambda t: 1.0 if (t % 1.0) < duty else -1.0


def _triangle(t):
    t = t % 1.0
    return 4 * t - 1 if t < 0.5 else 3 - 4 * t


def _flute(t):
    return 0.8 * math.sin(2 * math.pi * t) + 0.2 * math.sin(4 * math.pi * t)


def _bell(t):
    f = BASE_FREQ
    env = math.exp(-t * 5.0)
    shimmer = math.exp(-t * 9.0)
    return env * (0.65 * math.sin(2 * math.pi * f * t) + 0.25 * shimmer * math.sin(2 * math.pi * f * 2 * t)
                  + 0.18 * shimmer * math.sin(2 * math.pi * f * 3.01 * t)
                  + 0.1 * shimmer * math.sin(2 * math.pi * f * 4.2 * t))


def _pluck(t):
    f = BASE_FREQ
    env = math.exp(-t * 7.0)
    return env * (0.7 * _triangle(f * t) + 0.3 * math.sin(2 * math.pi * f * 2 * t))


_rng = random.Random(1225)
_noise = [(_rng.random() * 2 - 1) for _ in range(RATE)]


def _kick(t):
    freq = 120 * math.exp(-t * 20) + 45
    phase = 2 * math.pi * (45 * t + (120 / 20) * (1 - math.exp(-t * 20)))
    return math.sin(phase) * math.exp(-t * 14) * 1.1 + 0 * freq


def _snare(t):
    i = int(t * RATE) % RATE
    return (0.75 * _noise[i] + 0.35 * math.sin(2 * math.pi * 190 * t)) * math.exp(-t * 22)


def _hat(t):
    i = int(t * RATE) % RATE
    hp = _noise[i] - _noise[i - 1]
    return 0.6 * hp * math.exp(-t * 70)


def _jingle(t):
    # Sleigh bells: bright noise with a few quick shakes.
    i = int(t * RATE) % RATE
    hp = _noise[i] - _noise[i - 1]
    shake = sum(math.exp(-max(0.0, t - d) * 45) if t >= d else 0 for d in (0.0, 0.035, 0.07))
    ring = 0.25 * math.sin(2 * math.pi * 2093 * t) + 0.2 * math.sin(2 * math.pi * 2637 * t)
    return (0.45 * hp + ring * 0.5) * shake * 0.8


# name -> (sample bytes, loop?, octave offset, default volume)
# octave offset: MOD octave = our octave - offset.
INSTRUMENTS = {
    "lead": (_cycle(_square(0.25), amp=60), True, 2),
    "square": (_cycle(_square(0.5), amp=50), True, 2),
    "flute": (_cycle(_flute, amp=105), True, 2),
    "bell": (_oneshot(_bell, 0.9), False, 2),
    "pluck": (_oneshot(_pluck, 0.5), False, 2),
    "bass": (_cycle(_triangle, n=64, amp=110), True, 1),
    "kick": (_oneshot(_kick, 0.22), False, 2),
    "snare": (_oneshot(_snare, 0.2), False, 2),
    "hat": (_oneshot(_hat, 0.06), False, 2),
    "jingle": (_oneshot(_jingle, 0.22), False, 2),
}
ORDER = list(INSTRUMENTS.keys())
DRUMS = {"k": "kick", "s": "snare", "h": "hat", "j": "jingle"}


def _parse(text):
    return [tok for tok in text.replace("|", " ").split() if tok]


def _note_period(tok, octave_offset):
    name = tok[:-1]
    octave = int(tok[-1])
    if name not in NOTE_INDEX:
        raise ValueError(f"bad note {tok}")
    mod_oct = octave - octave_offset
    idx = (mod_oct - 1) * 12 + NOTE_INDEX[name]
    if not 0 <= idx < len(PERIODS):
        raise ValueError(f"note {tok} out of range for this instrument")
    return PERIODS[idx]


def build_mod(song, title="snowberry"):
    bpm = song["bpm"]
    tracks = song["tracks"]
    if len(tracks) > 4:
        raise ValueError("max 4 tracks")
    parsed = [(inst, vol, _parse(notes)) for inst, vol, notes in tracks]
    steps = max(len(p[2]) for p in parsed)
    # One sample slot per (instrument, volume) so notes never need a volume effect.
    slots = []

    def slot(name, vol):
        key = (name, max(1, min(64, vol)))
        if key not in slots:
            slots.append(key)
        return slots.index(key) + 1

    cells = [[[0, 0, 0, 0] for _ in range(4)] for _ in range(steps)]
    for ch, (inst, vol, toks) in enumerate(parsed):
        sounding = False
        for row in range(steps):
            tok = toks[row] if row < len(toks) else "."
            if tok == "-":
                continue
            if tok == ".":
                if sounding:
                    cells[row][ch] = [0, 0, 0xC, 0]
                    sounding = False
                continue
            if inst == "drums":
                if tok not in DRUMS:
                    raise ValueError(f"bad drum {tok}")
                name, period = DRUMS[tok], PERIODS[12]
            else:
                name, period = inst, _note_period(tok, INSTRUMENTS[inst][2])
            cells[row][ch] = [slot(name, vol), period, 0, 0]
            sounding = True
    if len(slots) > 31:
        raise ValueError("too many instrument/volume combinations (max 31)")
    while len(cells) % 64 and len(cells) < 2:
        cells.append([[0, 0, 0, 0] for _ in range(4)])

    def put_effect(row, eff, prm):
        for ch in range(4):
            if cells[row][ch][2] == 0 and cells[row][ch][3] == 0:
                cells[row][ch][2], cells[row][ch][3] = eff, prm
                return
        raise ValueError(f"no free channel for effect on row {row}")

    # Speed 6 ticks per row; at tempo T there are T*2/5 ticks per second.
    # One row is an eighth note, so T = bpm / 2.
    tempo = max(32, min(255, int(round(bpm / 2))))
    put_effect(0, 0xF, 6)
    put_effect(0, 0xF, tempo)
    if steps % 64:
        put_effect(steps - 1, 0xD, 0)
    patterns = []
    for start in range(0, steps, 64):
        rows = cells[start:start + 64]
        while len(rows) < 64:
            rows.append([[0, 0, 0, 0] for _ in range(4)])
        patterns.append(rows)
    out = bytearray()
    out += title.encode()[:20].ljust(20, b"\0")
    for i in range(31):
        if i < len(slots):
            name, vol = slots[i]
            data, loop, _ = INSTRUMENTS[name]
            length = (len(data) + 1) // 2
            out += f"{name}{vol}".encode()[:22].ljust(22, b"\0")
            out += struct.pack(">HBBHH", length, 0, vol, 0, length if loop else 1)
        else:
            out += b"\0" * 22 + struct.pack(">HBBHH", 0, 0, 0, 0, 1)
    out += bytes([len(patterns), 127])
    out += bytes(list(range(len(patterns))) + [0] * (128 - len(patterns)))
    out += b"M.K."
    for rows in patterns:
        for row in rows:
            for smp, period, eff, prm in row:
                out += bytes([(smp & 0xF0) | (period >> 8), period & 0xFF, ((smp & 0x0F) << 4) | eff, prm])
    for name, _vol in slots:
        data = INSTRUMENTS[name][0]
        if len(data) % 2:
            data += b"\0"
        out += data
    return bytes(out)
