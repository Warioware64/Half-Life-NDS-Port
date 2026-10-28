// The program's memory: everything shared/dsp/hl_dsp.h (its model) keeps,
// under the same names -- the simulator reads some of it by name. A symbol
// is a word's address; `.space` counts bytes, two to a word.

#include "hl_dsp_proto.h"

    .section .bss.hl_state, "aw", %nobits
    .align 2

// What the ARM9 writes into: asked for by address (HL_DSP_CMD_INFO).
    .global hlMusicRing, hlSendRing, hlRoomBlock
hlMusicRing:    .space HL_DSP_MUSIC_WORDS * 2
hlSendRing:     .space HL_DSP_SEND_WORDS * 2
hlRoomBlock:    .space HL_DSP_ROOM_WORDS * 2

// The room's lines: emptied when a room is taken (main.s), and not run
// before one has been.
    .global hlEr, hlFdn, hlDly
hlEr:           .space HL_DSP_ER_WORDS * 2
hlFdn:          .space 4 * HL_DSP_FDN_LINE * 2
hlDly:          .space HL_DSP_DLY_WORDS * 2

// Everything from here to hlStateEnd is cleared at the start (main.s):
// HL_STATE_WORDS words of it.
    .global hlState
hlState:
    .global hlOutRing, hlOutRead, hlOutWrite, hlPortL, hlPortR, hlStarved
hlOutRing:      .space HL_DSP_OUT_WORDS * 2
hlOutRead:      .space 2
hlOutWrite:     .space 2
hlPortL:        .space 2
hlPortR:        .space 2
hlStarved:      .space 2

    .global hlMusicRead, hlMusicWrite, hlMusicOn, hlMusicHalf
    .global hlBlockLeft, hlFault, hlPinned
hlMusicRead:    .space 2
hlMusicWrite:   .space 2
hlMusicOn:      .space 2
hlMusicHalf:    .space 2
hlBlockLeft:    .space 2
hlFault:        .space 2
hlPinned:       .space 2

    .global hlSendRead, hlSendWrite, hlTick, hlDry, hlHeard
hlSendRead:     .space 2
hlSendWrite:    .space 2
hlTick:         .space 2
hlDry:          .space 2
hlHeard:        .space 2

    .global hlValue, hlIndex, hlLast, hlOutL, hlOutR
hlValue:        .space 4
hlIndex:        .space 4
hlLast:         .space 4
hlOutL:         .space 2
hlOutR:         .space 2

// The room: its numbers as taken, where each line is, what each line's
// dulling holds, and what it puts out (HL_ROOM_STATE_WORDS words: main.s).
    .global hlRoom, hlLevel, hlErPos, hlFdnPos, hlDlyPos, hlFdnLow, hlDlyLast1, hlDlyLast2
    .global hlWetL, hlWetStepL, hlWetR, hlWetStepR, hlEchoIn, hlEchoOutL, hlEchoOutR
    .global hlRvD, hlRvM, hlRvTap
hlRoom:         .space HL_DSP_ROOM_WORDS * 2
hlLevel:        .space 2
hlErPos:        .space 2
hlFdnPos:       .space 8
hlDlyPos:       .space 2
hlFdnLow:       .space 8
hlDlyLast1:     .space 2
hlDlyLast2:     .space 2
hlWetL:         .space 2
hlWetStepL:     .space 2
hlWetR:         .space 2
hlWetStepR:     .space 2
hlEchoIn:       .space 2
hlEchoOutL:     .space 2
hlEchoOutR:     .space 2
hlRvD:          .space 8
hlRvM:          .space 8
hlRvTap:        .space 8

    .section .rodata.hl_tables, "a", %progbits
    .align 2

// IMA-ADPCM's steps, and how far the three low bits move along them.
    .global hlStepTable, hlIndexStep
hlStepTable:
    .short 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23
    .short 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80
    .short 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279
    .short 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963
    .short 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327
    .short 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487
    .short 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
hlIndexStep:
    .short -1, -1, -1, -1, 2, 4, 6, 8
