// The game's DSP program (DSi): the room (the room effects' Enhanced).
//
// All of it is assembly: this file is the start and the loop that takes the
// ARM9's commands; tick.s works a pair of samples out, reverb.s the room
// in it, irq.s hands the pairs to the speakers, data.s is its memory. What it must work out is written down in C in
// shared/dsp/hl_dsp.h, its model, and tools/dspsim holds the two against
// each other, sample for sample.
//
// (The library's own start-up code and its few set-up routines are called,
// once, at the start; nothing of theirs runs for a sample.)

#include <teak/asminc.h>

#include "hl_dsp_proto.h"

#define PORT_STAT 0x82c2
#define PORT_DATA 0x82c6
#define PORT_NOT_EMPTY 0x10
#define APBP_STAT 0x80d6
#define APBP_CMD0_NEW 0x100

// (data.s: from hlState to its end.)
#define HL_STATE_WORDS (HL_DSP_OUT_WORDS + 5 + 4 + 2 + HL_DSP_ROOM_WORDS + 32)

    .section .bss.hl_main, "aw", %nobits
    .align 2
mn_command: .space 2
mn_arg1:    .space 2
mn_arg2:    .space 2
mn_answer:  .space 2

BEGIN_ASM_FUNC main

    call    teakInit, always

    // Nothing is anything yet. (Not the rings the ARM9 fills: what is in
    // them is not looked at until it says there is something.)
    mov     hlState, r0
    mov     HL_STATE_WORDS, a0
    clr     a1, always
.Lmain_clear:
    mov     a1l, [r0++]
    sub     0x1, a0
    br      .Lmain_clear, neq

    // The speakers' port, asking for samples through CPU interrupt 1. One
    // pair of silence to start with: the interrupt is "the port has just
    // run out", and a port that has never had anything never runs out.
    clr     a0, always
    mov     0x1, a1
    call    btdmpSetupOutputSpeakers, always
    clr     a0, always
    mov     a0l, [PORT_DATA]
    mov     a0l, [PORT_DATA]
    call    cpuEnableInt1, always
    call    cpuEnableIrqs, always

.Lmain_loop:
    // Working out what is to be heard comes first, always; a command is
    // taken between two fillings of the ring.
    call    hlDspFill, always
    // The port found empty: it ran out before the interrupt got to it, and
    // it asks only when it runs out. It is started again from here.
    mov     PORT_STAT, r0
    tst0    PORT_NOT_EMPTY, [r0]
    br      .Lmain_fed, neq
    call    cpuDisableIrqs, always
    call    hlSendPair, always
    call    cpuEnableIrqs, always
.Lmain_fed:
    mov     APBP_STAT, r0
    tst0    APBP_CMD0_NEW, [r0]
    br      .Lmain_loop, eq

    // The arguments are written before the command, so they are there.
    clr     a0, always
    call    apbpReceiveData, always
    mov     a0l, [mn_command]
    mov     0x1, a0
    call    apbpReceiveData, always
    mov     a0l, [mn_arg1]
    mov     0x2, a0
    call    apbpReceiveData, always
    mov     a0l, [mn_arg2]
    clr     a0, always
    mov     a0l, [mn_answer]

    mov     [mn_command], a0
    cmp     HL_DSP_CMD_PING, a0
    br      .Lcmd_ping, eq
    cmp     HL_DSP_CMD_INFO, a0
    br      .Lcmd_info, eq
    cmp     HL_DSP_CMD_SEND_FED, a0
    br      .Lcmd_send, eq
    cmp     HL_DSP_CMD_ROOM, a0
    br      .Lcmd_room, eq
    cmp     HL_DSP_CMD_STATUS, a0
    br      .Lcmd_status, eq
    cmp     HL_DSP_CMD_HEARD, a0
    br      .Lcmd_heard, eq
    br      .Lcmd_done, always

.Lcmd_ping:
    mov     HL_DSP_MAGIC, a0
    mov     a0l, [mn_answer]
    br      .Lcmd_done, always

// Where the ARM9 is to write: addresses in the DSP's data memory.
.Lcmd_info:
    mov     [mn_arg1], a0
    mov     hlRoomBlock, a1
    cmp     HL_DSP_INFO_ROOM, a0
    br      .Lcmd_info_is, eq
    mov     hlSendRing, a1
.Lcmd_info_is:
    mov     a1l, [mn_answer]
    br      .Lcmd_done, always

