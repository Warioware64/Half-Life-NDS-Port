// The game's sound on the ARM7: what the ARM9 asks for through the shared
// block (shared/HlAudioLink.hpp), done once a block -- 64 samples of the
// hardware's own rate, 2 ms, a timer interrupt locked to the sound clock
// (tools/hwtest/routing: never late on a DS or a DSi over minutes).
//
// Each sound is played on a hardware channel of its own, which decodes the
// clip's IMA-ADPCM, keeps its pitch and pans it; this program finds the
// channel, starts it, changes it, stops it, and says when it has ended -- to
// the block, by the token the ARM9 gave it -- so the ARM9 knows when a voice
// is free and a word has been said, and never guesses by the clock.
//
// The channels: 1 is not ours (its clock is what the DSi's capture for the
// DSP's echo keeps time by: libnds's sound messages, from the ARM9); 3 plays
// the music, a ring of 8-bit samples the ARM9 keeps filled; the other
// fourteen are the sounds'.

#include <nds.h>

#include "Audio.hpp"
#include "HlAudioLink.hpp"

using namespace hldata;

namespace hlaudio
{
    namespace
    {
        constexpr int kMusicChannel = 3;
        constexpr int kChannelCount = 14;
        constexpr std::uint8_t kChannels[kChannelCount] = {0, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        /// Sound-clock ticks in a block (the block timer counts the bus
        /// clock, twice as fast: 65536 of those).
        constexpr std::uint32_t kBlockSoundTicks = 32768;

        struct Channel
        {
            std::uint16_t token = 0; ///< 0: free
            bool ui = false;
            std::uint8_t volume = 0, pan = 64;
            std::uint16_t rate = 0, pitch = 1024;
        };

        AudioLink *volatile link = nullptr;
        Channel channels[kChannelCount];
        volatile std::uint32_t pending = 0; ///< blocks the timer has counted, not yet done
        bool paused = false;

        // The music.
        bool musicOn = false;
        std::uint32_t musicRingBytes = 0;
        std::uint32_t musicAcc = 0;   ///< bytes played, in 65536ths
        std::uint32_t musicPerBlock = 0;
        bool musicMuted = false;

        void linkHandler(u32 value, void *)
        {
            link = reinterpret_cast<AudioLink *>(value);
        }

        void blockTick()
        {
            pending = pending + 1;
        }

        /// The bus clock as one 32-bit count: timer 1 counting, timer 2 its
        /// overflows.
        std::uint32_t stamp()
        {
            std::uint16_t high = TIMER_DATA(2);
            std::uint16_t low = TIMER_DATA(1);
            const std::uint16_t again = TIMER_DATA(2);
            if (again != high)
            {
                high = again;
                low = TIMER_DATA(1);
            }
            return static_cast<std::uint32_t>(high) << 16 | low;
        }

        int channelOf(std::uint16_t token)
        {
            for (int c = 0; c < kChannelCount; ++c)
            {
                if (channels[c].token == token && token != 0)
                    return c;
            }
            return -1;
        }

        void release(int c, bool stopped)
        {
            const int n = kChannels[c];
            if (stopped)
                SCHANNEL_CR(n) = 0;
            audioEnd(*link, static_cast<std::uint8_t>(n), channels[c].token, stopped);
            link->voiceToken[n] = 0;
            channels[c] = Channel();
        }

        /// What the channel's timer is now: its pitch, or as slow as the
        /// hardware goes while the game stands still (the menu's own play on).
        std::uint16_t timerOf(const Channel &ch)
        {
            return paused && !ch.ui ? 0 : audioTimer(ch.rate, ch.pitch);
        }
        std::uint8_t volumeOf(const Channel &ch)
        {
            return paused && !ch.ui ? 0 : ch.volume;
        }

        void play(const AudioCommand &command)
        {
            const AudioPlay p = audioPlayOf(command);
            int c = -1;
            for (int k = 0; k < kChannelCount && c < 0; ++k)
                c = channels[k].token == 0 ? k : -1;
            if (c < 0)
            {
                // (The ARM9 never asks for more than there are; if it does,
                // the first that is not the menu's gives way.)
                for (int k = 0; k < kChannelCount && c < 0; ++k)
                    c = channels[k].ui ? -1 : k;
                if (c < 0)
                    c = 0;
                release(c, true);
            }
            if (p.words < 4 || p.address == 0)
            {
                audioEnd(*link, static_cast<std::uint8_t>(kChannels[c]), command.token, false);
                return;
            }
            Channel &ch = channels[c];
            ch.token = command.token;
            ch.ui = (p.flags & kAudioPlayUi) != 0;
            ch.volume = p.volume;
            ch.pan = p.pan;
            ch.rate = p.rate;
            ch.pitch = p.pitch;
            const int n = kChannels[c];
            const bool loops = p.loopWord != kAudioNoLoop && p.loopWord < p.words;
            SCHANNEL_CR(n) = 0;
            SCHANNEL_SOURCE(n) = p.address;
            SCHANNEL_TIMER(n) = timerOf(ch);
            SCHANNEL_REPEAT_POINT(n) = loops ? p.loopWord : 0;
            SCHANNEL_LENGTH(n) = loops ? p.words - p.loopWord : p.words;
            SCHANNEL_CR(n) = static_cast<std::uint32_t>(volumeOf(ch)) | static_cast<std::uint32_t>(ch.pan) << 16 |
                             (loops ? 1u : 2u) << 27 | 2u << 29 | 1u << 31;
            link->voiceToken[n] = command.token;
        }

        void set(const AudioCommand &command)
        {
            const int c = channelOf(command.token);
            if (c < 0)
                return;
            Channel &ch = channels[c];
            const int n = kChannels[c];
            const std::uint32_t what = command.a >> 16;
            if (what & kAudioSetVolume)
            {
                ch.volume = static_cast<std::uint8_t>(command.a & 127);
                SCHANNEL_VOL(n) = volumeOf(ch);
            }
            if (what & kAudioSetPan)
            {
                ch.pan = static_cast<std::uint8_t>((command.a >> 8) & 127);
                SCHANNEL_PAN(n) = ch.pan;
            }
            if (what & kAudioSetPitch)
            {
                ch.pitch = static_cast<std::uint16_t>(command.b);
                SCHANNEL_TIMER(n) = timerOf(ch);
            }
        }

        void pause(bool on)
        {
            paused = on;
            for (int c = 0; c < kChannelCount; ++c)
            {
                if (channels[c].token == 0 || channels[c].ui)
                    continue;
                const int n = kChannels[c];
                SCHANNEL_VOL(n) = volumeOf(channels[c]);
                SCHANNEL_TIMER(n) = timerOf(channels[c]);
            }
        }

        void musicOpen(const AudioCommand &command)
        {
            const std::uint32_t rate = command.c & 0xFFFF;
            const std::uint16_t timer = audioTimer(rate, 1024);
            SCHANNEL_CR(kMusicChannel) = 0;
            if (timer == 0 || command.b < 64)
            {
                musicOn = false;
                return;
            }
            musicRingBytes = command.b;
            // Bytes played in a block, in 65536ths: the block's sound ticks
            // over the ticks of one sample.
            const std::uint32_t perSample = 0x10000u - timer;
            musicPerBlock = static_cast<std::uint32_t>((static_cast<std::uint64_t>(kBlockSoundTicks) << 16) / perSample);
            musicAcc = 0;
            link->musicRead = 0;
            musicMuted = true; // (until there is something in the ring)
            SCHANNEL_SOURCE(kMusicChannel) = command.a;
            SCHANNEL_TIMER(kMusicChannel) = timer;
            SCHANNEL_REPEAT_POINT(kMusicChannel) = 0;
            SCHANNEL_LENGTH(kMusicChannel) = musicRingBytes / 4;
            SCHANNEL_CR(kMusicChannel) = 0u | 64u << 16 | 1u << 27 | 0u << 29 | 1u << 31;
            musicOn = true;
        }

        void musicBlock(std::uint32_t blocks)
        {
            if (!musicOn)
                return;
            musicAcc += musicPerBlock * blocks;
            link->musicRead = link->musicRead + (musicAcc >> 16);
            musicAcc &= 0xFFFF;
            // Run dry (the ARM9 behind): silent rather than going round
            // what it already played.
            const std::int32_t ahead = static_cast<std::int32_t>(link->musicWrite - link->musicRead);
            const bool dry = ahead <= 0;
            if (dry)
                link->musicDry = link->musicDry + blocks;
            if (dry != musicMuted)
            {
                musicMuted = dry;
                SCHANNEL_VOL(kMusicChannel) = dry ? 0 : 127;
            }
        }

        void run(std::uint32_t blocks)
        {
            AudioLink &l = *link;
            AudioCommand command;
            while (audioPop(l, command))
            {
                switch (command.op)
                {
                case AudioOp::Play:
                    play(command);
                    break;
                case AudioOp::Set:
                    set(command);
                    break;
                case AudioOp::Stop:
                {
                    const int c = channelOf(command.token);
                    if (c >= 0)
                        release(c, true);
                    break;
                }
                case AudioOp::Pause:
                    pause(command.a != 0);
                    break;
                case AudioOp::Master:
                    REG_SOUNDCNT = static_cast<std::uint16_t>((REG_SOUNDCNT & ~0x7F) | (command.a & 0x7F));
                    break;
                case AudioOp::MusicOpen:
                    musicOpen(command);
                    break;
                case AudioOp::MusicStop:
                    SCHANNEL_CR(kMusicChannel) = 0;
                    musicOn = false;
                    break;
                case AudioOp::StopAll:
                    for (int c = 0; c < kChannelCount; ++c)
                    {
                        if (channels[c].token != 0)
                            release(c, true);
                    }
                    break;
                default:
                    break; // (rooms and modes: not yet)
                }
            }
            // The sounds that ended on their own.
            for (int c = 0; c < kChannelCount; ++c)
            {
                if (channels[c].token != 0 && !(SCHANNEL_CR(kChannels[c]) & (1u << 31)))
                    release(c, false);
            }
            musicBlock(blocks);
        }
    } // namespace

