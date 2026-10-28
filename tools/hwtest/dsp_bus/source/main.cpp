// The DSP bus probe (DSi only; the audio plan, phase P0).
//
// On a DSi a DMA feeding the geometry FIFO while the DSP's DMA moves data can
// wedge the DSP's bus for good (NEA's measurement: nitro-engine
// NEADisplayList.c). NEA measured it with jobs the DSP pulls through AHBM.
// The game's own way of feeding the DSP is different: the ARM9 writes words
// into the PDATA FIFO, and the DSP's DMA moves them into its own memory. This
// probe measures THAT way, with the game's own DSP program:
//
//   1 PDATA      words to the DSP and back, no 3D
//   2 DMA        a legacy DMA feeds the geometry FIFO, no words (DSP running)
//   3 NDMA       the same with an NDMA (as the game's world feeds it)
//   4 seq DMA    the legacy DMA, all done, then the words
//   5 seq NDMA   the same with the NDMA
//   6 ovl NDMA   the words while the NDMA runs
//   7 ovl DMA    ... while the legacy DMA runs (hardware: it jams)
//
// A frame fails if a transfer stops moving, what is read back is not what was
// written, or the DSP does not answer a ping after it; it is then restarted.
// "fail" is the answer: 0 in 4 and 5 means sequencing is enough (2 and 3 say
// whether a display list alone, beside a running DSP, is any trouble).

#include <cstdio>
#include <cstring>

#include <nds.h>

extern "C"
{
#include "dsp/hl_dsp_proto.h"
}

// The game's DSP program, as the game embeds it.
#include "../../../../source/audio/DspProgram.gen.cpp"

namespace
{
    constexpr int kFramesPerCondition = 60;
    constexpr int kConditions = 7;
    constexpr int kChunk = 512;  // words per PDATA transfer
    constexpr int kChunks = 4;   // transfers per frame
    constexpr int kSpin = 400000; // a transfer stopped moving, if this runs out

    // A long display list that keeps the geometry engine busy for some
    // milliseconds and makes no polygons: identity 4x4 matrix multiplies
    // (MTX_MULT_4x4, 35 cycles each), four to a packed command word. (The
    // first version drew tiny triangles, more than polygon memory holds,
    // and on hardware that choked the geometry engine on its own.)
    constexpr int kListWords = 65 * 1008;
    alignas(32) std::uint32_t displayList[kListWords];
    int listWords = 0;

    void buildList()
    {
        int n = 0;
        while (n + 65 <= kListWords)
        {
            displayList[n++] = 0x18181818;
            for (int m = 0; m < 4; ++m)
            {
                for (int i = 0; i < 16; ++i)
                    displayList[n++] = (i % 5 == 0) ? (1u << 12) : 0u; // 1.0 on the diagonal
            }
        }
        listWords = n;
        DC_FlushRange(displayList, sizeof(displayList));
    }

    enum class Gx
    {
        None,
        Dma,
        Ndma
    };

    void startGx(Gx how)
    {
        if (how == Gx::Dma)
        {
            DMA_SRC(1) = reinterpret_cast<std::uint32_t>(displayList);
            DMA_DEST(1) = reinterpret_cast<std::uint32_t>(&GFX_FIFO);
            DMA_CR(1) = DMA_FIFO | static_cast<std::uint32_t>(listWords);
        }
        else if (how == Gx::Ndma)
        {
            REG_NDMA_SRC(0) = reinterpret_cast<std::uint32_t>(displayList);
            REG_NDMA_DEST(0) = reinterpret_cast<std::uint32_t>(&GFX_FIFO);
            REG_NDMA_LENGTH(0) = static_cast<std::uint32_t>(listWords);
            REG_NDMA_BLENGTH(0) = 112;
            REG_NDMA_BDELAY(0) = 0;
            REG_NDMA_CR(0) = NDMA_ENABLE | NDMA_START_FIFO | NDMA_SRC_INC | NDMA_DST_FIX;
        }
    }

    std::uint32_t stallStatus = 0; // GXSTAT when a display list was last given up on

    void setUp3D()
    {
        glInit();
        glViewport(0, 0, 255, 191);
        glClearColor(0, 0, 8, 31);
        glClearPolyID(63);
        glClearDepth(0x7FFF);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluPerspective(70, 256.0 / 192.0, 0.1, 40);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }

    /// The display list's DMA finished within 20 ms (false: it never did; it
    /// is stopped, and the geometry engine started again, so that half a
    /// list does not spoil the next frame's).
    bool waitGx(Gx how)
    {
        if (how == Gx::None)
            return true;
        cpuStartTiming(2);
        while (cpuGetTiming() < BUS_CLOCK / 50)
        {
            if (how == Gx::Dma && !(DMA_CR(1) & DMA_BUSY))
                return true;
            if (how == Gx::Ndma && !(REG_NDMA_CR(0) & NDMA_BUSY))
                return true;
        }
        stallStatus = GFX_STATUS;
        if (how == Gx::Dma)
            DMA_CR(1) = 0;
        else
            REG_NDMA_CR(0) = 0;
        setUp3D();
        return false;
    }

