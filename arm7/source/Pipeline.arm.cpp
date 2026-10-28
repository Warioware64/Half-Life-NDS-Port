// The pipeline. The console's own mixer adds up the sounds' channels; its
// left and right outputs are captured (capture units 0 and 1) into rings
// here in the ARM7's memory; every block -- 64 samples -- what was captured
// is taken, the room's effect put on it (HlRoomDsp.hpp: the game's rooms),
// the music added after it (as the game's, it is not in the room), laid out
// for speakers, headphones or one channel, and written
// into the rings channels 1 and 3 play, which the speakers take directly and
// the mixer does not hear (SOUNDCNT): so nothing that is added is captured
// again. tools/hwtest/routing measured all of it on a DS and a DSi.
//
// All of it runs off the sound clock: the channels and capture at 32728 Hz
// (512 ticks a sample), the block timer once every 64 samples, set going
// together -- so block b is the rings' samples b*64 .. b*64+63, captured by
// the end of the block, and written back kLead samples later in the rings,
// ahead of where channels 1 and 3 are playing by more than any lateness the
// main loop has (a block, two at the very worst).

#include <nds.h>

#include "HlRoomDsp.hpp"
#include "HlStereoMusic.hpp"
#include "Pipeline.hpp"

using namespace hldata;

namespace hlaudio
{
    namespace
    {
        constexpr int kRing = AudioLink::kOutRing; ///< samples (a power of two)
        constexpr int kBlock = 64;
        constexpr int kLead = 256; ///< 4 blocks: 7.8 ms
        constexpr std::uint16_t kRateTimer = static_cast<std::uint16_t>(0x10000 - 512); // 32728 Hz
        constexpr int kOutRate = 32728;

        alignas(32) std::int16_t capLeft[kRing];
        alignas(32) std::int16_t capRight[kRing];
        std::int16_t roomLeft[kBlock], roomRight[kBlock];
        std::int32_t mixLeft[kBlock], mixRight[kBlock];
        RoomDsp room; // (23 KB: its delay lines)
        bool roomInit = false;
        std::uint8_t effects = kAudioEffectsClassic;
        std::uint8_t output = kAudioOutputSpeakers;

        bool on = false;
        std::uint32_t block = 0; ///< blocks done since it was set going
        StereoMusic music;

        void channel(int n, const void *ring, int pan)
        {
            SCHANNEL_CR(n) = 0;
            SCHANNEL_SOURCE(n) = reinterpret_cast<std::uint32_t>(ring);
            SCHANNEL_TIMER(n) = kRateTimer;
            SCHANNEL_REPEAT_POINT(n) = 0;
            SCHANNEL_LENGTH(n) = kRing / 2;
            SCHANNEL_CR(n) = 127u | static_cast<std::uint32_t>(pan) << 16 | 1u << 27 | 1u << 29 | 1u << 31;
        }
    } // namespace

    bool pipelineOn()
    {
        return on;
    }

    void pipelineStart(AudioLink &link)
    {
        if (on)
            return;
        if (!roomInit)
        {
            room.init(kOutRate, 3);
            roomInit = true;
        }
        const int ime = enterCriticalSection();
        REG_SNDCAP0CNT = 0;
        REG_SNDCAP1CNT = 0;
        SCHANNEL_CR(1) = 0;
        SCHANNEL_CR(3) = 0;
        for (int i = 0; i < kRing; ++i)
        {
            capLeft[i] = capRight[i] = 0;
            link.outLeft[i] = link.outRight[i] = 0;
        }
        // The speakers from channels 1 and 3 (hard left, hard right), which
        // the mixer -- and so the capture -- leaves out.
        REG_SOUNDCNT = static_cast<std::uint16_t>((REG_SOUNDCNT & 0x7F) | 1 << 8 | 2 << 10 | 1 << 12 | 1 << 13 | 1 << 15);
        REG_SNDCAP0DAD = reinterpret_cast<std::uint32_t>(capLeft);
        REG_SNDCAP0LEN = kRing / 2;
        REG_SNDCAP1DAD = reinterpret_cast<std::uint32_t>(capRight);
        REG_SNDCAP1LEN = kRing / 2;
        REG_SNDCAP0CNT = SNDCAPCNT_REPEAT | SNDCAPCNT_FORMAT_16BIT | SNDCAPCNT_START_BUSY;
        REG_SNDCAP1CNT = SNDCAPCNT_REPEAT | SNDCAPCNT_FORMAT_16BIT | SNDCAPCNT_START_BUSY;
        channel(1, link.outLeft, 0);
        channel(3, link.outRight, 127);
        // (The blocks from here: the timer started again with them.)
        TIMER_CR(0) = 0;
        TIMER_DATA(0) = 0;
        TIMER_CR(0) = TIMER_ENABLE | TIMER_DIV_1 | TIMER_IRQ_REQ;
        block = 0;
        on = true;
        leaveCriticalSection(ime);
    }

