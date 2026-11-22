// "Paddle Party": an original bouncy loop for Ping Pong Pals.
// 8 bars of 8 eighth notes. Chords: C | Am | F | G | C | Am | F G | C
#ifndef SONG_H
#define SONG_H
#include "../common/music.h"

#define _ N_HOLD
#define R N_REST

static const uint8_t pp_melody[64] = {
    M_E5, M_G5, M_C6, M_G5,  M_E5, _,    M_D5, M_C5,   // C
    M_A4, M_C5, M_E5, M_C5,  M_A4, _,    M_B4, M_C5,   // Am
    M_F5, M_E5, M_D5, M_C5,  M_A4, _,    M_C5, M_D5,   // F
    M_G5, _,    M_F5, M_E5,  M_D5, _,    M_B4, _,      // G
    M_E5, M_G5, M_C6, M_G5,  M_E5, _,    M_G5, M_A5,   // C
    M_A5, M_G5, M_E5, M_C5,  M_E5, _,    M_A5, _,      // Am
    M_F5, M_A5, M_G5, M_F5,  M_D5, M_G5, M_F5, M_D5,   // F  G
    M_C5, M_E5, M_G5, M_C6,  _,    _,    R,    R,      // C
};

static const uint8_t pp_bass[64] = {
    B_C3, B_C4, B_C3, B_C4,  B_C3, B_C4, B_C3, B_C4,
    B_A2, B_A3, B_A2, B_A3,  B_A2, B_A3, B_A2, B_A3,
    B_F2, B_F3, B_F2, B_F3,  B_F2, B_F3, B_F2, B_F3,
    B_G2, B_G3, B_G2, B_G3,  B_G2, B_G3, B_G2, B_G3,
    B_C3, B_C4, B_C3, B_C4,  B_C3, B_C4, B_C3, B_C4,
    B_A2, B_A3, B_A2, B_A3,  B_A2, B_A3, B_A2, B_A3,
    B_F2, B_F3, B_F2, B_F3,  B_G2, B_G3, B_G2, B_G3,
    B_C3, B_C4, B_C3, B_C4,  B_G2, B_G3, B_C3, R,
};

#define K D_KICK
#define S D_SNARE
#define H D_HAT
static const uint8_t pp_drums[64] = {
    K, H, S, H,  K, H, S, H,
    K, H, S, H,  K, H, S, H,
    K, H, S, H,  K, H, S, H,
    K, H, S, H,  K, H, S, H,
    K, H, S, H,  K, H, S, H,
    K, H, S, H,  K, H, S, H,
    K, H, S, H,  K, H, S, H,
    K, H, S, H,  S, S, S, S,   // little drum fill
};
#undef K
#undef S
#undef H
#undef _
#undef R

static const song_t paddle_party = { pp_melody, pp_bass, pp_drums, 64, 11 };

#endif
