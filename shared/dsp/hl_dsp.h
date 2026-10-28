/* What the DSi's DSP does for the game's sound: the room (the room
 * effects' Enhanced).
 *
 * This file is the DSP program's MODEL. The program itself is assembly
 * (teak/source/*.s), written to give the same samples as this, bit for bit:
 * the host tests run this, and the simulator's harness (tools/dspsim) runs
 * both -- the real program in an emulated DSP -- and compares every pair
 * that comes out. Anything the program is to do is written here first.
 * (What the two share by name -- the numbers of the commands, the sizes of
 * the rings -- is in hl_dsp_proto.h, which both include.) On the DSP the
 * pairs are worked out by the main loop into a small ring (hlDspFill) and
 * only handed to the speakers by the interrupt (hlDspPull): a pair that
 * takes long to work out is then a moment's delay, not a missed sample. It
 * is plain C in the DSP's own terms: 16-bit words, 32-bit sums.
 *
 * It is the room, fed with what the DS's own sixteen channels are
 * playing, which the ARM7's sound program sends and the ARM9 passes on: one
 * channel, a quarter of the output rate, in a ring. Out of it come,
 * in stereo: the first reflections (one line, read at four places back,
 * two to each side); the room's long reverb (four lines feeding each other
 * through a Hadamard square, each dulled on the way round, the left side
 * from two of them, the right from the other two); and the game's echo
 * (one line, up to 0.4 s, coming back and back). How each room has them is
 * the ARM9's to work out (HlDspRoom.hpp, from the game's presets) and write
 * in (HL_DSP_INFO_ROOM). Only the room is put out: the sound itself is
 * already coming out of the ordinary channels.
 */
#ifndef HL_DSP_H
#define HL_DSP_H

#include <stdint.h>

#include "hl_dsp_proto.h"

static int16_t hlSendRing[HL_DSP_SEND_WORDS];
static int16_t hlOutRing[HL_DSP_OUT_WORDS];
static volatile uint16_t hlOutRead, hlOutWrite;
static int16_t hlPortL, hlPortR;
static uint16_t hlStarved;

static uint16_t hlSendRead, hlSendWrite;
static uint16_t hlTick, hlHeard;
static int16_t hlOutL, hlOutR;

/* The room: its numbers as the ARM9 writes them, and as they are taken. */
static uint16_t hlRoomBlock[HL_DSP_ROOM_WORDS];
static int16_t hlRoom[HL_DSP_ROOM_WORDS];
static int16_t hlLevel;
/* Its lines (emptied when a room is taken), and where each is. */
static int16_t hlEr[HL_DSP_ER_WORDS];
static int16_t hlFdn[4 * HL_DSP_FDN_LINE];
static int16_t hlDly[HL_DSP_DLY_WORDS];
static uint16_t hlErPos, hlFdnPos[4], hlDlyPos;
static int16_t hlFdnLow[4], hlDlyLast1, hlDlyLast2;
/* What it puts out, drawn in straight lines over the four ticks. */
static int16_t hlWetL, hlWetStepL, hlWetR, hlWetStepR;

static void hlRoomClear(void)
{
    uint16_t i;
    for (i = 0; i < HL_DSP_ER_WORDS; i++)
        hlEr[i] = 0;
    for (i = 0; i < 4 * HL_DSP_FDN_LINE; i++)
        hlFdn[i] = 0;
    for (i = 0; i < HL_DSP_DLY_WORDS; i++)
        hlDly[i] = 0;
    hlErPos = hlDlyPos = 0;
    for (i = 0; i < 4; i++)
    {
        hlFdnPos[i] = 0;
        hlFdnLow[i] = 0;
    }
    hlDlyLast1 = hlDlyLast2 = 0;
}

__attribute__((unused)) static void hlDspInit(void)
{
    uint16_t i;
    for (i = 0; i < HL_DSP_ROOM_WORDS; i++)
        hlRoom[i] = 0;
    hlRoomClear();
    hlOutRead = 0;
    hlOutWrite = 0;
    hlPortL = hlPortR = 0;
    hlStarved = 0;
    hlSendRead = hlSendWrite = 0;
    hlTick = hlHeard = 0;
    hlOutL = hlOutR = 0;
    hlLevel = 0;
    hlWetL = hlWetStepL = hlWetR = hlWetStepR = 0;
}

#define HL16(wide) ((int16_t)(wide))

