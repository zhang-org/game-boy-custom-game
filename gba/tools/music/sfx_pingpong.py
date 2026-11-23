"""Extra Ping Pong sound effects (make_audio.py picks up every tools/music/sfx_<game>.py).

Run this file on its own to (re)write just these sounds:
    python3 gba/tools/music/sfx_pingpong.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_audio import mix, noise, seq, tone, write_wav  # noqa: E402

SFX = {
    # A perfectly timed smash: a sharp crack, a deep thump and a little sparkle.
    "sfx_pp_perfect": lambda: seq(
        mix(noise(0.10, vol=0.9, decay=7, smooth=0.0),
            tone(2200, 700, 0.07, wave="square", vol=0.55, decay=5),
            tone(180, 50, 0.22, wave="triangle", vol=0.8, decay=3)),
        tone(2637, 2637, 0.04, vol=0.3, decay=2),
        tone(3136, 3136, 0.09, vol=0.3, decay=4)),
    # The power meter is full.
    "sfx_pp_charged": lambda: tone(700, 1500, 0.08, wave="square", vol=0.3, decay=1.5, duty=0.25),
}

if __name__ == "__main__":
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "audio")
    for name, fn in SFX.items():
        write_wav(os.path.join(out, f"{name}.wav"), fn())
    print("Wrote", ", ".join(SFX))
