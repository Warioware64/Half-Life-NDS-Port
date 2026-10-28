// One pair of samples out (hlDspTick), and the main loop's part: pairs
// worked out until the ring between it and the interrupt is full
// (hlDspFill). What they work out is shared/dsp/hl_dsp.h's, bit for bit
// (tools/dspsim holds the two against each other).

#include <teak/asminc.h>

#include "macros.inc"

    .section .bss.hl_tick, "aw", %nobits
    .align 2
tk_left:    .space 2
tk_right:   .space 2
tk_wetL:    .space 2
tk_wetR:    .space 2
tk_have:    .space 2
tk_word:    .space 2
tk_next:    .space 2

// The music ring's next word in a1, and the ring moved on (a0, r0 used up).
.macro TAKE
    mov     [hlMusicRead], a0
    add     hlMusicRing, a0
    mov     a0l, r0
    mov     [r0], a1
    mov     [hlMusicRead], a0
    addv    0x1, a0l
    cmp     0x1000, a0
    brr     1f, lt
    clr     a0, always
1:
    mov     a0l, [hlMusicRead]
.endm

// The music is wrong, and is stopped: silence is better than what a decoder
// makes of the wrong words.
BEGIN_ASM_FUNC hlMusicFault

    clr     a0, always
    mov     a0l, [hlMusicOn]
    mov     a0l, [hlValue]
    mov     a0l, [hlValue+1]
    mov     a0l, [hlLast]
    mov     a0l, [hlLast+1]
    mov     0x1, a0
    mov     a0l, [hlFault]
    ret     always

// The next pair of the music, if there is one to be had: a0 = 1 if
// hlValue[] has moved on, 0 if not. A new block is begun here when the last
// is done.
BEGIN_ASM_FUNC hlMusicPair

    push    r0

    // How much is in the ring.
    mov     [hlMusicWrite], a0
    mov     [hlMusicRead], a1
    sub     a1, a0
    br      .Lpair_have, ge
    add     0x1000, a0
.Lpair_have:
    mov     a0l, [tk_have]

    mov     [hlBlockLeft], a0
    cmp     0x0u8, a0
    br      .Lpair_block, neq

    // A block's heading: its mark, and both decoders' states.
    mov     [tk_have], a0
    cmp     0x5u8, a0
    br      .Lpair_none, lt
    // (The mark is 0xB10C: as a word read with its sign, that much under
    // nothing -- and 0x4EF4 more is nothing.)
    TAKE
    add     0x4ef4, a1
    br      .Lpair_fault, neq
    TAKE
    mov     a1l, [hlValue]
    TAKE
    mov     a1l, [hlIndex]
    TAKE
    mov     a1l, [hlValue+1]
    TAKE
    mov     a1l, [hlIndex+1]
    mov     [hlIndex], a0
    cmp     0x0u8, a0
    br      .Lpair_fault, lt
    cmp     0x58, a0
    br      .Lpair_fault, gt
    mov     [hlIndex+1], a0
    cmp     0x0u8, a0
    br      .Lpair_fault, lt
    cmp     0x58, a0
    br      .Lpair_fault, gt
    mov     0x400, a0
    mov     a0l, [hlBlockLeft]
    clr     a0, always
    mov     a0l, [hlMusicHalf]
    mov     [tk_have], a0
    sub     0x5, a0
    mov     a0l, [tk_have]

.Lpair_block:
    mov     [tk_have], a0
    cmp     0x0u8, a0
    br      .Lpair_none, eq

    // Two pairs to a word: the low half first.
    mov     [hlMusicRead], a0
    add     hlMusicRing, a0
    mov     a0l, r0
    mov     [r0], a1
    mov     [hlMusicHalf], a0
    cmp     0x0u8, a0
    br      .Lpair_low, eq
    shfi    a1, a1, -8
.Lpair_low:
    mov     a1l, [tk_word]
    clr     a0, always
    call    hlDecode, always
    mov     [tk_word], a1
    shfi    a1, a1, -4
    mov     0x1, a0
    call    hlDecode, always

    mov     [hlMusicHalf], a0
    cmp     0x0u8, a0
    br      .Lpair_half, eq
    TAKE
    mov     [hlBlockLeft], a0
    sub     0x1, a0
    mov     a0l, [hlBlockLeft]
    clr     a0, always
    br      .Lpair_halved, always
.Lpair_half:
    mov     0x1, a0
.Lpair_halved:
    mov     a0l, [hlMusicHalf]

    // Both channels against the stops for a long run is not music.
    mov     [hlValue], a0
    sub     0x7fff, a0
    br      .Lpair_pinned, eq
    mov     [hlValue], a0
    add     0x7fff, a0
    br      .Lpair_pinned, eq
    mov     [hlValue+1], a0
    sub     0x7fff, a0
    br      .Lpair_pinned, eq
    mov     [hlValue+1], a0
    add     0x7fff, a0
    br      .Lpair_pinned, eq
    clr     a0, always
    mov     a0l, [hlPinned]
    br      .Lpair_one, always
.Lpair_pinned:
    mov     [hlPinned], a0
    addv    0x1, a0l
    mov     a0l, [hlPinned]
    cmp     0x40, a0
    br      .Lpair_one, le
    call    hlMusicFault, always
.Lpair_one:
    mov     0x1, a0
    pop     r0
    ret     always

.Lpair_fault:
    call    hlMusicFault, always
.Lpair_none:
    clr     a0, always
    pop     r0
    ret     always

// One pair of samples out: hlOutL and hlOutR.
BEGIN_ASM_FUNC hlDspTick

    push    y0
    push    r0

    clr     a0, always
    mov     a0l, [tk_left]
    mov     a0l, [tk_right]

    mov     [hlMusicOn], a0
    cmp     0x0u8, a0
    br      .Ltick_echo, eq
    mov     [hlTick], a0
    and     0x1, a0
    br      .Ltick_odd, neq

    // A new pair, if the ring has one; the tick between two pairs is half
    // way from one to the other.
    mov     [hlValue], a0
    mov     a0l, [hlLast]
    mov     [hlValue+1], a0
    mov     a0l, [hlLast+1]
    call    hlMusicPair, always
    cmp     0x0u8, a0
    br      .Ltick_fed, neq
    mov     [hlDry], a0
    cmp     0x7fff, a0
    br      .Ltick_fed, ge
    addv    0x1, a0l
    mov     a0l, [hlDry]
.Ltick_fed:
    mov     [hlLast], a1
    mov     [hlValue], a0
    add     a0, a1
    SHIFTZ  a1, 1
    mov     a1l, [tk_left]
    mov     [hlLast+1], a1
    mov     [hlValue+1], a0
    add     a0, a1
    SHIFTZ  a1, 1
    mov     a1l, [tk_right]
    br      .Ltick_echo, always
.Ltick_odd:
    mov     [hlValue], a0
    mov     a0l, [tk_left]
    mov     [hlValue+1], a0
    mov     a0l, [tk_right]

.Ltick_echo:
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
    mov     [tk_left], a0
    add     a0, a1
    CLAMP
    mov     a1l, [hlOutL]
    mov     [hlWetR], a1
    mov     [tk_right], a0
    add     a0, a1
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