static int16_t hlSaturate(int32_t value)
{
    if (value > 32767)
        value = 32767;
    if (value < -32767)
        value = -32767;
    return HL16(value);
}

/* `value` over two to the `bits`, towards nothing: a negative number is
 * shifted as its size is, so that the echo dies away to silence and not to
 * minus one. (`value` is worked out more than once: always a plain variable
 * here.) */
#define hlShift(value, bits) ((value) < 0 ? -((-(value)) >> (bits)) : ((value) >> (bits)))

#define hlMul(a, b) ((int32_t)(a) * (b))

/* One sample of the room, from one sample of what is to echo: hlEchoIn in,
 * hlEchoOutL and hlEchoOutR out. */
static int16_t hlEchoIn, hlEchoOutL, hlEchoOutR;
static int16_t hlRvD[4], hlRvM[4], hlRvTap[4];
static int32_t hlRvWide, hlRvA, hlRvB, hlRvC, hlRvE, hlRvL, hlRvR, hlRvEcho;
static uint16_t hlRvI, hlRvAt;

/* `value` * `q` over 2^`bits`, towards nothing: the multiply the DSP does. */
#define hlScale(value, q, bits) hlShift(hlMul((value), (q)), (bits))

static void hlReverb(void)
{
    /* The first reflections: the line written, then read at four places
     * back -- left, right, left, right. */
    hlEr[hlErPos] = hlEchoIn;
    for (hlRvI = 0; hlRvI < 4; hlRvI++)
    {
        hlRvWide = (int32_t)hlErPos - hlRoom[HL_DSP_ROOM_ER_TAP + hlRvI];
        if (hlRvWide < 0)
            hlRvWide += hlRoom[HL_DSP_ROOM_ER_LEN];
        hlRvTap[hlRvI] = hlEr[(uint16_t)hlRvWide];
    }
    hlErPos++;
    if (hlErPos >= (uint16_t)hlRoom[HL_DSP_ROOM_ER_LEN])
        hlErPos = 0;
    hlRvWide = (int32_t)hlRvTap[0] + hlRvTap[2];
    hlRvWide = hlShift(hlRvWide, 1);
    hlRvL = hlScale(hlSaturate(hlRvWide), hlRoom[HL_DSP_ROOM_ER_MIX], 15);
    hlRvWide = (int32_t)hlRvTap[1] + hlRvTap[3];
    hlRvWide = hlShift(hlRvWide, 1);
    hlRvR = hlScale(hlSaturate(hlRvWide), hlRoom[HL_DSP_ROOM_ER_MIX], 15);

    /* The reverb: what comes round each line, dulled ((16384 - damp) of it
     * and damp of what it was)... */
    for (hlRvI = 0; hlRvI < 4; hlRvI++)
    {
        hlRvAt = (uint16_t)(hlRvI * HL_DSP_FDN_LINE + hlFdnPos[hlRvI]);
        hlRvD[hlRvI] = hlFdn[hlRvAt];
        hlRvWide = hlMul(hlRvD[hlRvI], HL16(16384 - (int32_t)hlRoom[HL_DSP_ROOM_FDN_DAMP]));
        hlRvWide += hlMul(hlFdnLow[hlRvI], hlRoom[HL_DSP_ROOM_FDN_DAMP]);
        hlRvWide = hlShift(hlRvWide, 14);
        hlFdnLow[hlRvI] = HL16(hlRvWide);
    }
    /* ... mixed through a Hadamard square (sums and differences, halved:
     * nothing gained, nothing lost)... */
    hlRvA = (int32_t)hlFdnLow[0] + hlFdnLow[1];
    hlRvB = (int32_t)hlFdnLow[0] - hlFdnLow[1];
    hlRvC = (int32_t)hlFdnLow[2] + hlFdnLow[3];
    hlRvE = (int32_t)hlFdnLow[2] - hlFdnLow[3];
    hlRvWide = hlRvA + hlRvC;
    hlRvM[0] = hlSaturate(hlShift(hlRvWide, 1));
    hlRvWide = hlRvB + hlRvE;
    hlRvM[1] = hlSaturate(hlShift(hlRvWide, 1));
    hlRvWide = hlRvA - hlRvC;
    hlRvM[2] = hlSaturate(hlShift(hlRvWide, 1));
    hlRvWide = hlRvB - hlRvE;
    hlRvM[3] = hlSaturate(hlShift(hlRvWide, 1));
    /* ... and back in, quieter, with what is new (every other line the
     * other way up: the two sides come out unlike). */
    for (hlRvI = 0; hlRvI < 4; hlRvI++)
    {
        hlRvAt = (uint16_t)(hlRvI * HL_DSP_FDN_LINE + hlFdnPos[hlRvI]);
        hlRvWide = hlScale(hlRvM[hlRvI], hlRoom[HL_DSP_ROOM_FDN_GAIN], 15);
        if (hlRvI & 1)
            hlRvWide = hlRvWide - hlEchoIn;
        else
            hlRvWide = hlRvWide + hlEchoIn;
        hlFdn[hlRvAt] = hlSaturate(hlRvWide);
        hlFdnPos[hlRvI]++;
        if (hlFdnPos[hlRvI] >= (uint16_t)hlRoom[HL_DSP_ROOM_FDN_LEN + hlRvI])
            hlFdnPos[hlRvI] = 0;
    }
    hlRvWide = (int32_t)hlFdnLow[0] + hlFdnLow[2];
    hlRvWide = hlShift(hlRvWide, 1);
    hlRvL += hlScale(hlSaturate(hlRvWide), hlRoom[HL_DSP_ROOM_LATE_MIX], 15);
    hlRvWide = (int32_t)hlFdnLow[1] + hlFdnLow[3];
    hlRvWide = hlShift(hlRvWide, 1);
    hlRvR += hlScale(hlSaturate(hlRvWide), hlRoom[HL_DSP_ROOM_LATE_MIX], 15);

    /* The game's echo: one line, coming back quieter (and dulled, two parts
     * of the new to one each of the last two, if the room says so). */
    hlRvEcho = 0;
    if (hlRoom[HL_DSP_ROOM_DLY_LEN] != 0)
    {
        hlRvD[0] = hlDly[hlDlyPos];
        if (hlRoom[HL_DSP_ROOM_DLY_LOW] != 0)
        {
            hlRvWide = (int32_t)hlRvD[0] + hlRvD[0] + hlDlyLast1 + hlDlyLast2;
            hlDlyLast2 = hlDlyLast1;
            hlDlyLast1 = hlRvD[0];
            hlRvD[0] = HL16(hlShift(hlRvWide, 2));
        }
        hlRvWide = hlScale(hlRvD[0], hlRoom[HL_DSP_ROOM_DLY_GAIN], 15);
        hlRvWide = hlRvWide + hlEchoIn;
        hlDly[hlDlyPos] = hlSaturate(hlRvWide);
        hlDlyPos++;
        if (hlDlyPos >= (uint16_t)hlRoom[HL_DSP_ROOM_DLY_LEN])
            hlDlyPos = 0;
        hlRvEcho = hlScale(hlRvD[0], hlRoom[HL_DSP_ROOM_ECHO_MIX], 15);
    }
    hlEchoOutL = hlSaturate(hlRvL + hlRvEcho);
    hlEchoOutR = hlSaturate(hlRvR + hlRvEcho);
}

