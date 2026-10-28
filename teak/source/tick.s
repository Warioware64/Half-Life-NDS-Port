// One pair of samples out (hlDspTick): the room, a new sample of it every
// fourth, drawn in straight lines between; and the main loop's part: pairs
// worked out until the ring between it and the interrupt is full
// (hlDspFill). What they work out is shared/dsp/hl_dsp.h's, bit for bit
// (tools/dspsim holds the two against each other).

#include <teak/asminc.h>

#include "macros.inc"

    .section .bss.hl_tick, "aw", %nobits
    .align 2
tk_wetL:    .space 2
tk_wetR:    .space 2
tk_next:    .space 2

// One pair of samples out: hlOutL and hlOutR.
BEGIN_ASM_FUNC hlDspTick

    push    y0
    push    r0

    mov     [hlTick], a0
    and     0x3, a0
    br      .Ltick_wet, neq

    // A new sample of the room every fourth tick, and a straight line to
    // it over the four, on each side.
    clr     a0, always
    mov     a0l, [tk_wetL]
    mov     a0l, [tk_wetR]
    mov     a0l, [hlEchoIn]
    mov     [hlSendRead], a0
    mov     [hlSendWrite], a1
    sub     a1, a0
    br      .Ltick_sent, eq
    mov     [hlSendRead], a0
    add     hlSendRing, a0
    mov     a0l, r0
    mov     [r0], a1
    mov     [hlHeard], a0
    sub     a1, a0
    br      .Ltick_quiet, ge
    mov     a1l, [hlHeard]
.Ltick_quiet:
    SHIFTZ  a1, 2
    mov     a1l, [hlEchoIn]
    mov     [hlSendRead], a0
    addv    0x1, a0l
    cmp     0x800, a0
    br      .Ltick_round, lt
    clr     a0, always
.Ltick_round:
    mov     a0l, [hlSendRead]
.Ltick_sent:
    mov     [hlLevel], a0
    cmp     0x0u8, a0
    br      .Ltick_step, eq
    call    hlReverb, always
    mov     [hlLevel], a0
    mov     a0l, y0
    mov     [hlEchoOutL], a0
    mpy     y0, a0l, a1
    clr     a1, always
    add     p*, a1
    SHIFTZ  a1, 6
    CLAMP
    mov     a1l, [tk_wetL]
    mov     [hlLevel], a0
    mov     a0l, y0
    mov     [hlEchoOutR], a0
    mpy     y0, a0l, a1
    clr     a1, always
    add     p*, a1
    SHIFTZ  a1, 6
    CLAMP
    mov     a1l, [tk_wetR]
.Ltick_step:
    mov     [tk_wetL], a1
    mov     [hlWetL], a0
    sub     a0, a1
    SHIFTZ  a1, 2
    mov     a1l, [hlWetStepL]
    mov     [tk_wetR], a1
    mov     [hlWetR], a0
    sub     a0, a1
    SHIFTZ  a1, 2
    mov     a1l, [hlWetStepR]

.Ltick_wet:
    mov     [hlWetL], a0
    mov     [hlWetStepL], a1
    add     a1, a0
    mov     a0l, [hlWetL]
    mov     [hlWetR], a0
    mov     [hlWetStepR], a1
    add     a1, a0
    mov     a0l, [hlWetR]
    mov     [hlTick], a0
    addv    0x1, a0l
    mov     a0l, [hlTick]

    mov     [hlWetL], a1
    CLAMP
    mov     a1l, [hlOutL]
    mov     [hlWetR], a1
    CLAMP
    mov     a1l, [hlOutR]

    pop     r0
    pop     y0
    ret     always

// The main loop's part: pairs worked out until the ring between it and the
// interrupt is full.
BEGIN_ASM_FUNC hlDspFill

    push    r0
.Lfill_more:
    mov     [hlOutWrite], a0
    addv    0x2, a0l
    cmp     0x80, a0
    br      .Lfill_next, lt
    clr     a0, always
.Lfill_next:
    mov     [hlOutRead], a1
    sub     a0, a1
    br      .Lfill_full, eq
    mov     a0l, [tk_next]
    call    hlDspTick, always
    mov     [hlOutWrite], a0
    add     hlOutRing, a0
    mov     a0l, r0
    mov     [hlOutL], a1
    mov     a1l, [r0++]
    mov     [hlOutR], a1
    mov     a1l, [r0]
    mov     [tk_next], a0
    mov     a0l, [hlOutWrite]
    br      .Lfill_more, always
.Lfill_full:
    pop     r0
    ret     always
