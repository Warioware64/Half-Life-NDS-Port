// The DSi's DSP's rooms (Enhanced): the numbers each of the game's rooms is
// given (shared/HlDspRoom.hpp) fit the DSP's lines; and its model
// (shared/dsp/hl_dsp.h), which its program matches sample for sample
// (tools/dspsim), puts out what a room should -- nothing for a normal one,
// a reverb in stereo dying away for a chamber, the game's echo far off for
// the outside -- and never runs away.

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "HlDspRoom.hpp"
#include "TestBspBuilder.hpp"

extern "C"
{
#include "dsp/hl_dsp.h"
}

using namespace hldata;
using namespace hldstest;

namespace
{
    /// The room taken, `count` samples sent (at a quarter of the output
    /// rate), `ticks` pairs worked out; the pairs' left and right.
    void play(int room, const std::vector<std::int16_t> &send, int ticks, std::vector<int> &left,
              std::vector<int> &right)
    {
        hlDspInit();
        std::uint16_t block[HL_DSP_ROOM_WORDS];
        dspRoomBlock(room, block);
        for (int i = 0; i < HL_DSP_ROOM_WORDS; ++i)
            hlRoomBlock[i] = block[i];
        hlDspCommand(HL_DSP_CMD_ROOM, 0, 0);
        left.clear();
        right.clear();
        std::size_t sent = 0;
        for (int n = 0; n < ticks; ++n)
        {
            // (Kept a little ahead of the reverb's need: a quarter a tick.)
            while (sent < send.size() && sent < static_cast<std::size_t>(n / 4 + 64))
            {
                hlSendRing[hlSendWrite] = send[sent++];
                hlDspCommand(HL_DSP_CMD_SEND_FED, static_cast<std::uint16_t>((hlSendWrite + 1) % HL_DSP_SEND_WORDS), 0);
            }
            hlDspTick();
            left.push_back(hlOutL);
            right.push_back(hlOutR);
        }
    }

    double energy(const std::vector<int> &s, int from, int to)
    {
        double sum = 0;
        for (int i = from; i < to && i < static_cast<int>(s.size()); ++i)
            sum += static_cast<double>(s[i]) * s[i];
        return sum;
    }
} // namespace

int main()
{
    const int second = 47605;

    // Every room's numbers within the DSP's lines.
    {
        bool fits = true;
        for (int room = 0; room < kRoomCount; ++room)
        {
            std::uint16_t b[HL_DSP_ROOM_WORDS];
            dspRoomBlock(room, b);
            fits = fits && b[HL_DSP_ROOM_ER_LEN] >= 1 && b[HL_DSP_ROOM_ER_LEN] <= HL_DSP_ER_WORDS &&
                   b[HL_DSP_ROOM_DLY_LEN] <= HL_DSP_DLY_WORDS && b[HL_DSP_ROOM_FDN_GAIN] < 32768 &&
                   b[HL_DSP_ROOM_FDN_DAMP] <= 16384;
            for (int i = 0; i < 4; ++i)
                fits = fits && b[HL_DSP_ROOM_ER_TAP + i] < b[HL_DSP_ROOM_ER_LEN] &&
                       b[HL_DSP_ROOM_FDN_LEN + i] >= 1 && b[HL_DSP_ROOM_FDN_LEN + i] <= HL_DSP_FDN_LINE;
        }
        check(fits, "every room's numbers fit the DSP's lines");
    }

    std::vector<std::int16_t> click(400, 0);
    click[0] = 30000;
    std::vector<int> l, r;

    // Normal: nothing.
    play(0, click, second / 2, l, r);
    check(energy(l, 0, second / 2) == 0 && energy(r, 0, second / 2) == 0, "a normal room adds nothing");

    // A chamber: a reverb, in stereo, dying away.
    play(9, click, 3 * second, l, r);
    {
        const double early = energy(l, 0, second / 4) + energy(r, 0, second / 4);
        const double late = energy(l, 2 * second, 3 * second) + energy(r, 2 * second, 3 * second);
        double apart = 0;
        for (int i = 0; i < second; ++i)
            apart += static_cast<double>(l[i] - r[i]) * (l[i] - r[i]);
        check(early > 1e6 && late < early / 50, "a chamber: a reverb that dies away");
        check(apart > early / 10, "... and the two sides unlike: stereo");
        if (!(early > 1e6 && late < early / 50))
            std::printf("test_dsproom: chamber %.3g %.3g\n", early, late);
    }

    // Outside (big 2): the game's echo, 350 ms off.
    play(21, click, second, l, r);
    {
        int loudest = second / 10;
        for (int i = second / 10; i < second * 6 / 10; ++i)
        {
            if (std::abs(l[i]) > std::abs(l[loudest]))
                loudest = i;
        }
        const int ms = loudest * 1000 / second;
        check(ms >= 345 && ms <= 360 && std::abs(l[loudest]) > 500, "outside: the echo comes back 350 ms off");
        if (!(ms >= 345 && ms <= 360))
            std::printf("test_dsproom: outside echo at %d ms (%d)\n", ms, l[loudest]);
    }

    // Loud noise, every room: nothing runs away, and it dies once it stops.
    {
        std::vector<std::int16_t> noise(second / 4);
        std::uint32_t seed = 5;
        for (std::int16_t &s : noise)
        {
            seed = seed * 1664525u + 1013904223u;
            s = static_cast<std::int16_t>(static_cast<int>(seed >> 16) - 32768);
        }
        bool settle = true;
        for (int room = 1; room < kRoomCount; ++room)
        {
            play(room, noise, 6 * second, l, r);
            const double loud = energy(l, 0, second) + energy(r, 0, second);
            const double tail = energy(l, 5 * second, 6 * second) + energy(r, 5 * second, 6 * second);
            if (tail > loud / 20)
            {
                settle = false;
                std::printf("test_dsproom: room %d: tail %.3g of %.3g\n", room, tail, loud);
            }
        }
        check(settle, "every room dies away once the sound stops");
    }

    if (g_failures == 0)
        std::printf("test_dsproom: ok\n");
    return g_failures == 0 ? 0 : 1;
}
