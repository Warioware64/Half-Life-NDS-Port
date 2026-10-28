// dspsim: the game's DSP program run in an emulated Teak (teakra), against
// its own arithmetic run on the host.
//
// shared/dsp/hl_dsp.h is compiled twice: by the Teak compiler into
// build/teak.elf, and here. This plays the same scene to both -- a stereo
// tune in the music ring, a burst of noise sent to echo in a room -- and
// compares what comes out of the emulated speakers' port with what the host
// worked out, sample for sample. It also says how much of a tick the program
// used.
//
// usage: dspsim build/teak.elf        (run.sh builds and runs it)

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <teakra/teakra.h>

extern "C"
{
#include "hl_dsp.h"
}

#include "HlDspRoom.hpp"

namespace
{
    bool loadElf(Teakra::Teakra &t, const char *path)
    {
        FILE *f = std::fopen(path, "rb");
        if (!f)
            return false;
        std::vector<uint8_t> d;
        uint8_t buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
            d.insert(d.end(), buf, buf + n);
        std::fclose(f);
        auto u16 = [&](size_t o) { return (uint32_t)d[o] | ((uint32_t)d[o + 1] << 8); };
        auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
        const uint32_t shoff = u32(0x20), shentsize = u16(0x2E), shnum = u16(0x30);
        for (uint32_t i = 0; i < shnum; i++)
        {
            const size_t sh = shoff + i * shentsize;
            const uint32_t type = u32(sh + 4), flags = u32(sh + 8), addr = u32(sh + 12);
            const uint32_t off = u32(sh + 16), size = u32(sh + 20);
            if (type != 1 || !(flags & 2) || size == 0) // PROGBITS + ALLOC
                continue;
            for (uint32_t w = 0; w < size / 2; w++)
            {
                const uint16_t v = (uint16_t)u16(off + w * 2);
                if (addr >= 0x10000000)
                    t.DataWrite((uint16_t)((addr - 0x10000000) / 2 + w), v, true);
                else
                    t.ProgramWrite(addr / 2 + w, v);
            }
        }
        return true;
    }

    /// Where the program's variables are: its ELF's symbols, by name, as
    /// addresses in the DSP's data memory.
    std::vector<std::pair<std::string, uint16_t>> symbols;

    void loadSymbols(const char *path)
    {
        FILE *f = std::fopen(path, "rb");
        if (!f)
            return;
        std::vector<uint8_t> d;
        uint8_t buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
            d.insert(d.end(), buf, buf + n);
        std::fclose(f);
        auto u16 = [&](size_t o) { return (uint32_t)d[o] | ((uint32_t)d[o + 1] << 8); };
        auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
        const uint32_t shoff = u32(0x20), shentsize = u16(0x2E), shnum = u16(0x30);
        for (uint32_t i = 0; i < shnum; i++)
        {
            const size_t sh = shoff + i * shentsize;
            if (u32(sh + 4) != 2) // SYMTAB
                continue;
            const uint32_t off = u32(sh + 16), size = u32(sh + 20), link = u32(sh + 24);
            const uint32_t strings = u32(shoff + link * shentsize + 16);
            for (uint32_t at = off; at + 16 <= off + size; at += 16)
            {
                const uint32_t value = u32(at + 4);
                const char *name = (const char *)&d[strings + u32(at)];
                if (value >= 0x10000000 && name[0] != '\0')
                    symbols.push_back({name, (uint16_t)((value - 0x10000000) / 2)});
            }
        }
    }

    int symbol(const std::string &name)
    {
        for (const auto &entry : symbols)
        {
            if (entry.first == name)
                return entry.second;
        }
        return -1;
    }

