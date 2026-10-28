// The routing probe's ARM9: the buttons pick a test, the screen shows what
// the ARM7 measured (common/probe.h).

#include <stdio.h>

#include <nds.h>

#include "probe.h"

static ProbeShared block __attribute__((aligned(32)));

static const char *verdict(bool ok)
{
    return ok ? "YES" : "no";
}

int main(void)
{
    consoleDemoInit();
    ProbeShared *p = (ProbeShared *)memUncached(&block);

    // What the channels play: a level, a sine tone (511 Hz), a click.
    for (int i = 0; i < 64; ++i)
    {
        block.dc[i] = 0x2000;
        block.tone[i] = (int16_t)(sinLerp((int16_t)(i * (32768 / 64))) * 6);
    }
    for (int i = 0; i < PROBE_RING; ++i)
        block.click[i] = i < 4 ? 0x7000 : 0;
    block.command = PROBE_IDLE;
    block.running = PROBE_IDLE;
    block.autoDone = 0;
    DC_FlushRange(&block, sizeof(block));
    fifoSendValue32(PROBE_FIFO, (uint32_t)&block);
    // (The automatic tests once at the start: nothing to press for those.)
    for (int i = 0; i < 30; ++i)
        swiWaitForVBlank();
    p->command = PROBE_AUTO;

    static const char *const kNames[] = {"idle",          "auto tests",    "pan 0 (Ch1 direct)",
                                         "pan 64 (Ch1 direct)", "pan 127 (Ch1 direct)", "PIPELINE tone",
                                         "DIRECT tone",   "LOCK (click)",  "silence"};
    for (;;)
    {
        swiWaitForVBlank();
        scanKeys();
        const uint32_t down = keysDown();
        if (down & KEY_A)
            p->command = PROBE_AUTO;
        if (down & KEY_L)
            p->command = PROBE_PAN_LEFT;
        if (down & KEY_UP)
            p->command = PROBE_PAN_MIDDLE;
        if (down & KEY_R)
            p->command = PROBE_PAN_RIGHT;
        if (down & KEY_B)
            p->command = PROBE_PIPELINE;
        if (down & KEY_Y)
            p->command = PROBE_DIRECT;
        if (down & KEY_X)
            p->command = PROBE_LOCK;
        if (down & KEY_START)
            p->command = PROBE_SILENCE;

        consoleClear();
        printf("HL audio routing probe  (%s)\n", isDSiMode() ? "DSi" : "DS");
        printf("A auto  L/Up/R pan  B pipe  Y direct\nX lock  START silence\n");
        const uint32_t running = p->running < 9 ? p->running : 0;
        printf("now: %s\n\n", kNames[running]);

        if (p->autoDone != 0)
        {
            printf("1 Ch1 in mixer:  mean %ld\n", (long)p->meanMixed);
            printf("  Ch1 muted:     mean %ld\n", (long)p->meanMuted);
            const long mixed = p->meanMixed, muted = p->meanMuted;
            printf("  dry-only capture: %s\n",
                   verdict(mixed > 500 && (muted < 0 ? -muted : muted) < mixed / 20));
            printf("2 WRAM written %ld/512 mean %ld\n", (long)p->wramWritten, (long)p->meanWram);
            printf("  capture to WRAM: %s\n", verdict(p->wramWritten > 400 && p->meanWram > 500));
            printf("3 samples expected/captured:\n ");
            for (int k = 0; k < 4; ++k)
                printf(" %ld/%ld", (long)p->expected[k], (long)p->captured[k]);
            printf("\n  lag: %ld samples\n\n", (long)(p->expected[3] - p->captured[3]));
        }
        else
        {
            printf("press A for the automatic tests\n\n");
        }

        if (running == PROBE_LOCK)
        {
            printf("6 %lu s  blocks %lu  late %lu\n", (unsigned long)p->seconds, (unsigned long)p->blocks,
                   (unsigned long)p->late);
            printf("  gap min %lu max %lu (65536)\n", (unsigned long)p->gapMin, (unsigned long)p->gapMax);
            printf("  click at %ld  moved %lu\n", (long)p->clickAt, (unsigned long)p->clickMoves);
        }
        else if (running >= PROBE_PAN_LEFT && running <= PROBE_PAN_RIGHT)
        {
            printf("4 LISTEN: speakers take Ch1 (left)\n  and Ch3 (right, silent).\n");
            printf("  Does the left side change\n  between pan 0, 64 and 127?\n");
        }
        else if (running == PROBE_PIPELINE || running == PROBE_DIRECT)
        {
            printf("5 LISTEN: switch B and Y.\n  Same tone, as loud, clean,\n  no echo or howl?\n");
        }
    }
    return 0;
}
