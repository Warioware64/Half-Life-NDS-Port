#pragma once

// What the two processors say to each other about sound (the ARM7's own
// audio core, arm7/source; the ARM9's client, source/audio). One block in
// main RAM, which the ARM9 reaches through its uncached mirror and the ARM7
// as it is (it has no cache):
//
//  * COMMANDS, ARM9 to ARM7: a ring of fixed-size records, written by the
//    ARM9 and taken by the ARM7 at the start of each of its blocks (64
//    samples, 2 ms). No interrupts, nothing on the FIFO that the wifi and
//    the card also use; a full ring refuses (the caller tries again next
//    frame or does without).
//  * STATUS, ARM7 to ARM9: a ring of ENDS -- each sound that finished on its
//    own or was stopped, by its voice and the caller's token, so that the
//    ARM9 knows to the block when a voice is free and a word is spoken --
//    and the core's counters (its load, blocks late, music run dry).
//
// Which voice plays what is the ARM9's choice (HlMixer.hpp's rules); the
// ARM7 plays each sound on a free channel and says, by the caller's token,
// when it ended. (A sound being faded out keeps its token after its voice
// has gone to another, as a maxmod handle did.)
//
// Every field one side writes, the other only reads; indices only grow
// (wrapping at 2^32), so either side sees how far the other has got.
// std-only: the host tests run both sides.

#include <cstdint>

namespace hldata
{
    enum class AudioOp : std::uint8_t
    {
        None,
        /// a: the clip's address (its SoundHeader's data: a DS-hardware
        /// IMA-ADPCM preamble and nibbles); b: its words (low 16) and the
        /// word it loops from (high 16; kAudioNoLoop: it does not);
        /// c: its rate in Hz (low 16), volume 0..127 (bits 16-23), pan
        /// 0..127 (24-31); d: pitch (low 16, 1024 as recorded), flags
        /// (high 16: kAudioPlay*).
        Play,
        /// a: volume (low 8), pan (bits 8-15), which of the three to change
        /// (bits 16-18: kAudioSet*); b: pitch (1024 as recorded).
        Set,
        /// At once. (A fade is the ARM9's: volumes stepped down, then this.)
        Stop,
        /// a: 1 paused, 0 going again. (Voices with kAudioPlayUi play on.)
        Pause,
        /// a: the room (HlRoomPresets.hpp, 0..28).
        Room,
        /// a: effects (kAudioEffects*), output (bits 8-15, kAudioOutput*),
        /// quality (bits 16-23, kAudioQuality*).
        Mode,
        /// a: the master volume, 0..127.
        Master,
        /// a: the ring's address; b: its bytes; c: rate in Hz (low 16),
        /// channels (high 16). Blocks of HlDsp's stereo IMA (as the DSi's
        /// music files are), the ARM9 feeding the ring (`musicWrite`).
        MusicOpen,
        MusicStop,
        /// Every sound stopped (a map let go of).
        StopAll,
        Count
    };

    inline constexpr std::uint16_t kAudioNoLoop = 0xFFFF;
    inline constexpr std::uint32_t kAudioSetVolume = 1, kAudioSetPan = 2, kAudioSetPitch = 4;
    inline constexpr std::uint16_t kAudioPlayUi = 1;  ///< plays on in the pause menu
    inline constexpr std::uint16_t kAudioPlayHere = 2; ///< not placed in the room (the player's own)

    inline constexpr std::uint8_t kAudioEffectsOff = 0, kAudioEffectsClassic = 1, kAudioEffectsEnhanced = 2;
    inline constexpr std::uint8_t kAudioOutputSpeakers = 0, kAudioOutputHeadphones = 1, kAudioOutputMono = 2;
    inline constexpr std::uint8_t kAudioQualityStandard = 0, kAudioQualityHigh = 1;

    struct AudioCommand
    {
        AudioOp op = AudioOp::None;
        std::uint8_t voice = 0;
        /// The caller's name for the sound (not 0): Set and Stop find it by
        /// this, and its END gives it back. (The ARM7 picks its channel.)
        std::uint16_t token = 0;
        std::uint32_t a = 0, b = 0, c = 0, d = 0;
    };
    static_assert(sizeof(AudioCommand) == 20);

    /// A sound that ended: on its own, or stopped.
    struct AudioEnd
    {
        std::uint8_t voice; ///< the channel it was on
        std::uint8_t stopped; ///< 1: by a Stop (or another Play on its voice)
        std::uint16_t token;
    };

    struct AudioLink
    {
        static constexpr std::uint32_t kCommands = 64; ///< a power of two
        static constexpr std::uint32_t kEnds = 32;     ///< a power of two
        static constexpr int kVoices = 16;

