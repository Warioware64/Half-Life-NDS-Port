// The routing probe's ARM7: drives the sound channels, the capture units and
// SOUNDCNT directly, and writes what it measures into the shared block
// (common/probe.h says what each test asks).

#include <nds.h>

#include "probe.h"

static ProbeShared *shared;

// 32728 Hz: the hardware's own output rate (16.757 MHz / 512). One sample is
// 1024 ticks of a timer counting the 33.5 MHz bus clock.
#define RATE_TIMER ((uint16_t)-512)
#define TICKS_PER_SAMPLE 1024
#define BLOCK_TICKS 65536 // a timer overflow: 64 samples

// SOUNDxCNT: volume, pan, loop or one-shot, PCM16, start.
static void play(int n, const void *src, int words, int volume, int pan, bool loop)
{
    SCHANNEL_CR(n) = 0;
    SCHANNEL_SOURCE(n) = (uint32_t)src;
    SCHANNEL_TIMER(n) = RATE_TIMER;
    SCHANNEL_REPEAT_POINT(n) = 0;
    SCHANNEL_LENGTH(n) = words;
    SCHANNEL_CR(n) = (uint32_t)volume | (uint32_t)pan << 16 | (loop ? 1u : 2u) << 27 | 1u << 29 | 1u << 31;
}

static void capture(int unit, void *dst, int words, bool oneShot)
{
    if (unit == 0)
    {
        REG_SNDCAP0CNT = 0;
        REG_SNDCAP0DAD = (uint32_t)dst;
        REG_SNDCAP0LEN = (uint16_t)words;
        REG_SNDCAP0CNT = (oneShot ? SNDCAPCNT_ONESHOT : SNDCAPCNT_REPEAT) | SNDCAPCNT_FORMAT_16BIT |
                         SNDCAPCNT_START_BUSY;
    }
    else
    {
        REG_SNDCAP1CNT = 0;
        REG_SNDCAP1DAD = (uint32_t)dst;
        REG_SNDCAP1LEN = (uint16_t)words;
        REG_SNDCAP1CNT = (oneShot ? SNDCAPCNT_ONESHOT : SNDCAPCNT_REPEAT) | SNDCAPCNT_FORMAT_16BIT |
                         SNDCAPCNT_START_BUSY;
    }
}

// Left from (0 mixer, 1 Ch1, 2 Ch3), right likewise; Ch1/Ch3 kept out of the mixer.
static void route(int left, int right, bool muteCh1, bool muteCh3)
{
    REG_SOUNDCNT = 127 | left << 8 | right << 10 | (muteCh1 ? 1 << 12 : 0) | (muteCh3 ? 1 << 13 : 0) | 1 << 15;
}

static void silence(void)
{
    REG_SNDCAP0CNT = 0;
    REG_SNDCAP1CNT = 0;
    for (int n = 0; n < 16; ++n)
        SCHANNEL_CR(n) = 0;
    TIMER_CR(0) = 0;
    route(0, 0, false, false);
}

// A 32-bit clock of bus ticks: timer 1 counting, timer 2 counting its overflows.
static uint32_t stamp(void)
{
    uint16_t high = TIMER_DATA(2);
    uint16_t low = TIMER_DATA(1);
    const uint16_t again = TIMER_DATA(2);
    if (again != high)
    {
        high = again;
        low = TIMER_DATA(1);
    }
    return (uint32_t)high << 16 | low;
}

static void waitFrames(int n)
{
    while (n-- > 0)
        swiWaitForVBlank();
}

static int32_t mean(const int16_t *samples, int count)
{
    int32_t sum = 0;
    for (int i = 0; i < count; ++i)
        sum += samples[i];
    return sum / count;
}

static int16_t wram[1024] __attribute__((aligned(4))); // in ARM7 WRAM, as all of this binary's data

// Tests 1-3.
static void runAuto(void)
{
    // 1. Ch1 playing +0x2000, captured from the left mixer, with Ch1 in the
    // mixer and then out of it.
    route(0, 0, false, false);
    capture(0, shared->cap0, PROBE_RING / 2, false);
    play(1, shared->dc, 32, 127, 64, true);
    waitFrames(6);
    shared->meanMixed = mean(shared->cap0, PROBE_RING);
    silence();
    route(0, 0, true, false);
    capture(0, shared->cap0, PROBE_RING / 2, false);
    play(1, shared->dc, 32, 127, 64, true);
    waitFrames(6);
    shared->meanMuted = mean(shared->cap0, PROBE_RING);
    silence();

    // 2. The same captured into this processor's own WRAM.
    for (int i = 0; i < 1024; ++i)
        wram[i] = 0x5A5A;
    route(0, 0, false, false);
    capture(0, wram, 512, false);
    play(1, shared->dc, 32, 127, 64, true);
    waitFrames(6);
    silence();
    int changed = 0;
    for (int i = 0; i < 1024; i += 2)
        changed += (wram[i] != 0x5A5A || wram[i + 1] != 0x5A5A) ? 1 : 0;
    shared->wramWritten = changed;
    shared->meanWram = mean(wram, 1024);

    // 3. One-shot capture of silence over a known pattern, started with
    // Ch1's timer: how far it has got, against how far it should have.
    for (int i = 0; i < 1024; ++i)
        shared->oneShot[i] = 0x1234;
    route(0, 0, true, false);
    TIMER_CR(1) = 0;
    TIMER_CR(2) = 0;
    TIMER_DATA(1) = 0;
    TIMER_DATA(2) = 0;
    TIMER_CR(2) = TIMER_ENABLE | TIMER_CASCADE;
    TIMER_CR(1) = TIMER_ENABLE | TIMER_DIV_1;
    capture(0, shared->oneShot, 512, true);
    play(1, shared->dc, 32, 127, 64, true);
    const uint32_t start = stamp();
    static const int32_t kAt[4] = {100, 300, 500, 800};
    for (int k = 0; k < 4; ++k)
    {
        while ((int32_t)((stamp() - start) / TICKS_PER_SAMPLE) < kAt[k])
            ;
        int got = 0;
        while (got < 1024 && shared->oneShot[got] != 0x1234)
            ++got;
        shared->expected[k] = kAt[k];
        shared->captured[k] = got;
    }
    silence();
    shared->autoDone = shared->autoDone + 1;
}