    void pipelineStop()
    {
        if (!on)
            return;
        REG_SNDCAP0CNT = 0;
        REG_SNDCAP1CNT = 0;
        SCHANNEL_CR(1) = 0;
        SCHANNEL_CR(3) = 0;
        REG_SOUNDCNT = static_cast<std::uint16_t>((REG_SOUNDCNT & 0x7F) | 1 << 15);
        on = false;
    }

    void pipelineRun(AudioLink &link, std::uint32_t blocks)
    {
        if (!on)
            return;
        const std::uint32_t written = link.musicWrite;
        for (std::uint32_t n = 0; n < blocks; ++n, ++block)
        {
            const int from = static_cast<int>((block * kBlock) & (kRing - 1));
            // The room on what the console played...
            if (effects == kAudioEffectsClassic)
            {
                room.process(capLeft + from, capRight + from, roomLeft, roomRight, kBlock);
                for (int i = 0; i < kBlock; ++i)
                {
                    mixLeft[i] = roomLeft[i];
                    mixRight[i] = roomRight[i];
                }
            }
            else
            {
                for (int i = 0; i < kBlock; ++i)
                {
                    mixLeft[i] = capLeft[from + i];
                    mixRight[i] = capRight[from + i];
                }
            }
            // ... the music after it ...
            music.mix(mixLeft, mixRight, kBlock, written);
            // ... laid out: the console's speakers are close together, and
            // a little less apart is easier on them (six tenths of what is
            // to one side); headphones have all of it; one channel, both.
            if (output == kAudioOutputSpeakers)
            {
                for (int i = 0; i < kBlock; ++i)
                {
                    const std::int32_t middle = (mixLeft[i] + mixRight[i]) >> 1;
                    const std::int32_t side = (((mixLeft[i] - mixRight[i]) >> 1) * 154) >> 8;
                    mixLeft[i] = middle + side;
                    mixRight[i] = middle - side;
                }
            }
            else if (output == kAudioOutputMono)
            {
                for (int i = 0; i < kBlock; ++i)
                    mixLeft[i] = mixRight[i] = (mixLeft[i] + mixRight[i]) >> 1;
            }
            const int to = static_cast<int>((block * kBlock + kLead) & (kRing - 1));
            for (int i = 0; i < kBlock; ++i)
            {
                const std::int32_t l = mixLeft[i], r = mixRight[i];
                link.outLeft[to + i] = static_cast<std::int16_t>(l < -32768 ? -32768 : (l > 32767 ? 32767 : l));
                link.outRight[to + i] = static_cast<std::int16_t>(r < -32768 ? -32768 : (r > 32767 ? 32767 : r));
            }
        }
        if (music.on())
        {
            link.musicRead = music.played();
            link.musicDry = music.dry();
            link.musicOver = music.over() ? 1 : 0;
        }
    }

    void pipelineMode(std::uint8_t newEffects, std::uint8_t newOutput)
    {
        // (The room going off and on again: what it had, emptied.)
        if (newEffects == kAudioEffectsClassic && effects != kAudioEffectsClassic && roomInit)
        {
            const int was = room.room();
            room.init(kOutRate, 3);
            room.setRoom(was);
        }
        effects = newEffects == kAudioEffectsClassic ? kAudioEffectsClassic : kAudioEffectsOff;
        output = newOutput < 3 ? newOutput : kAudioOutputSpeakers;
    }

    void pipelineRoom(int which)
    {
        if (!roomInit)
        {
            room.init(kOutRate, 3);
            roomInit = true;
        }
        room.setRoom(which);
    }

    void musicOpen(AudioLink &link, const AudioCommand &command)
    {
        link.musicRead = 0;
        link.musicDry = 0;
        link.musicOver = 0;
        music.open(reinterpret_cast<const std::uint16_t *>(command.a), command.b & 0xFFFF, command.b >> 16, kOutRate,
                   command.c, static_cast<int>(command.d));
    }

    void musicStop(AudioLink &link)
    {
        music.close();
        link.musicOver = 1;
    }

    void musicVolume(int volume)
    {
        music.setVolume(volume);
    }
} // namespace hlaudio
