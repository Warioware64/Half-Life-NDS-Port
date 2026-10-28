// Shared between the two processors of the routing probe: what the ARM9
// asks for, and what the ARM7 measured. It lives in main RAM; the ARM9
// reads and writes it through the uncached mirror, the ARM7 has no cache.
//
// The probe answers the questions the custom ARM7 audio core depends on
// (docs: the audio plan, phase P0):
//  1. With SOUNDCNT bit 12 set (Ch1 out of the mixer), does the capture of
//     the left mixer leave Ch1 out? (Then the room effect, played on Ch1 and
//     Ch3, never hears itself.)
//  2. Can capture write into ARM7 WRAM, or only into main RAM?
//  3. How many samples late is capture, from the moment Ch1's timer starts?
//  4. Does a channel's pan still apply when the speakers take Ch1/Ch3
//     directly (SOUNDCNT bits 8-11)? -- by ear.
//  5. Does the whole pipeline (dry mix captured, played back on Ch1/Ch3)
//     sound clean? -- by ear, against the plain mixer.
//  6. Does a timer interrupt every 64 samples stay on time, and does the
//     captured picture stay still against the channels, for minutes?

#pragma once

#include <stdint.h>

#define PROBE_FIFO FIFO_USER_01

enum ProbeCommand
{
    PROBE_IDLE = 0,
    PROBE_AUTO,        // tests 1-3, then idle
    PROBE_PAN_LEFT,    // test 4: a tone on Ch1, speakers from Ch1/Ch3, pan 0
    PROBE_PAN_MIDDLE,  // ... pan 64
    PROBE_PAN_RIGHT,   // ... pan 127
    PROBE_PIPELINE,    // test 5: a tone on Ch0, through capture and Ch1/Ch3
    PROBE_DIRECT,      // ... the same tone through the plain mixer
    PROBE_LOCK,        // test 6: the pipeline with a click, timed, until stopped
    PROBE_SILENCE,
};

#define PROBE_RING 2048 // samples in each capture ring (and the click's period)

typedef struct ProbeShared
{
    volatile uint32_t command; // ProbeCommand, from the ARM9
    volatile uint32_t running; // the command the ARM7 is carrying out
    volatile uint32_t autoDone; // counts finished PROBE_AUTO runs

    // 1, 2: the average of a captured stretch, while Ch1 plays +0x2000
    volatile int32_t meanMixed; // Ch1 into the mixer: about +0x2000 (times volume)
    volatile int32_t meanMuted; // Ch1 out of it: about 0 if the routing works
    volatile int32_t meanWram;  // as meanMixed, captured into ARM7 WRAM
    volatile int32_t wramWritten; // words of the WRAM buffer that changed

    // 3: samples expected (from the timer) and captured, at four moments
    volatile int32_t expected[4];
    volatile int32_t captured[4];

    // 6: the block timer and the click
    volatile uint32_t blocks;     // interrupts so far
    volatile uint32_t late;       // ... more than half a block late
    volatile uint32_t gapMin, gapMax; // between interrupts, in 33.5 MHz ticks
    volatile int32_t clickAt;     // where the click is in the ring (-1: not found)
    volatile uint32_t clickMoves; // times it was found somewhere else
    volatile uint32_t seconds;

    // Buffers the channels read and capture writes (word aligned).
    int16_t dc[64] __attribute__((aligned(32)));
    int16_t tone[64] __attribute__((aligned(32)));
    int16_t click[PROBE_RING] __attribute__((aligned(32)));
    int16_t cap0[PROBE_RING] __attribute__((aligned(32)));
    int16_t cap1[PROBE_RING] __attribute__((aligned(32)));
    int16_t oneShot[1024] __attribute__((aligned(32)));
} ProbeShared;