    // The two transfers exactly as libnds does them (dspFifoSend and
    // dspFifoRecv, read off its library): the same register order, the same
    // short wait before each access -- only every wait is bounded here, so a
    // transfer that stops moving is counted instead of hanging the probe.
    // (That everything arrived is what the read-back shows.)
    inline void spinWait()
    {
        asm volatile("nop\n\tnop");
    }

    int lastStall = 0; // 1 write, 2 read, 3 display list, 4 ping

    /// Words into the DSP's data memory.
    bool pdataWrite(const std::uint16_t *src, std::uint16_t dst, int count)
    {
        spinWait();
        REG_DSP_PCFG = (REG_DSP_PCFG & 0x0FFD) | DSP_PCFG_MEMSEL_DATA | DSP_PCFG_AUTOINC;
        REG_DSP_PADR = dst;
        bool ok = true;
        for (int i = 0; i < count && ok; ++i)
        {
            spinWait();
            int spin = 0;
            while ((REG_DSP_PSTS & DSP_PSTS_WR_FIFO_FULL) && ++spin < kSpin)
                ;
            ok = spin < kSpin;
            if (ok)
                REG_DSP_PDATA = src[i];
        }
        spinWait();
        REG_DSP_PCFG &= ~0xE;
        if (!ok)
            lastStall = 1;
        return ok;
    }

    /// Words out of it.
    bool pdataRead(std::uint16_t src, std::uint16_t *dst, int count)
    {
        REG_DSP_PADR = src;
        spinWait();
        REG_DSP_PCFG = (REG_DSP_PCFG & 0x0FFD) | DSP_PCFG_RLEN_FREE | DSP_PCFG_AUTOINC | DSP_PCFG_MEMSEL_DATA |
                       DSP_PCFG_RSTART;
        bool ok = true;
        for (int i = 0; i < count && ok; ++i)
        {
            spinWait();
            int spin = 0;
            while (!(REG_DSP_PSTS & DSP_PSTS_RD_FIFO_READY) && ++spin < kSpin)
                ;
            ok = spin < kSpin;
            if (ok)
                dst[i] = REG_DSP_PDATA;
        }
        spinWait();
        REG_DSP_PCFG &= ~0x1E;
        // (What the FIFO had read ahead, thrown away.)
        for (int spin = 0; spin < 64 && (REG_DSP_PSTS & DSP_PSTS_RD_FIFO_READY); ++spin)
            (void)REG_DSP_PDATA;
        if (!ok)
            lastStall = 2;
        return ok;
    }

    bool command(std::uint16_t cmd, std::uint16_t a1, std::uint16_t a2, std::uint16_t &answer)
    {
        auto wait = [](bool (*ready)(int), int id)
        {
            for (int spin = 0; spin < 200000; ++spin)
            {
                if (ready(id))
                    return true;
            }
            return false;
        };
        if (!wait(dspSendDataReady, 1))
            return false;
        dspSendData(1, a1);
        if (!wait(dspSendDataReady, 2))
            return false;
        dspSendData(2, a2);
        if (!wait(dspSendDataReady, 0))
            return false;
        dspSendData(0, cmd);
        if (!wait(dspReceiveDataReady, 1))
            return false;
        dspReceiveData(1);
        if (!wait(dspReceiveDataReady, 0))
            return false;
        answer = dspReceiveData(0);
        return true;
    }

    std::uint16_t sendAt = 0;
    unsigned restarts = 0;

    bool startDsp()
    {
        if (dspExecuteDefaultTLF(hlDspProgram) != DSP_EXEC_OK)
            return false;
        std::uint16_t answer = 0;
        return command(HL_DSP_CMD_PING, 0, 0, answer) && answer == HL_DSP_MAGIC &&
               command(HL_DSP_CMD_INFO, HL_DSP_INFO_SEND, 0, sendAt);
    }

    void restartDsp()
    {
        dspPowerOff();
        ++restarts;
        startDsp();
    }

    struct Condition
    {
        const char *name;
        Gx gx;
        bool sequenced;
        bool words;
        unsigned frames, fails, stalls, wrong, deaf;
    };
    Condition conds[kConditions] = {
        {"PDATA", Gx::None, true, true, 0, 0, 0, 0, 0},    {"DMA", Gx::Dma, true, false, 0, 0, 0, 0, 0},
        {"NDMA", Gx::Ndma, true, false, 0, 0, 0, 0, 0},    {"seq DMA", Gx::Dma, true, true, 0, 0, 0, 0, 0},
        {"seq NDMA", Gx::Ndma, true, true, 0, 0, 0, 0, 0}, {"ovl NDMA", Gx::Ndma, false, true, 0, 0, 0, 0, 0},
        {"ovl DMA", Gx::Dma, false, true, 0, 0, 0, 0, 0},
    };