// More has been written into the echo's ring: where to; the answer is
// where it has been read to.
.Lcmd_send:
    mov     [mn_arg1], a1
    cmp     0x0u8, a1
    br      .Lcmd_send_read, lt
    cmp     HL_DSP_SEND_WORDS, a1
    br      .Lcmd_send_read, ge
    mov     a1l, [hlSendWrite]
.Lcmd_send_read:
    mov     [hlSendRead], a0
    mov     a0l, [mn_answer]
    br      .Lcmd_done, always

// The room: its numbers taken from where the ARM9 wrote them, each line's
// length (and each tap) held within its line, and the lines emptied.
.Lcmd_room:
    mov     [hlRoomBlock+0], a0
    mov     a0l, [hlRoom+0]
    mov     [hlRoomBlock+1], a0
    mov     a0l, [hlRoom+1]
    mov     [hlRoomBlock+2], a0
    mov     a0l, [hlRoom+2]
    mov     [hlRoomBlock+3], a0
    mov     a0l, [hlRoom+3]
    mov     [hlRoomBlock+4], a0
    mov     a0l, [hlRoom+4]
    mov     [hlRoomBlock+5], a0
    mov     a0l, [hlRoom+5]
    mov     [hlRoomBlock+6], a0
    mov     a0l, [hlRoom+6]
    mov     [hlRoomBlock+7], a0
    mov     a0l, [hlRoom+7]
    mov     [hlRoomBlock+8], a0
    mov     a0l, [hlRoom+8]
    mov     [hlRoomBlock+9], a0
    mov     a0l, [hlRoom+9]
    mov     [hlRoomBlock+10], a0
    mov     a0l, [hlRoom+10]
    mov     [hlRoomBlock+11], a0
    mov     a0l, [hlRoom+11]
    mov     [hlRoomBlock+12], a0
    mov     a0l, [hlRoom+12]
    mov     [hlRoomBlock+13], a0
    mov     a0l, [hlRoom+13]
    mov     [hlRoomBlock+14], a0
    mov     a0l, [hlRoom+14]
    mov     [hlRoomBlock+15], a0
    mov     a0l, [hlRoom+15]
    mov     [hlRoomBlock+16], a0
    mov     a0l, [hlRoom+16]
    mov     [hlRoomBlock+17], a0
    mov     a0l, [hlRoom+17]
    mov     [hlRoom+HL_DSP_ROOM_ER_LEN], a0
    cmp     0x1, a0
    br      .Lroom_er_bad, lt
    cmp     HL_DSP_ER_WORDS, a0
    br      .Lroom_er_ok, le
.Lroom_er_bad:
    mov     0x1, a0
    mov     a0l, [hlRoom+HL_DSP_ROOM_ER_LEN]
.Lroom_er_ok:
    mov     [hlRoom+HL_DSP_ROOM_ER_TAP+0], a0
    cmp     0x0u8, a0
    br      .Lroom_tap0_bad, lt
    mov     [hlRoom+HL_DSP_ROOM_ER_LEN], a1
    sub     a1, a0
    br      .Lroom_tap0_ok, lt
.Lroom_tap0_bad:
    clr     a0, always
    mov     a0l, [hlRoom+HL_DSP_ROOM_ER_TAP+0]
.Lroom_tap0_ok:
    mov     [hlRoom+HL_DSP_ROOM_ER_TAP+1], a0
    cmp     0x0u8, a0
    br      .Lroom_tap1_bad, lt
    mov     [hlRoom+HL_DSP_ROOM_ER_LEN], a1
    sub     a1, a0
    br      .Lroom_tap1_ok, lt
.Lroom_tap1_bad:
    clr     a0, always
    mov     a0l, [hlRoom+HL_DSP_ROOM_ER_TAP+1]
.Lroom_tap1_ok:
    mov     [hlRoom+HL_DSP_ROOM_ER_TAP+2], a0
    cmp     0x0u8, a0
    br      .Lroom_tap2_bad, lt
    mov     [hlRoom+HL_DSP_ROOM_ER_LEN], a1
    sub     a1, a0
    br      .Lroom_tap2_ok, lt
.Lroom_tap2_bad:
    clr     a0, always
    mov     a0l, [hlRoom+HL_DSP_ROOM_ER_TAP+2]
