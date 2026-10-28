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
// (512 ticks a sample) -- or, on a DSi at the high quality, at 47605 Hz (352),
// the rate its speakers are fed at -- the block timer once every 64 samples,
// set going together: so block b is the rings' samples b*64 .. b*64+63,
// captured by the end of the block, and written back kLead samples later in
// the rings, ahead of where channels 1 and 3 are playing by more than any
// lateness the main loop has (a block, two at the very worst). Held up for
// longer than that (the card, on a DSi, is the ARM7's to read), it does not
// try to catch up: the rings are emptied and it goes on from where the
// channels are -- a moment of silence, not the rings played round again.

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
        constexpr int kLead = 256; ///< 4 blocks: 7.8 ms (5.4 at the high quality)

        /// The two rates: sound-clock ticks a sample, the rate, and how many
        /// samples to one of the room's reverb (about 11 kHz either way).
        struct Rate
        {
            int ticks, hz, factor;
        };
        constexpr Rate kStandard = {512, 32728, 3};
        constexpr Rate kHigh = {352, 47605, 4};
        Rate rate = kStandard;
        std::uint8_t quality = kAudioQualityStandard;

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
            SCHANNEL_TIMER(n) = static_cast<std::uint16_t>(0x10000 - rate.ticks);
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
        // (The high quality only where the speakers are fed at its rate.)
        const Rate want = quality == kAudioQualityHigh && isDSiMode() ? kHigh : kStandard;
        if (!roomInit || want.hz != rate.hz)
        {
            const int was = roomInit ? room.room() : 0;
            rate = want;
            room.init(rate.hz, rate.factor);
            room.setRoom(was);
            roomInit = true;
        }
        music.setOutRate(static_cast<std::uint32_t>(rate.hz));
        // A block: 64 samples, in bus ticks (twice the sound clock's).
        const std::uint32_t blockTicks = static_cast<std::uint32_t>(kBlock * rate.ticks * 2);
        link.blockTicks = blockTicks;
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
        TIMER_DATA(0) = static_cast<std::uint16_t>(0x10000 - blockTicks);
        TIMER_CR(0) = TIMER_ENABLE | TIMER_DIV_1 | TIMER_IRQ_REQ;
        block = 0;
        on = true;
        leaveCriticalSection(ime);
    }

    void pipelineStop(AudioLink &link)
    {
        if (!on)
            return;
        const int ime = enterCriticalSection();
        REG_SNDCAP0CNT = 0;
        REG_SNDCAP1CNT = 0;
        SCHANNEL_CR(1) = 0;
        SCHANNEL_CR(3) = 0;
        REG_SOUNDCNT = static_cast<std::uint16_t>((REG_SOUNDCNT & 0x7F) | 1 << 15);
        // (The blocks the plain way: 65536 bus ticks.)
        TIMER_CR(0) = 0;
        TIMER_DATA(0) = 0;
        TIMER_CR(0) = TIMER_ENABLE | TIMER_DIV_1 | TIMER_IRQ_REQ;
        link.blockTicks = 0x10000;
        on = false;
        leaveCriticalSection(ime);
    }

    bool pipelineRateChanges(std::uint8_t newQuality)
    {
        const Rate want = newQuality == kAudioQualityHigh && isDSiMode() ? kHigh : kStandard;
        return want.hz != rate.hz;
    }

    void pipelineRun(AudioLink &link, std::uint32_t blocks)
    {
        if (!on)
            return;
        // Held up for longer than the rings are ahead: what is in them is
        // out of date -- emptied, and on from where the channels are now.
        if (blocks > static_cast<std::uint32_t>(kLead / kBlock - 1))
        {
            for (int i = 0; i < kRing; ++i)
                link.outLeft[i] = link.outRight[i] = 0;
            block += blocks - 1;
            blocks = 1;
        }
        const std::uint32_t written = link.musicWrite;
        for (std::uint32_t n = 0; n < blocks; ++n, ++block)
        {
            const int from = static_cast<int>((block * kBlock) & (kRing - 1));
            // The DSi's DSP's rooms (Enhanced): what the console played, in
            // one channel, a quarter of the rate (47605 Hz then: 11901), sent
            // on for its reverb -- the speakers have it as it is, and the
            // DSP's echo of it, mixed by the DSi.
            if (effects == kAudioEffectsEnhanced)
            {
                std::uint32_t at = link.sendWrite;
                for (int i = 0; i < kBlock; i += 4)
                {
                    const std::int32_t sum = capLeft[from + i] + capLeft[from + i + 1] + capLeft[from + i + 2] +
                                             capLeft[from + i + 3] + capRight[from + i] + capRight[from + i + 1] +
                                             capRight[from + i + 2] + capRight[from + i + 3];
                    link.send[at++ & (AudioLink::kSendRing - 1)] = static_cast<std::int16_t>(sum >> 3);
                }
                link.sendWrite = at;
            }
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
            // (With the DSP's rooms the DSi gives the ordinary sound three
            // quarters of the speakers, not all of them: made up here.)
            if (effects == kAudioEffectsEnhanced)
            {
                for (int i = 0; i < kBlock; ++i)
                {
                    mixLeft[i] = (mixLeft[i] * 341) >> 8;
                    mixRight[i] = (mixRight[i] * 341) >> 8;
                }
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

    void pipelineMode(std::uint8_t newEffects, std::uint8_t newOutput, std::uint8_t newQuality)
    {
        quality = newQuality;
        // (The room going off and on again: what it had, emptied.)
        if (newEffects == kAudioEffectsClassic && effects != kAudioEffectsClassic && roomInit)
        {
            const int was = room.room();
            room.init(rate.hz, rate.factor);
            room.setRoom(was);
        }
        effects = newEffects <= kAudioEffectsEnhanced ? newEffects : kAudioEffectsOff;
        output = newOutput < 3 ? newOutput : kAudioOutputSpeakers;
    }

    void pipelineRoom(int which)
    {
        if (!roomInit)
        {
            room.init(rate.hz, rate.factor);
            roomInit = true;
        }
        room.setRoom(which);
    }

    void musicOpen(AudioLink &link, const AudioCommand &command)
    {
        link.musicRead = 0;
        link.musicDry = 0;
        link.musicOver = 0;
        music.open(reinterpret_cast<const std::uint16_t *>(command.a), command.b & 0xFFFF, command.b >> 16,
                   static_cast<std::uint32_t>(rate.hz), command.c, static_cast<int>(command.d));
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
