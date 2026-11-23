"""Extra Pony Gallop sound effects (make_audio.py picks up every tools/music/sfx_<game>.py).

Run this file on its own to (re)write just these sounds:
    python3 gba/tools/music/sfx_horse.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_audio import mix, noise, seq, tone, write_wav  # noqa: E402

SFX = {
    # A PERFECT tap on the hoofbeat: a bright little bell.
    "sfx_hb_perfect": lambda: mix(tone(1760, 1760, 0.09, wave="sine", vol=0.45, decay=4),
                                  tone(2637, 2637, 0.06, wave="sine", vol=0.25, decay=5)),
    # A GOOD tap: a soft wooden tick.
    "sfx_hb_good": lambda: tone(1200, 900, 0.04, wave="triangle", vol=0.4, decay=5),
    # The horse is tired: a sagging snort.
    "sfx_hb_tired": lambda: seq(mix(noise(0.18, vol=0.4, decay=3, smooth=0.85),
                                    tone(330, 220, 0.18, wave="triangle", vol=0.35, decay=1)),
                                tone(220, 150, 0.22, wave="triangle", vol=0.35, decay=2)),
}

if __name__ == "__main__":
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "audio")
    for name, fn in SFX.items():
        write_wav(os.path.join(out, f"{name}.wav"), fn())
    print("Wrote", ", ".join(SFX))
