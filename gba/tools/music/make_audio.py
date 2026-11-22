"""Generates every music (.mod) and sound effect (.wav) file in gba/audio/.

    python3 tools/music/make_audio.py            # write everything
    python3 tools/music/make_audio.py --wav DIR  # also render the songs as .wav previews into DIR

Songs are in songs.py. Sound effects are defined at the bottom of this file: each one is a short
recipe of tones and noise, so they're easy to tweak.
"""
import math
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from modfile import build_mod  # noqa: E402
from songs import SONGS  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
OUT = os.path.join(ROOT, "audio")
SR = 22050


def write_wav(path, samples, rate=SR):
    """samples: floats in -1..1 -> 8-bit unsigned mono WAV."""
    data = bytes(max(0, min(255, int(round(s * 127 + 128)))) for s in samples)
    header = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", 36 + len(data), b"WAVE", b"fmt ", 16, 1, 1, rate, rate, 1, 8,
                         b"data", len(data))
    with open(path, "wb") as f:
        f.write(header + data)


# ----------------------------------------------------------------------------- sound effect synth
_rng = random.Random(42)


def tone(freq_start, freq_end, seconds, wave="square", vol=0.6, decay=4.0, duty=0.5):
    out = []
    n = int(SR * seconds)
    phase = 0.0
    for i in range(n):
        t = i / n
        f = freq_start * (freq_end / freq_start) ** t if freq_start > 0 and freq_end > 0 else freq_start
        phase += f / SR
        p = phase % 1.0
        if wave == "square":
            v = 1.0 if p < duty else -1.0
        elif wave == "triangle":
            v = 4 * p - 1 if p < 0.5 else 3 - 4 * p
        else:
            v = math.sin(2 * math.pi * p)
        out.append(v * vol * math.exp(-decay * t))
    return out


def noise(seconds, vol=0.6, decay=6.0, smooth=0.0):
    out = []
    n = int(SR * seconds)
    last = 0.0
    for i in range(n):
        v = _rng.random() * 2 - 1
        last = last * smooth + v * (1 - smooth)
        out.append(last * vol * math.exp(-decay * i / n))
    return out


def silence(seconds):
    return [0.0] * int(SR * seconds)


def mix(*parts):
    n = max(len(p) for p in parts)
    return [max(-1.0, min(1.0, sum(p[i] if i < len(p) else 0 for p in parts))) for i in range(n)]


def seq(*parts):
    out = []
    for p in parts:
        out += p
    return out


SFX = {
    # Menus
    "sfx_select": lambda: tone(1320, 1320, 0.04, vol=0.35, decay=2),
    "sfx_confirm": lambda: seq(tone(988, 988, 0.05, vol=0.4, decay=1), tone(1480, 1480, 0.09, vol=0.4, decay=3)),
    "sfx_back": lambda: seq(tone(880, 880, 0.05, vol=0.35, decay=1), tone(587, 587, 0.08, vol=0.35, decay=3)),
    "sfx_bump": lambda: tone(140, 90, 0.07, wave="triangle", vol=0.6, decay=3),
    "sfx_door": lambda: seq(tone(523, 523, 0.06, vol=0.35), tone(659, 659, 0.06, vol=0.35),
                            tone(784, 784, 0.12, vol=0.35, decay=3)),
    # Generic gameplay
    "sfx_point": lambda: seq(tone(1568, 1568, 0.05, vol=0.4, decay=0.5), tone(2093, 2093, 0.14, vol=0.4, decay=4)),
    "sfx_jump": lambda: tone(300, 900, 0.16, vol=0.4, decay=1.5, duty=0.25),
    "sfx_flap": lambda: mix(noise(0.09, vol=0.4, decay=5, smooth=0.6), tone(500, 700, 0.06, vol=0.2, decay=4)),
    "sfx_crash": lambda: mix(noise(0.45, vol=0.8, decay=5, smooth=0.3), tone(160, 50, 0.35, wave="triangle",
                                                                             vol=0.6, decay=3)),
    "sfx_hurt": lambda: seq(tone(440, 330, 0.08, vol=0.45, decay=1), tone(330, 220, 0.14, vol=0.45, decay=3)),
    "sfx_land": lambda: noise(0.08, vol=0.5, decay=6, smooth=0.7),
    "sfx_whoosh": lambda: noise(0.25, vol=0.35, decay=3, smooth=0.85),
    "sfx_countdown": lambda: tone(880, 880, 0.12, vol=0.4, decay=2),
    "sfx_go": lambda: tone(1760, 1760, 0.3, vol=0.4, decay=3),
    "sfx_record": lambda: seq(*[tone(f, f, 0.07, vol=0.35, decay=1) for f in (1047, 1319, 1568, 2093)],
                             tone(2637, 2637, 0.25, vol=0.35, decay=3)),
    # Ping pong
    "sfx_paddle": lambda: mix(tone(900, 600, 0.05, wave="triangle", vol=0.7, decay=5), noise(0.03, vol=0.3)),
    "sfx_smash": lambda: mix(tone(1200, 500, 0.09, wave="square", vol=0.5, decay=4),
                             noise(0.08, vol=0.5, decay=4, smooth=0.2)),
    "sfx_bounce": lambda: tone(700, 500, 0.05, wave="sine", vol=0.6, decay=5),
    # Present catch
    "sfx_catch": lambda: seq(tone(1175, 1175, 0.04, vol=0.4), tone(1568, 1568, 0.1, vol=0.4, decay=4)),
    "sfx_coal": lambda: tone(120, 80, 0.3, vol=0.45, decay=2, duty=0.3),
    # Fishing
    "sfx_cast": lambda: mix(noise(0.3, vol=0.3, decay=2, smooth=0.9), tone(900, 300, 0.3, wave="sine", vol=0.25)),
    "sfx_splash": lambda: noise(0.35, vol=0.55, decay=5, smooth=0.6),
    "sfx_bite": lambda: seq(tone(600, 1200, 0.05, wave="sine", vol=0.5, decay=0.5),
                           tone(1200, 600, 0.05, wave="sine", vol=0.5, decay=0.5)),
    "sfx_reel": lambda: seq(*[mix(noise(0.015, vol=0.35, decay=2), silence(0.03)) for _ in range(4)]),
    "sfx_miss": lambda: tone(400, 200, 0.25, vol=0.35, decay=2, duty=0.25),
    # Horse
    "sfx_gallop": lambda: seq(noise(0.03, vol=0.45, decay=8, smooth=0.8), silence(0.06),
                             noise(0.03, vol=0.35, decay=8, smooth=0.8)),
}


def _extra_sfx():
    """Extra sound effects from tools/music/sfx_<game>.py files (each defines an SFX dict)."""
    import importlib
    extra = {}
    here = os.path.dirname(os.path.abspath(__file__))
    for fname in sorted(os.listdir(here)):
        if fname.startswith("sfx_") and fname.endswith(".py"):
            mod = importlib.import_module(fname[:-3])
            extra.update(mod.SFX)
    return extra


def main():
    os.makedirs(OUT, exist_ok=True)
    SFX.update(_extra_sfx())
    for name, song in SONGS.items():
        with open(os.path.join(OUT, f"{name}.mod"), "wb") as f:
            f.write(build_mod(song, name))
    for name, fn in SFX.items():
        write_wav(os.path.join(OUT, f"{name}.wav"), fn())
    print(f"Wrote {len(SONGS)} songs and {len(SFX)} sound effects to {os.path.normpath(OUT)}")


if __name__ == "__main__":
    main()