    void install()
    {
        fifoSetValue32Handler(FIFO_USER_01, linkHandler, nullptr);
        // The bus clock, for the load figures.
        TIMER_CR(1) = 0;
        TIMER_CR(2) = 0;
        TIMER_DATA(1) = 0;
        TIMER_DATA(2) = 0;
        TIMER_CR(2) = TIMER_ENABLE | TIMER_CASCADE;
        TIMER_CR(1) = TIMER_ENABLE | TIMER_DIV_1;
        // A block every 65536 bus ticks: 64 samples at 32728 Hz.
        irqSet(IRQ_TIMER0, blockTick);
        irqEnable(IRQ_TIMER0);
        TIMER_DATA(0) = 0;
        TIMER_CR(0) = TIMER_ENABLE | TIMER_DIV_1 | TIMER_IRQ_REQ;
    }

    void waitAndRun()
    {
        while (pending == 0)
            swiIntrWait(1, IRQ_TIMER0);
        const int ime = enterCriticalSection();
        const std::uint32_t blocks = pending;
        pending = 0;
        leaveCriticalSection(ime);
        AudioLink *l = link;
        if (l == nullptr)
            return;
        const std::uint32_t began = stamp();
        run(blocks);
        const std::uint32_t took = stamp() - began;
        l->blocks = l->blocks + blocks;
        if (blocks > 1)
            l->late = l->late + (blocks - 1);
        l->cpuAvg = l->cpuAvg - (l->cpuAvg >> 4) + (took >> 4);
        if (took > l->cpuMost)
            l->cpuMost = took;
    }
} // namespace hlaudio