// Test 6: an interrupt every 64 samples, its gaps timed.
static uint32_t lastStamp;
static void blockTick(void)
{
    const uint32_t now = stamp();
    if (shared->blocks != 0)
    {
        const uint32_t gap = now - lastStamp;
        if (gap < shared->gapMin)
            shared->gapMin = gap;
        if (gap > shared->gapMax)
            shared->gapMax = gap;
        if (gap > BLOCK_TICKS + BLOCK_TICKS / 2)
            shared->late = shared->late + 1;
    }
    lastStamp = now;
    shared->blocks = shared->blocks + 1;
}

static void startPipeline(const int16_t *dry, int dryWords)
{
    // The dry mix (Ch0) captured left and right, played back on Ch1/Ch3,
    // which the speakers take directly and the mixer does not hear.
    route(1, 2, true, true);
    capture(0, shared->cap0, PROBE_RING / 2, false);
    capture(1, shared->cap1, PROBE_RING / 2, false);
    play(1, shared->cap0, PROBE_RING / 2, 127, 0, true);
    play(3, shared->cap1, PROBE_RING / 2, 127, 127, true);
    play(0, dry, dryWords, 127, 64, true);
}

static void start(uint32_t command)
{
    silence();
    switch (command)
    {
    case PROBE_AUTO:
        runAuto();
        break;
    case PROBE_PAN_LEFT:
    case PROBE_PAN_MIDDLE:
    case PROBE_PAN_RIGHT:
        route(1, 2, true, true);
        play(1, shared->tone, 32, 127, command == PROBE_PAN_LEFT ? 0 : (command == PROBE_PAN_MIDDLE ? 64 : 127),
             true);
        break;
    case PROBE_PIPELINE:
        startPipeline(shared->tone, 32);
        break;
    case PROBE_DIRECT:
        route(0, 0, false, false);
        play(0, shared->tone, 32, 127, 64, true);
        break;
    case PROBE_LOCK:
        shared->blocks = 0;
        shared->late = 0;
        shared->gapMin = 0xFFFFFFFF;
        shared->gapMax = 0;
        shared->clickAt = -1;
        shared->clickMoves = 0;
        shared->seconds = 0;
        TIMER_CR(1) = 0;
        TIMER_CR(2) = 0;
        TIMER_CR(2) = TIMER_ENABLE | TIMER_CASCADE;
        TIMER_CR(1) = TIMER_ENABLE | TIMER_DIV_1;
        startPipeline(shared->click, PROBE_RING / 2);
        TIMER_DATA(0) = 0;
        TIMER_CR(0) = TIMER_ENABLE | TIMER_DIV_1 | TIMER_IRQ_REQ;
        break;
    default:
        break;
    }
}

// Where the click is in the left ring now.
static int findClick(void)
{
    int best = -1, most = 0x1000;
    for (int i = 0; i < PROBE_RING; ++i)
    {
        int v = shared->cap0[i];
        v = v < 0 ? -v : v;
        if (v > most)
        {
            most = v;
            best = i;
        }
    }
    return best;
}

static void vblankHandler(void)
{
    inputGetAndSend();
}

int main(void)
{
    enableSound();
    readUserSettings();
    ledBlink(LED_ALWAYS_ON);
    touchInit();
    irqInit();
    fifoInit();
    installSystemFIFO();
    irqSet(IRQ_VBLANK, vblankHandler);
    irqEnable(IRQ_VBLANK);
    irqSet(IRQ_TIMER0, blockTick);
    irqEnable(IRQ_TIMER0);

    while (!fifoCheckValue32(PROBE_FIFO))
        swiWaitForVBlank();
    shared = (ProbeShared *)fifoGetValue32(PROBE_FIFO);
    silence();

    uint32_t frames = 0;
    for (;;)
    {
        swiWaitForVBlank();
        const uint32_t command = shared->command;
        if (command != shared->running)
        {
            shared->running = command;
            start(command);
            if (command == PROBE_AUTO)
            {
                shared->command = PROBE_IDLE;
                shared->running = PROBE_IDLE;
            }
        }
        if (shared->running == PROBE_LOCK && ++frames % 30 == 0)
        {
            const int at = findClick();
            if (at >= 0)
            {
                if (shared->clickAt >= 0)
                {
                    int moved = at - shared->clickAt;
                    moved = moved < 0 ? -moved : moved;
                    if (moved > 2 && moved < PROBE_RING - 2)
                        shared->clickMoves = shared->clickMoves + 1;
                }
                shared->clickAt = at;
            }
            if (frames % 60 == 0)
                shared->seconds = shared->seconds + 1;
        }
    }
    return 0;
}