    /// A stereo tune as the music ring holds it: blocks, each a mark, the
    /// decoders' states at its start, and its samples.
    std::vector<uint16_t> makeMusic(int blocks, double loud = 9000.0)
    {
        std::vector<uint16_t> words;
        hlDspInit();
        int at = 0;
        for (int b = 0; b < blocks; b++)
        {
            words.push_back(HL_DSP_BLOCK_MARK);
            words.push_back((uint16_t)hlValue[0]);
            words.push_back((uint16_t)hlIndex[0]);
            words.push_back((uint16_t)hlValue[1]);
            words.push_back((uint16_t)hlIndex[1]);
            for (int w = 0; w < HL_DSP_BLOCK_WORDS; w++)
            {
                uint16_t word = 0;
                for (int half = 0; half < 2; half++, at++)
                {
                    for (int c = 0; c < 2; c++)
                    {
                        const double hz = c == 0 ? 440.0 : 660.0;
                        const int want = (int)std::lround(std::sin(at * 2.0 * M_PI * hz / 23802.0) * loud);
                        const int step = hlStepTable[hlIndex[c]];
                        int delta = want - hlValue[c], code = 0;
                        if (delta < 0)
                        {
                            code = 8;
                            delta = -delta;
                        }
                        int size = (delta * 4) / step;
                        code |= size > 7 ? 7 : size;
                        // The state moves on exactly as the decoder's will.
                        hlDecode((uint16_t)c, (uint16_t)code);
                        word |= (uint16_t)(code << (half * 8 + c * 4));
                    }
                }
                words.push_back(word);
            }
        }
        hlDspInit();
        return words;
    }
} // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: dspsim teak.elf\n");
        return 2;
    }
    Teakra::UserConfig config;
    Teakra::Teakra t(config);
    Teakra::AHBMCallback none;
    none.read8 = [](uint32_t) { return (uint8_t)0; };
    none.write8 = [](uint32_t, uint8_t) {};
    none.read16 = [](uint32_t) { return (uint16_t)0; };
    none.write16 = [](uint32_t, uint16_t) {};
    none.read32 = [](uint32_t) { return (uint32_t)0; };
    none.write32 = [](uint32_t, uint32_t) {};
    t.SetAHBMCallback(none);
    std::vector<int16_t> heard; // left, right, left, right...
    t.SetAudioCallback([&](std::array<int16_t, 2> pair)
                       {
                           heard.push_back(pair[0]);
                           heard.push_back(pair[1]);
                       });
    t.Reset();
    if (!loadElf(t, argv[1]))
    {
        std::fprintf(stderr, "cannot load %s\n", argv[1]);
        return 2;
    }
    loadSymbols(argv[1]);

    uint64_t cycles = 0;
    bool dead = false;
    auto run = [&](unsigned n)
    {
        t.Run(n);
        cycles += n;
    };
    // A command, as the ARM9 sends one; its answer and the tick it took
    // effect before.
    auto command = [&](uint16_t cmd, uint16_t a1, uint16_t a2, uint16_t &tick) -> uint16_t
    {
        uint64_t waited = 0;
        auto wait = [&](bool ready)
        {
            (void)ready;
            run(16);
            waited += 16;
        };
        while (!t.SendDataIsEmpty(1) && waited < 50000000)
            wait(false);
        t.SendData(1, a1);
        while (!t.SendDataIsEmpty(2) && waited < 50000000)
            wait(false);
        t.SendData(2, a2);
        while (!t.SendDataIsEmpty(0) && waited < 50000000)
            wait(false);
        t.SendData(0, cmd);
        while (!t.RecvDataIsReady(1) && waited < 50000000)
            wait(false);
        tick = t.RecvData(1);
        while (!t.RecvDataIsReady(0) && waited < 50000000)
            wait(false);
        if (waited >= 50000000)
            dead = true;
        return t.RecvData(0);
    };

    uint16_t tick = 0;
    const uint16_t magic = command(HL_DSP_CMD_PING, 0, 0, tick);
    if (dead || magic != HL_DSP_MAGIC)
    {
        std::printf("dspsim: FAIL: the program does not answer a ping (%04X)\n", magic);
        return 1;
    }
    const uint16_t musicAt = command(HL_DSP_CMD_INFO, HL_DSP_INFO_MUSIC, 0, tick);
    const uint16_t sendAt = command(HL_DSP_CMD_INFO, HL_DSP_INFO_SEND, 0, tick);
    const uint16_t roomAt = command(HL_DSP_CMD_INFO, HL_DSP_INFO_ROOM, 0, tick);
    std::printf("dspsim: ping answered; music ring at %04X, echo ring at %04X\n", musicAt, sendAt);

    // The scene. A hall; a burst of noise to echo in it; then a tune.
    // (DSPSIM_SCENE=loud: everything as loud as it goes, a room that hardly
    // dies away -- so that what holds a sum within sixteen bits is gone
    // through, which a quiet scene never reaches.)
    const char *scene = std::getenv("DSPSIM_SCENE");
    const bool loudScene = scene != nullptr && std::string(scene) == "loud";
    // (A large concrete room -- reflections, reverb, and an echo 0.3 s off --
    // or, loud, the strangest the game has: weirdo 3, a reverb going round
    // all but whole.)
    uint16_t roomBlock[HL_DSP_ROOM_WORDS];
    hldata::dspRoomBlock(loudScene ? 28 : 19, roomBlock);
    // The room's numbers written where the program said, then taken.
    auto room = [&](uint16_t &when)
    {
        for (int i = 0; i < HL_DSP_ROOM_WORDS; i++)
            t.DataWrite((uint16_t)(roomAt + i), roomBlock[i], true);
        command(HL_DSP_CMD_ROOM, 0, 0, when);
    };
    std::vector<int16_t> burst(600);
    uint32_t seed = 99;
    for (int16_t &s : burst)
    {
        seed = seed * 1664525u + 1013904223u;
        s = loudScene ? (int16_t)((seed >> 16) & 1 ? 32767 : -32768) : (int16_t)((int)(seed >> 17) - 16384);
    }
    const std::vector<uint16_t> music = makeMusic(3, loudScene ? 31000.0 : 9000.0); // three blocks: 3087 words

    uint16_t roomTick, sendTick, musicTick;
    room(roomTick);
    for (size_t i = 0; i < burst.size(); i++)
        t.DataWrite((uint16_t)(sendAt + i), (uint16_t)burst[i], true);
    command(HL_DSP_CMD_SEND_FED, (uint16_t)burst.size(), 0, sendTick);
    for (size_t i = 0; i < music.size(); i++)
        t.DataWrite((uint16_t)(musicAt + i), music[i], true);
    // (DSPSIM_ONLY=echo leaves the tune out, to hear the echo alone.)
    const char *only = std::getenv("DSPSIM_ONLY");
    const bool tune = only == nullptr || std::string(only) != "echo";
    musicTick = 0xFFFF;
    if (tune)
        command(HL_DSP_CMD_MUSIC_START, (uint16_t)music.size(), 0, musicTick);

    // Long enough for the tune to run out and the echo to die down.
    const uint64_t before = cycles;
    const size_t heardBefore = heard.size() / 2;
    // DSPSIM_STATE=<n>: once n pairs have been heard, the program's
    // variables are read out of its memory, to be set beside the model's at
    // the same tick. For finding where the two part company.
    struct Seen
    {
        std::string name;
        int value;
    };
    std::vector<Seen> seen;
    long stateTick = -1;
    if (const char *text = std::getenv("DSPSIM_STATE"))
    {
        const size_t after = (size_t)std::atol(text);
        while (heard.size() / 2 < after && cycles < 4000000000ull)
            run(64);
        auto peek = [&](const std::string &name, int offset)
        {
            const int at = symbol(name);
            if (at >= 0)
                seen.push_back({name + (offset ? "+" + std::to_string(offset) : ""),
                                (int16_t)t.DataRead((uint16_t)(at + offset), true)});
        };
        for (const char *name : {"hlTick", "hlSendRead", "hlEchoIn", "hlEchoOutL", "hlEchoOutR", "hlWetL",
                                 "hlWetR", "hlLevel", "hlErPos", "hlDlyPos", "hlDlyLast1"})
            peek(name, 0);
        for (int offset : {0, 1, 2, 3})
        {
            peek("hlFdnLow", offset);
            peek("hlFdnPos", offset);
            peek("hlRvM", offset);
            peek("hlRvTap", offset);
        }
        stateTick = seen.empty() ? -1 : (uint16_t)seen[0].value;
    }
    while (heard.size() / 2 < 30000 && cycles < 4000000000ull)
        run(4096);
    const size_t ticks = heard.size() / 2;
    const double perTick = (double)(cycles - before) / (double)(ticks - heardBefore);

    // The same on the host.
    hlDspInit();
    std::vector<int16_t> model;
    for (uint32_t n = 0; n < ticks + 64; n++)
    {
        const uint16_t now = (uint16_t)n;
        if (n < 65536 && now == roomTick && n >= roomTick)
        {
            for (int i = 0; i < HL_DSP_ROOM_WORDS; i++)
                hlRoomBlock[i] = roomBlock[i];
            hlDspCommand(HL_DSP_CMD_ROOM, 0, 0);
        }
        if (n < 65536 && now == sendTick)
        {
            for (size_t i = 0; i < burst.size(); i++)
                hlSendRing[i] = burst[i];
            hlDspCommand(HL_DSP_CMD_SEND_FED, (uint16_t)burst.size(), 0);
        }
        if (tune && n < 65536 && now == musicTick)
        {
            for (size_t i = 0; i < music.size(); i++)
                hlMusicRing[i] = music[i];
            hlDspCommand(HL_DSP_CMD_MUSIC_START, (uint16_t)music.size(), 0);
        }
        if ((long)n == stateTick)
        {
            auto mine = [&](const std::string &name) -> int
            {
                if (name == "hlTick") return (int16_t)hlTick;
                if (name == "hlSendRead") return hlSendRead;
                if (name == "hlEchoIn") return hlEchoIn;
                if (name == "hlEchoOutL") return hlEchoOutL;
                if (name == "hlEchoOutR") return hlEchoOutR;
                if (name == "hlWetL") return hlWetL;
                if (name == "hlWetR") return hlWetR;
                if (name == "hlLevel") return hlLevel;
                if (name == "hlErPos") return hlErPos;
                if (name == "hlDlyPos") return hlDlyPos;
                if (name == "hlDlyLast1") return hlDlyLast1;
                const int k = name.back() - '0';
                if (name.rfind("hlFdnLow+", 0) == 0) return hlFdnLow[k];
                if (name.rfind("hlFdnPos+", 0) == 0) return hlFdnPos[k];
                if (name.rfind("hlRvM+", 0) == 0) return hlRvM[k];
                if (name.rfind("hlRvTap+", 0) == 0) return hlRvTap[k];
                if (name == "hlFdnLow") return hlFdnLow[0];
                if (name == "hlFdnPos") return hlFdnPos[0];
                if (name == "hlRvM") return hlRvM[0];
                if (name == "hlRvTap") return hlRvTap[0];
                return 0;
            };
            for (const Seen &one : seen)
                std::printf("  %-14s program %6d   model %6d%s\n", one.name.c_str(), one.value,
                            mine(one.name), one.value == mine(one.name) ? "" : "   <--");
        }
        hlDspTick();
        model.push_back(hlOutL);
        model.push_back(hlOutR);
    }

    // The port holds a few pairs back: line the two up by the first one
    // that is not silence, then every pair has to be the same.
    auto firstSound = [](const std::vector<int16_t> &v) -> long
    {
        for (size_t i = 0; i + 1 < v.size(); i += 2)
        {
            if (v[i] != 0 || v[i + 1] != 0)
                return (long)(i / 2);
        }
        return -1;
    };
    const long a = firstSound(heard), b = firstSound(model);
    if (a < 0 || b < 0)
    {
        std::printf("dspsim: FAIL: silence (program %ld, model %ld)\n", a, b);
        return 1;
    }
    long same = 0, differ = 0, firstBad = -1;
    int loudest = 0;
    for (long n = 0; a + n < (long)ticks && b + n < (long)model.size() / 2; n++)
    {
        const int16_t hl = heard[(a + n) * 2], hr = heard[(a + n) * 2 + 1];
        const int16_t ml = model[(b + n) * 2], mr = model[(b + n) * 2 + 1];
        loudest = std::max(loudest, std::abs((int)hl));
        if (hl == ml && hr == mr)
            ++same;
        else
        {
            ++differ;
            if (firstBad < 0)
                firstBad = n;
        }
    }
    std::printf("dspsim: %ld pairs compared from the first sound (program tick %ld, model tick %ld): "
                "%ld the same, %ld different; loudest %d\n",
                same + differ, a, b, same, differ, loudest);
    (void)perTick;
    // The commands that ask: the loudest sample sent to echo is the
    // model's; nothing is called wrong; and a block of music that does not
    // begin with its mark is -- the music stopped, and said to be.
    {
        uint16_t ignored;
        const uint16_t heardMost = command(HL_DSP_CMD_HEARD, 0, 0, ignored);
        const uint16_t again = command(HL_DSP_CMD_HEARD, 0, 0, ignored);
        const uint16_t well = command(HL_DSP_CMD_STATUS, 0, 0, ignored);
        t.DataWrite(musicAt, 0x1234, true);
        command(HL_DSP_CMD_MUSIC_START, 64, 0, ignored);
        run(400000);
        const uint16_t ill = command(HL_DSP_CMD_STATUS, 0, 0, ignored);
        // (Stopped: the program's own word for it, read out of its memory.)
        const int onAt = symbol("hlMusicOn");
        const bool silent = onAt < 0 || t.DataRead((uint16_t)onAt, true) == 0;
        command(HL_DSP_CMD_MUSIC_STOP, 0, 0, ignored);
        std::printf("dspsim: loudest sent to echo %u (model %u), then %u; status %04X, after a bad block %04X%s\n", heardMost,
                    hlHeard, again, well, ill, silent ? "" : ", and the music NOT stopped");
        if (heardMost != hlHeard || again != 0 || (well & HL_DSP_FAULT) != 0 || (ill & HL_DSP_FAULT) == 0 ||
            !silent)
        {
            std::printf("dspsim: FAIL: the commands' answers\n");
            return 1;
        }
    }
    // How much of the processor the program uses, with everything going: a
    // tune and a room with something echoing in it. The main loop works a
    // pair out whenever the ring to the speakers has room for one, and only
    // then: so it is at work exactly while that ring is not full, which is
    // looked at after every step. (A step is what teakra counts: one
    // instruction, near enough -- the chip's own cycles are not modelled.
    // It is a measure to hold one program against another, and against the
    // 2815 or so steps there are to a pair.)
    {
        const int readAt = symbol("hlOutRead"), writeAt = symbol("hlOutWrite");
        uint16_t ignored;
        room(ignored);
        for (size_t i = 0; i < burst.size(); i++)
            t.DataWrite((uint16_t)(sendAt + i), (uint16_t)burst[i], true);
        command(HL_DSP_CMD_SEND_FED, 0, 0, ignored); // (once round: all of the ring is to echo)
        for (size_t i = 0; i < music.size(); i++)
            t.DataWrite((uint16_t)(musicAt + i), music[i], true);
        if (tune)
            command(HL_DSP_CMD_MUSIC_START, (uint16_t)music.size(), 0, ignored);
        run(200000);
        if (readAt >= 0 && writeAt >= 0)
        {
            const size_t pairsBefore = heard.size() / 2;
            const unsigned steps = 3000000;
            unsigned busy = 0;
            for (unsigned n = 0; n < steps; n++)
            {
                run(1);
                const uint16_t read = t.DataRead((uint16_t)readAt, true), write = t.DataRead((uint16_t)writeAt, true);
                uint16_t next = (uint16_t)(write + 2);
                if (next >= HL_DSP_OUT_WORDS)
                    next = 0;
                busy += next != read ? 1 : 0;
            }
            const size_t pairs = heard.size() / 2 - pairsBefore;
            const double load = 100.0 * busy / steps;
            std::printf("dspsim: load %.1f%% -- %.0f steps a pair at work, of %.0f between two pairs\n", load,
                        pairs ? (double)busy / pairs : 0.0, pairs ? (double)steps / pairs : 0.0);
            // (DSPSIM_BUDGET=<percent>: more than this is a failure.)
            const char *budget = std::getenv("DSPSIM_BUDGET");
            if (budget != nullptr && load > std::atof(budget))
            {
                std::printf("dspsim: FAIL: over the budget of %s%%\n", budget);
                return 1;
            }
        }
        else
        {
            std::printf("dspsim: load not measured (the program has no hlOutRead/hlOutWrite)\n");
        }
    }
    std::printf("dspsim: commands took effect before ticks %u (room), %u (echo), %u (music)\n", roomTick, sendTick, musicTick);
    if (differ != 0)
    {
        for (long n = firstBad - 2; n < firstBad + 14; n++)
            std::printf("  %2ld: program %6d %6d   model %6d %6d\n", n, heard[(a + n) * 2],
                        heard[(a + n) * 2 + 1], model[(b + n) * 2], model[(b + n) * 2 + 1]);
        std::printf("dspsim: FAIL: first difference %ld pairs in: program %d %d, model %d %d\n", firstBad,
                    heard[(a + firstBad) * 2], heard[(a + firstBad) * 2 + 1], model[(b + firstBad) * 2],
                    model[(b + firstBad) * 2 + 1]);
        return 1;
    }
    if (same < 20000 || loudest < 500)
    {
        std::printf("dspsim: FAIL: too little was heard to mean anything\n");
        return 1;
    }
    std::printf("dspsim: ok\n");
    return 0;
}