/* One pair of samples out: hlOutL and hlOutR. */
static void hlDspTick(void)
{
    int32_t wide;

    if ((hlTick & 3) == 0)
    {
        /* A new sample of the room every fourth tick, and a straight line
         * to it over the four, on each side. */
        int16_t wetL = 0, wetR = 0;
        hlEchoIn = 0;
        if (hlSendRead != hlSendWrite)
        {
            wide = hlSendRing[hlSendRead];
            if (wide > (int32_t)hlHeard)
                hlHeard = (uint16_t)wide;
            wide = hlShift(wide, 2);
            hlEchoIn = HL16(wide);
            hlSendRead++;
            if (hlSendRead >= HL_DSP_SEND_WORDS)
                hlSendRead = 0;
        }
        if (hlLevel != 0)
        {
            hlReverb();
            wide = hlMul(hlEchoOutL, hlLevel);
            wide = hlShift(wide, 6);
            wetL = hlSaturate(wide);
            wide = hlMul(hlEchoOutR, hlLevel);
            wide = hlShift(wide, 6);
            wetR = hlSaturate(wide);
        }
        wide = (int32_t)wetL - hlWetL;
        wide = hlShift(wide, 2);
        hlWetStepL = HL16(wide);
        wide = (int32_t)wetR - hlWetR;
        wide = hlShift(wide, 2);
        hlWetStepR = HL16(wide);
    }
    hlWetL += hlWetStepL;
    hlWetR += hlWetStepR;
    hlTick++;

    hlOutL = hlSaturate((int32_t)hlWetL);
    hlOutR = hlSaturate((int32_t)hlWetR);
}

