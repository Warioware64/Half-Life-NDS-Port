// The room: one sample in (hlEchoIn), two out (hlEchoOutL, hlEchoOutR) --
// the first reflections, the reverb's four lines through their Hadamard
// square, and the game's echo, as the room's numbers (hlRoom, taken by
// main.s) have them. What it works out is shared/dsp/hl_dsp.h's hlReverb(),
// bit for bit: the simulator holds the two against each other (tools/dspsim).
//
// Registers, for the whole of it: r4 = the reverb's dulling (damp, Q14),
// r5 = 16384 less it. Saturation is off (the start-up code's mod0), so a
// shift is a shift and a store is the low word.

#include <teak/asminc.h>

#include "hl_dsp_proto.h"
#include "macros.inc"

    .section .bss.hl_reverb, "aw", %nobits
    .align 2
rv_erL:     .space 2
rv_erR:     .space 2
rv_lateL:   .space 2
rv_lateR:   .space 2
rv_echo:    .space 2
rv_dly:     .space 2

// a1 = a1l * [q] over 2^15, towards nothing (a0, y0 used up).
.macro SCALE15 q
    mov     [\q], a0
    mov     a0l, y0
    mpy     y0, a1l, a1             // (a1 itself is left as it is)
    clr     a1, always
    add     p*, a1
    SHIFTZ  a1, 15
.endm

// One of the four places back along the first reflections' line, read into
// hlRvTap+index: the line's place less the tap, round its length.
.macro ERTAP index
    mov     [hlErPos], a0
    mov     [hlRoom+HL_DSP_ROOM_ER_TAP+\index], a1
    sub     a1, a0
    brr     1f, ge
    mov     [hlRoom+HL_DSP_ROOM_ER_LEN], a1
    add     a1, a0
1:
    add     hlEr, a0
    mov     a0l, r1
    mov     [r1], a0
    mov     a0l, [hlRvTap+\index]
.endm

// What comes round line `index`, dulled: hlFdnLow+index (r1, r3 used up).
.macro DULL index
    mov     [hlFdnPos+\index], a0
    add     hlFdn+\index*HL_DSP_FDN_LINE, a0
    mov     a0l, r1
    mov     [r1], a0
    mov     a0l, [hlRvD+\index]
    mov     a0l, r3
    clr     a1, always
    mov     r5, y0
    mpy     y0, r3, a1              // p = (16384 - damp) * it
    mov     [hlFdnLow+\index], a0
    mov     r4, y0
    mac     y0, a0l, a1             // a1 = p; p = damp * what it was
    add     p*, a1
    SHIFTZ  a1, 14
    mov     a1l, [hlFdnLow+\index]
.endm

// Back into line `index`: its mix, quieter, with what is new added (taken
// away on the odd lines); and the line moved on, round its length.
.macro FEED index, sign
    mov     [hlRvM+\index], a1
    SCALE15 hlRoom+HL_DSP_ROOM_FDN_GAIN
    mov     [hlEchoIn], a0
    \sign   a0, a1
    CLAMP
    mov     [hlFdnPos+\index], a0
    add     hlFdn+\index*HL_DSP_FDN_LINE, a0
    mov     a0l, r1
    mov     a1l, [r1]
    mov     [hlFdnPos+\index], a0
    addv    0x1, a0l
    mov     [hlRoom+HL_DSP_ROOM_FDN_LEN+\index], a1
    sub     a0, a1
    brr     1f, gt
    clr     a0, always
1:
    mov     a0l, [hlFdnPos+\index]
.endm

// m = the four dulled lines summed with signs s1, s2, s3 (the first always
// added), halved and held in sixteen bits, into hlRvM+index.
.macro MIX index, s1, s2, s3
    mov     [hlFdnLow], a1
    mov     [hlFdnLow+1], a0
    \s1     a0, a1
    mov     [hlFdnLow+2], a0
    \s2     a0, a1
    mov     [hlFdnLow+3], a0
    \s3     a0, a1
    SHIFTZ  a1, 1
    CLAMP
    mov     a1l, [hlRvM+\index]
.endm

// Two words summed, halved, held, and scaled by [q] into [out].
.macro PAIRMIX first, second, q, out
    mov     [\first], a1
    mov     [\second], a0
    add     a0, a1
    SHIFTZ  a1, 1
    CLAMP
    SCALE15 \q
    mov     a1l, [\out]