    std::uint16_t out[kChunk], back[kChunk];
    std::uint32_t frameUs = 0, frameUsMost = 0;

    /// One frame of a condition: true if all went through.
    void runFrame(Condition &c, unsigned frame)
    {
        bool stalled = false, wrong = false, dspStalled = false;
        startGx(c.gx);
        if (c.sequenced && !waitGx(c.gx))
        {
            stalled = true;
            lastStall = 3;
        }
        for (int k = 0; k < kChunks && !stalled && c.words; ++k)
        {
            for (int i = 0; i < kChunk; ++i)
                out[i] = static_cast<std::uint16_t>(frame * 977 + k * 131 + i * 7);
            const std::uint16_t at = static_cast<std::uint16_t>(sendAt + k * kChunk);
            if (!pdataWrite(out, at, kChunk) || !pdataRead(at, back, 64))
            {
                stalled = dspStalled = true;
                break;
            }
            wrong = wrong || std::memcmp(out, back, 64 * sizeof(std::uint16_t)) != 0;
        }
        if (!c.sequenced && !waitGx(c.gx))
        {
            stalled = true;
            lastStall = 3;
        }
        std::uint16_t answer = 0;
        const bool deaf = !command(HL_DSP_CMD_PING, 0, 0, answer) || answer != HL_DSP_MAGIC;
        if (deaf)
            lastStall = 4;
        ++c.frames;
        if (stalled || wrong || deaf)
        {
            ++c.fails;
            c.stalls += stalled;
            c.wrong += wrong;
            c.deaf += deaf;
            // (Only a DSP that failed is started again: a display list given
            // up on is the geometry engine's, and it was started again.)
            if (dspStalled || wrong || deaf)
                restartDsp();
        }
    }
} // namespace

int main()
{
    videoSetMode(MODE_0_3D);
    consoleDemoInit();
    setUp3D();
    buildList();

    const bool dsi = isDSiMode() && hlDspProgramBytes != 0;
    const bool up = dsi && startDsp();

    unsigned frame = 0;
    for (;;)
    {
        const int c = (frame / kFramesPerCondition) % kConditions;
        if (up)
        {
            glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE);
            glColor(RGB15(31, 31, 31));
            cpuStartTiming(0);
            runFrame(conds[c], frame);
            frameUs = static_cast<std::uint32_t>((static_cast<std::uint64_t>(cpuEndTiming()) * 1000000) / BUS_CLOCK);
            frameUsMost = frameUs > frameUsMost ? frameUs : frameUsMost;
        }
        glFlush(0);
        swiWaitForVBlank();
        ++frame;

        // (24 lines of 32, each written over in place, padded: everything
        // fits, nothing scrolls, nothing flickers.)
        char line[64];
        int row = 0;
        auto say = [&](const char *text)
        {
            std::printf("\x1b[%d;0H%-31.31s", row++, text);
        };
        say("DSP bus probe (PDATA path)");
        if (!dsi)
        {
            say("DSi only (and the DSP program");
            say("must be built in).");
            continue;
        }
        static const char *const kWhere[] = {"-", "write", "read", "3D list", "ping"};
        std::snprintf(line, sizeof(line), "DSP %s, restarts %u", up ? "up" : "DID NOT START", restarts);
        say(line);
        std::snprintf(line, sizeof(line), "last stuck: %s", kWhere[lastStall]);
        say(line);
        std::snprintf(line, sizeof(line), "GXSTAT then: %08lX", static_cast<unsigned long>(stallStatus));
        say(line);
        std::snprintf(line, sizeof(line), "frame us: %lu (most %lu)", static_cast<unsigned long>(frameUs),
                      static_cast<unsigned long>(frameUsMost));
        say(line);
        say("");
        say("  cond    frm fail stl bad dea");
        for (int i = 0; i < kConditions; ++i)
        {
            const Condition &k = conds[i];
            std::snprintf(line, sizeof(line), "%d%c%-8.8s%4u%5u%4u%4u%4u", i + 1, i == c ? '>' : ' ', k.name,
                          k.frames, k.fails, k.stalls, k.wrong, k.deaf);
            say(line);
        }
        say("");
        say("stl: a transfer or list stuck");
        say("bad: read back wrong");
        say("dea: no answer to a ping");
        say("");
        std::snprintf(line, sizeof(line), "%d frames each. A photo when", kFramesPerCondition);
        say(line);
        say("row 5 is full, or at 2 min.");
    }
}