/* The main loop's part: work out pairs until the ring between it and the
 * interrupt is full. */
__attribute__((unused)) static void hlDspFill(void)
{
    while (1)
    {
        uint16_t next = hlOutWrite + 2;
        if (next >= HL_DSP_OUT_WORDS)
            next = 0;
        if (next == hlOutRead)
            return;
        hlDspTick();
        hlOutRing[hlOutWrite] = hlOutL;
        hlOutRing[hlOutWrite + 1] = hlOutR;
        hlOutWrite = next;
    }
}

/* The interrupt's part: the next pair for the speakers, in hlPortL and
 * hlPortR. If the main loop has not kept up there is none; then the last
 * pair is sent again, a quarter quieter each time, so that falling behind is
 * a dip and not a bang. */
__attribute__((unused)) static void hlDspPull(void)
{
    int32_t wide;
    if (hlOutRead != hlOutWrite)
    {
        uint16_t next = hlOutRead + 2;
        hlPortL = hlOutRing[hlOutRead];
        hlPortR = hlOutRing[hlOutRead + 1];
        if (next >= HL_DSP_OUT_WORDS)
            next = 0;
        hlOutRead = next;
        return;
    }
    if (hlStarved < 0x7FFF)
        hlStarved++;
    wide = hlPortL;
    wide = hlShift(wide, 2);
    hlPortL = HL16((int32_t)hlPortL - wide);
    wide = hlPortR;
    wide = hlShift(wide, 2);
    hlPortR = HL16((int32_t)hlPortR - wide);
}

/* What a command does, and its answer. */
__attribute__((unused)) static uint16_t hlDspCommand(uint16_t command, uint16_t arg1, uint16_t arg2)
{
    uint16_t answer = 0;
    if (command == HL_DSP_CMD_PING)
    {
        answer = HL_DSP_MAGIC;
    }
    else if (command == HL_DSP_CMD_SEND_FED)
    {
        if (arg1 < HL_DSP_SEND_WORDS)
            hlSendWrite = arg1;
        answer = hlSendRead;
    }
    else if (command == HL_DSP_CMD_ROOM)
    {
        /* The room's numbers taken -- the lines' lengths each held within
         * its line -- and the lines emptied. */
        uint16_t i;
        for (i = 0; i < HL_DSP_ROOM_WORDS; i++)
            hlRoom[i] = (int16_t)hlRoomBlock[i];
        if (hlRoom[HL_DSP_ROOM_ER_LEN] < 1 || hlRoom[HL_DSP_ROOM_ER_LEN] > HL_DSP_ER_WORDS)
            hlRoom[HL_DSP_ROOM_ER_LEN] = 1;
        for (i = 0; i < 4; i++)
        {
            if (hlRoom[HL_DSP_ROOM_ER_TAP + i] < 0 || hlRoom[HL_DSP_ROOM_ER_TAP + i] >= hlRoom[HL_DSP_ROOM_ER_LEN])
                hlRoom[HL_DSP_ROOM_ER_TAP + i] = 0;
            if (hlRoom[HL_DSP_ROOM_FDN_LEN + i] < 1 || hlRoom[HL_DSP_ROOM_FDN_LEN + i] > HL_DSP_FDN_LINE)
                hlRoom[HL_DSP_ROOM_FDN_LEN + i] = 1;
        }
        if (hlRoom[HL_DSP_ROOM_DLY_LEN] < 0 || hlRoom[HL_DSP_ROOM_DLY_LEN] > HL_DSP_DLY_WORDS)
            hlRoom[HL_DSP_ROOM_DLY_LEN] = 0;
        hlLevel = hlRoom[HL_DSP_ROOM_LEVEL];
        hlRoomClear();
    }
    else if (command == HL_DSP_CMD_STATUS)
    {
        answer = hlStarved;
        hlStarved = 0;
    }
    else if (command == HL_DSP_CMD_HEARD)
    {
        answer = hlHeard;
        hlHeard = 0;
    }
    return answer;
}

#endif