.endm

BEGIN_ASM_FUNC hlReverb

    push    y0
    push    r1
    push    r3
    push    r4
    push    r5
    pusha   b0

    // The first reflections: the line written, then read four places back.
    mov     [hlErPos], a0
    add     hlEr, a0
    mov     a0l, r1
    mov     [hlEchoIn], a0
    mov     a0l, [r1]
    ERTAP   0
    ERTAP   1
    ERTAP   2
    ERTAP   3
    mov     [hlErPos], a0
    addv    0x1, a0l
    mov     [hlRoom+HL_DSP_ROOM_ER_LEN], a1
    sub     a0, a1
    brr     1f, gt
    clr     a0, always
1:
    mov     a0l, [hlErPos]
    PAIRMIX hlRvTap, hlRvTap+2, hlRoom+HL_DSP_ROOM_ER_MIX, rv_erL
    PAIRMIX hlRvTap+1, hlRvTap+3, hlRoom+HL_DSP_ROOM_ER_MIX, rv_erR

    // The reverb: each line dulled...
    mov     [hlRoom+HL_DSP_ROOM_FDN_DAMP], a0
    mov     a0l, r4
    mov     0x4000, a1
    sub     a0, a1
    mov     a1l, r5
    DULL    0
    DULL    1
    DULL    2
    DULL    3
    // ... through the Hadamard square...
    MIX     0, add, add, add
    MIX     1, sub, add, sub
    MIX     2, add, sub, sub
    MIX     3, sub, sub, add
    // ... and back in.
    FEED    0, add
    FEED    1, sub
    FEED    2, add
    FEED    3, sub
    PAIRMIX hlFdnLow, hlFdnLow+2, hlRoom+HL_DSP_ROOM_LATE_MIX, rv_lateL
    PAIRMIX hlFdnLow+1, hlFdnLow+3, hlRoom+HL_DSP_ROOM_LATE_MIX, rv_lateR

    // The game's echo, if the room has one.
    clr     a0, always
    mov     a0l, [rv_echo]
    mov     [hlRoom+HL_DSP_ROOM_DLY_LEN], a0
    cmp     0x0u8, a0
    br      .Lrv_out, eq
    mov     [hlDlyPos], a0
    add     hlDly, a0
    mov     a0l, r1
    mov     [r1], a0
    mov     a0l, [rv_dly]
    mov     [hlRoom+HL_DSP_ROOM_DLY_LOW], a1
    cmp     0x0u8, a1
    br      .Lrv_bright, eq
    // (Dulled: two parts of the new to one each of the last two.)
    mov     [rv_dly], a1
    mov     [rv_dly], a0
    add     a0, a1
    mov     [hlDlyLast1], a0
    add     a0, a1
    mov     [hlDlyLast2], a0
    add     a0, a1
    mov     [hlDlyLast1], a0
    mov     a0l, [hlDlyLast2]
    mov     [rv_dly], a0
    mov     a0l, [hlDlyLast1]
    SHIFTZ  a1, 2
    mov     a1l, [rv_dly]
.Lrv_bright:
    mov     [rv_dly], a1
    SCALE15 hlRoom+HL_DSP_ROOM_DLY_GAIN
    mov     [hlEchoIn], a0
    add     a0, a1
    CLAMP
    mov     a1l, [r1]
    mov     [hlDlyPos], a0
    addv    0x1, a0l
    mov     [hlRoom+HL_DSP_ROOM_DLY_LEN], a1
    sub     a0, a1
    brr     1f, gt
    clr     a0, always
1:
    mov     a0l, [hlDlyPos]
    mov     [rv_dly], a1
    SCALE15 hlRoom+HL_DSP_ROOM_ECHO_MIX
    mov     a1l, [rv_echo]

.Lrv_out:
    // Both sides: their reflections, their reverb, the echo.
    mov     [rv_erL], a1
    mov     [rv_lateL], a0
    add     a0, a1
    mov     [rv_echo], a0
    add     a0, a1
    CLAMP
    mov     a1l, [hlEchoOutL]
    mov     [rv_erR], a1
    mov     [rv_lateR], a0
    add     a0, a1
    mov     [rv_echo], a0
    add     a0, a1
    CLAMP
    mov     a1l, [hlEchoOutR]

    popa    b0
    pop     r5
    pop     r4
    pop     r3
    pop     r1
    pop     y0
    ret     always