.Lroom_tap2_ok:
    mov     [hlRoom+HL_DSP_ROOM_ER_TAP+3], a0
    cmp     0x0u8, a0
    br      .Lroom_tap3_bad, lt
    mov     [hlRoom+HL_DSP_ROOM_ER_LEN], a1
    sub     a1, a0
    br      .Lroom_tap3_ok, lt
.Lroom_tap3_bad:
    clr     a0, always
    mov     a0l, [hlRoom+HL_DSP_ROOM_ER_TAP+3]
.Lroom_tap3_ok:
    mov     [hlRoom+HL_DSP_ROOM_FDN_LEN+0], a0
    cmp     0x1, a0
    br      .Lroom_fdn0_bad, lt
    cmp     HL_DSP_FDN_LINE, a0
    br      .Lroom_fdn0_ok, le
.Lroom_fdn0_bad:
    mov     0x1, a0
    mov     a0l, [hlRoom+HL_DSP_ROOM_FDN_LEN+0]
.Lroom_fdn0_ok:
    mov     [hlRoom+HL_DSP_ROOM_FDN_LEN+1], a0
    cmp     0x1, a0
    br      .Lroom_fdn1_bad, lt
    cmp     HL_DSP_FDN_LINE, a0
    br      .Lroom_fdn1_ok, le
.Lroom_fdn1_bad:
    mov     0x1, a0
    mov     a0l, [hlRoom+HL_DSP_ROOM_FDN_LEN+1]
.Lroom_fdn1_ok:
    mov     [hlRoom+HL_DSP_ROOM_FDN_LEN+2], a0
    cmp     0x1, a0
    br      .Lroom_fdn2_bad, lt
    cmp     HL_DSP_FDN_LINE, a0
    br      .Lroom_fdn2_ok, le
.Lroom_fdn2_bad:
    mov     0x1, a0
    mov     a0l, [hlRoom+HL_DSP_ROOM_FDN_LEN+2]
.Lroom_fdn2_ok:
    mov     [hlRoom+HL_DSP_ROOM_FDN_LEN+3], a0
    cmp     0x1, a0
    br      .Lroom_fdn3_bad, lt
    cmp     HL_DSP_FDN_LINE, a0
    br      .Lroom_fdn3_ok, le
.Lroom_fdn3_bad:
    mov     0x1, a0
    mov     a0l, [hlRoom+HL_DSP_ROOM_FDN_LEN+3]
.Lroom_fdn3_ok:
    mov     [hlRoom+HL_DSP_ROOM_DLY_LEN], a0
    cmp     0x0u8, a0
    br      .Lroom_dly_bad, lt
    cmp     HL_DSP_DLY_WORDS, a0
    br      .Lroom_dly_ok, le
.Lroom_dly_bad:
    clr     a0, always
    mov     a0l, [hlRoom+HL_DSP_ROOM_DLY_LEN]
.Lroom_dly_ok:
    mov     [hlRoom+HL_DSP_ROOM_LEVEL], a0
    mov     a0l, [hlLevel]
    // The lines (hlEr, hlFdn, hlDly, one after another), and where each is
    // and what its dulling holds (hlErPos to hlDlyLast2).
    mov     hlEr, r0
    mov     HL_DSP_ER_WORDS + 4 * HL_DSP_FDN_LINE + HL_DSP_DLY_WORDS, a0
    clr     a1, always
.Lroom_clear:
    mov     a1l, [r0++]
    sub     0x1, a0
    br      .Lroom_clear, neq
    mov     hlErPos, r0
    mov     0xc, a0
.Lroom_clear_at:
    mov     a1l, [r0++]
    sub     0x1, a0
    br      .Lroom_clear_at, neq
    br      .Lcmd_done, always

// How often the speakers went short since last asked.
.Lcmd_status:
    mov     [hlStarved], a0
    mov     a0l, [mn_answer]
    clr     a0, always
    mov     a0l, [hlStarved]
    br      .Lcmd_done, always

.Lcmd_heard:
    mov     [hlHeard], a0
    mov     a0l, [mn_answer]
    clr     a0, always
    mov     a0l, [hlHeard]

// Answered: in register 1 the tick it took effect before (how the tests
// line the model up with the program), then the answer itself.
.Lcmd_done:
    mov     0x1, a0
    mov     [hlTick], a1
    call    apbpSendData, always
    clr     a0, always
    mov     [mn_answer], a1
    call    apbpSendData, always
    br      .Lmain_loop, always
