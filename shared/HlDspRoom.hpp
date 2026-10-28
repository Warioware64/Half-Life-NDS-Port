#pragma once

// A room of the game's (HlRoomPresets.hpp) as the DSi's DSP has it, for the
// room effects' Enhanced: the numbers its reverb runs on (shared/dsp/
// hl_dsp_proto.h's HL_DSP_ROOM_*), worked out here and written into it.
//
// The game's own rooms are a reverb of two lines of the room's size and an
// echo; the DSP's, richer, from the same numbers:
//  * the first reflections: four places back along one line the room's size
//    long, two to each side;
//  * the reverb: four lines feeding each other, from that size to 1.61
//    times it, going round as much as the room's own does over the longer
//    way, duller where the room says so;
//  * the echo: the room's, as far and coming back as much, dulled or not;
//  * all of it three times as loud as the game's mix has it beside the
//    sound, since the DSi gives the DSP a quarter of its speakers and the
//    sound three (DspAudio).
// A room with no reverb has none of the first two; a room with neither
// (normal, water 1) is silence.
//
// std-only: the host tests run it.

#include <cstdint>

#include "HlRoomPresets.hpp"

extern "C"
{
#include "dsp/hl_dsp_proto.h"
}

namespace hldata
{
    inline constexpr int kDspRoomRate = 11901; ///< the DSP's reverb's rate: 47605 / 4

    inline void dspRoomBlock(int room, std::uint16_t out[HL_DSP_ROOM_WORDS])
    {
        for (int i = 0; i < HL_DSP_ROOM_WORDS; ++i)
            out[i] = 0;
        room = room < 0 || room >= kRoomCount ? 0 : room;
        const RoomPreset &p = kRoomPresets[room];
        auto samples = [](int ms, int most)
        {
            const int n = (ms * kDspRoomRate + 500) / 1000;
            return n < 0 ? 0 : (n > most ? most : n);
        };
        auto q15 = [](int thousandths)
        {
            const int q = thousandths * 32767 / 1000;
            return static_cast<std::uint16_t>(q < 0 ? 0 : (q > 32767 ? 32767 : q));
        };

        out[HL_DSP_ROOM_ER_LEN] = 1;
        for (int i = 0; i < 4; ++i)
            out[HL_DSP_ROOM_FDN_LEN + i] = 1;
        const int size = samples(p.reverbMs, HL_DSP_ER_WORDS);
        if (size >= 8)
        {
            // The first reflections.
            out[HL_DSP_ROOM_ER_LEN] = static_cast<std::uint16_t>(size);
            static constexpr int kTaps[4] = {13, 21, 34, 55}; // in hundredths of the size
            for (int i = 0; i < 4; ++i)
                out[HL_DSP_ROOM_ER_TAP + i] = static_cast<std::uint16_t>(size * kTaps[i] / 100);
            out[HL_DSP_ROOM_ER_MIX] = 19661; // 0.6
            // The reverb.
            static constexpr int kLengths[4] = {100, 119, 137, 161};
            for (int i = 0; i < 4; ++i)
            {
                const int n = size * kLengths[i] / 100;
                out[HL_DSP_ROOM_FDN_LEN + i] =
                    static_cast<std::uint16_t>(n < 16 ? 16 : (n > HL_DSP_FDN_LINE ? HL_DSP_FDN_LINE : n));
            }
            // (Round a longer way, a little more is lost each time: the
            // room's feedback over about 1.3 of its lengths.)
            const int fb = p.reverbFb - (1000 - p.reverbFb) * 29 / 100;
            out[HL_DSP_ROOM_FDN_GAIN] = q15(fb);
            out[HL_DSP_ROOM_FDN_DAMP] = p.reverbLow ? 6000 : 2500;
            out[HL_DSP_ROOM_LATE_MIX] = 22938; // 0.7
        }
        const int delay = samples(p.delayMs, HL_DSP_DLY_WORDS);
        if (delay >= 1)
        {
            out[HL_DSP_ROOM_DLY_LEN] = static_cast<std::uint16_t>(delay);
            // (As the game takes its feedback: in 255ths, applied in 256ths
            // -- 0.999 comes back as 0.992, and a resonant room still dies.)
            out[HL_DSP_ROOM_DLY_GAIN] = static_cast<std::uint16_t>((p.delayFb * 255 / 1000) << 7);
            out[HL_DSP_ROOM_DLY_LOW] = p.delayLow ? 1 : 0;
            out[HL_DSP_ROOM_ECHO_MIX] = 16384; // 0.5
        }
        const bool any = size >= 8 || delay >= 1;
        out[HL_DSP_ROOM_LEVEL] = any ? 192 : 0; // (three times the 64 that is as it is)
    }
} // namespace hldata