        // ARM9 writes:
        volatile std::uint32_t commandWrite;
        volatile std::uint32_t musicWrite; ///< bytes put into the music ring, all told
        AudioCommand commands[kCommands];
        // ARM7 writes:
        volatile std::uint32_t commandRead;
        volatile std::uint32_t endWrite;
        AudioEnd ends[kEnds];
        volatile std::uint16_t voiceToken[kVoices]; ///< what each channel is playing now (0: nothing)
        volatile std::uint32_t blocks;    ///< 64-sample blocks so far
        volatile std::uint32_t late;      ///< blocks done after their time
        volatile std::uint32_t cpuAvg;    ///< ARM7 ticks per block, averaged
        volatile std::uint32_t cpuMost;   ///< ... the most since last read
        volatile std::uint32_t musicRead; ///< bytes of the music ring played
        volatile std::uint32_t musicDry;  ///< blocks the music ran out
    };

    // ---- the ARM9's side

    /// A command into the ring; false if it is full.
    inline bool audioPush(AudioLink &link, const AudioCommand &command)
    {
        const std::uint32_t write = link.commandWrite;
        if (write - link.commandRead >= AudioLink::kCommands)
            return false;
        link.commands[write & (AudioLink::kCommands - 1)] = command;
        // (The record first, then the index that hands it over.)
        asm volatile("" ::: "memory");
        link.commandWrite = write + 1;
        return true;
    }

    /// The ends not yet seen, oldest first, each given to `seen`; `read` is
    /// the caller's own place in the ring. Ends the ring no longer holds (the
    /// caller was too long away) are counted in `lost`.
    template <typename Fn>
    inline void audioEnds(const AudioLink &link, std::uint32_t &read, Fn &&seen, std::uint32_t *lost = nullptr)
    {
        const std::uint32_t write = link.endWrite;
        if (write - read > AudioLink::kEnds)
        {
            if (lost != nullptr)
                *lost += write - read - AudioLink::kEnds;
            read = write - AudioLink::kEnds;
        }
        for (; read != write; ++read)
        {
            const AudioEnd end = link.ends[read & (AudioLink::kEnds - 1)];
            seen(end);
        }
    }

    // ---- the ARM7's side

    /// The next command, if there is one.
    inline bool audioPop(AudioLink &link, AudioCommand &command)
    {
        const std::uint32_t read = link.commandRead;
        if (read == link.commandWrite)
            return false;
        asm volatile("" ::: "memory");
        command = link.commands[read & (AudioLink::kCommands - 1)];
        asm volatile("" ::: "memory");
        link.commandRead = read + 1;
        return true;
    }

    /// A voice's sound ended.
    inline void audioEnd(AudioLink &link, std::uint8_t voice, std::uint16_t token, bool stopped)
    {
        const std::uint32_t write = link.endWrite;
        link.ends[write & (AudioLink::kEnds - 1)] =
            AudioEnd{voice, static_cast<std::uint8_t>(stopped ? 1 : 0), token};
        asm volatile("" ::: "memory");
        link.endWrite = write + 1;
    }

    // ---- the fields of a Play, packed and unpacked

    struct AudioPlay
    {
        std::uint32_t address = 0;
        std::uint16_t words = 0;
        std::uint16_t loopWord = kAudioNoLoop;
        std::uint16_t rate = 0;
        std::uint8_t volume = 0, pan = 64;
        std::uint16_t pitch = 1024;
        std::uint16_t flags = 0;
    };

    inline AudioCommand audioPlay(std::uint8_t voice, std::uint16_t token, const AudioPlay &p)
    {
        AudioCommand c;
        c.op = AudioOp::Play;
        c.voice = voice;
        c.token = token;
        c.a = p.address;
        c.b = static_cast<std::uint32_t>(p.words) | static_cast<std::uint32_t>(p.loopWord) << 16;
        c.c = static_cast<std::uint32_t>(p.rate) | static_cast<std::uint32_t>(p.volume & 127) << 16 |
              static_cast<std::uint32_t>(p.pan & 127) << 24;
        c.d = static_cast<std::uint32_t>(p.pitch) | static_cast<std::uint32_t>(p.flags) << 16;
        return c;
    }

    inline AudioPlay audioPlayOf(const AudioCommand &c)
    {
        AudioPlay p;
        p.address = c.a;
        p.words = static_cast<std::uint16_t>(c.b);
        p.loopWord = static_cast<std::uint16_t>(c.b >> 16);
        p.rate = static_cast<std::uint16_t>(c.c);
        p.volume = static_cast<std::uint8_t>((c.c >> 16) & 127);
        p.pan = static_cast<std::uint8_t>((c.c >> 24) & 127);
        p.pitch = static_cast<std::uint16_t>(c.d);
        p.flags = static_cast<std::uint16_t>(c.d >> 16);
        return p;
    }

    /// The channel timer for a clip of `rate` Hz played at `pitch` (1024 as
    /// recorded): the hardware counts 16.756991 MHz down to each sample.
    /// (At most 0xFFFF... ticks a sample, at least 32.)
    inline std::uint16_t audioTimer(std::uint32_t rate, std::uint32_t pitch)
    {
        const std::uint32_t hz = (rate * pitch) >> 10;
        if (hz == 0)
            return 0;
        std::uint32_t ticks = (16756991u + hz / 2) / hz;
        ticks = ticks < 32 ? 32 : (ticks > 0xFFFF ? 0xFFFF : ticks);
        return static_cast<std::uint16_t>(0x10000u - ticks);
    }
} // namespace hldata
