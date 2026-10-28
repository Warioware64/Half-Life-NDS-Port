// The program's memory: everything shared/dsp/hl_dsp.h (its model) keeps,
// under the same names -- the simulator reads some of it by name. A symbol
// is a word's address; `.space` counts bytes, two to a word.

#include "hl_dsp_proto.h"

    .section .bss.hl_state, "aw", %nobits
    .align 2

// What the ARM9 writes into: asked for by address (HL_DSP_CMD_INFO).
    .global hlSendRing, hlRoomBlock
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

    .global hlSendRead, hlSendWrite, hlTick, hlHeard
hlSendRead:     .space 2
hlSendWrite:    .space 2
hlTick:         .space 2
hlHeard:        .space 2

    .global hlOutL, hlOutR
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
