// Tiny music player shared by all games.
//   Channel 3 (wave)  = melody
//   Channel 1 (pulse) = bass
//   Channel 4 (noise) = drums
// Channel 2 is left free for sound effects.
//
// A song is three arrays of equal length, one entry per step (an 8th note):
//   melody / bass: N_REST, N_HOLD (keep the previous note ringing), or a note name below
//   drums: D_NONE, D_KICK, D_SNARE, D_HAT
// Call music_tick() once per frame.
#ifndef MUSIC_H
#define MUSIC_H
#include <gb/gb.h>
#include <stdint.h>

#define N_REST 0
#define N_HOLD 1

// Melody notes (channel 3)
enum { M_C4 = 2, M_D4, M_E4, M_F4, M_G4, M_A4, M_B4,
       M_C5, M_D5, M_E5, M_F5, M_G5, M_A5, M_B5, M_C6 };
static const uint16_t ch3_notes[] = { 0, 0,
    1798, 1825, 1849, 1860, 1881, 1899, 1915,
    1923, 1936, 1949, 1954, 1964, 1974, 1982, 1985 };

// Bass notes (channel 1)
enum { B_E2 = 2, B_F2, B_G2, B_A2, B_C3, B_D3, B_E3, B_F3, B_G3, B_A3, B_C4 };
static const uint16_t ch1_notes[] = { 0, 0,
    458, 547, 710, 856, 1046, 1155, 1253, 1297, 1379, 1452, 1547 };

enum { D_NONE, D_KICK, D_SNARE, D_HAT };

typedef struct {
    const uint8_t *melody;
    const uint8_t *bass;
    const uint8_t *drums;
    uint8_t len;            // number of steps
    uint8_t step_frames;    // frames per step (bigger = slower)
} song_t;

// Soft, flute-like waveform for the melody
static const uint8_t wave_soft[16] = {
    0x8A, 0xCD, 0xEF, 0xFF, 0xFF, 0xFE, 0xDC, 0xA8,
    0x75, 0x32, 0x10, 0x00, 0x00, 0x01, 0x23, 0x57 };

static const song_t *mus_song;
static uint8_t mus_on = 1, mus_step, mus_timer;

static void music_init(void) {
    uint8_t i;
    volatile uint8_t *wave = (volatile uint8_t *)0xFF30;
    NR30_REG = 0x00;                    // wave RAM can only be written with ch3 off
    for (i = 0; i < 16; i++) wave[i] = wave_soft[i];
    NR30_REG = 0x80;
    NR32_REG = 0x00;
}

static void music_play(const song_t *s) {
    mus_song = s;
    mus_step = 0;
    mus_timer = 0;
}

static void music_silence(void) {
    NR32_REG = 0x00;                    // melody off
    NR12_REG = 0x00;                    // bass DAC off
}

static void music_toggle(void) {
    mus_on ^= 1;
    if (!mus_on) music_silence();
}

static void music_tick(void) {
    uint8_t n;
    uint16_t f;
    if (!mus_on || !mus_song) return;
    if (mus_timer) { mus_timer--; return; }
    mus_timer = mus_song->step_frames - 1;

    n = mus_song->melody[mus_step];
    if (n == N_REST) NR32_REG = 0x00;
    else if (n != N_HOLD) {
        f = ch3_notes[n];
        NR32_REG = 0x40;                // 100% volume... of a quiet wave
        NR33_REG = (uint8_t)f;
        NR34_REG = 0x80 | (uint8_t)(f >> 8);
    }

    n = mus_song->bass[mus_step];
    if (n == N_REST) NR12_REG = 0x00;
    else if (n != N_HOLD) {
        f = ch1_notes[n];
        NR10_REG = 0x00;
        NR11_REG = 0x40;                // 25% duty
        NR12_REG = 0x52;                // volume 5, plucky decay
        NR13_REG = (uint8_t)f;
        NR14_REG = 0x80 | (uint8_t)(f >> 8);
    }

    switch (mus_song->drums[mus_step]) {
        case D_KICK:  NR41_REG = 0; NR42_REG = 0x91; NR43_REG = 0x71; NR44_REG = 0x80; break;
        case D_SNARE: NR41_REG = 0; NR42_REG = 0x71; NR43_REG = 0x42; NR44_REG = 0x80; break;
        case D_HAT:   NR41_REG = 0; NR42_REG = 0x31; NR43_REG = 0x10; NR44_REG = 0x80; break;
    }

    if (++mus_step >= mus_song->len) mus_step = 0;
}

#endif
