#include "audio/Arm7Audio.hpp"

#include <cstring>
#include <malloc.h>

#include <nds.h>

namespace hlrt
{
    Arm7Audio arm7Audio;

    using namespace hldata;

    bool Arm7Audio::start()
    {
        if (link_ != nullptr)
            return true;
        // (On cache lines of its own; reached only through the uncached
        // mirror from here on, so that what the ARM7 writes is what is read.)
        void *block = memalign(32, (sizeof(AudioLink) + 31) & ~31u);
        if (block == nullptr)
            return false;
        std::memset(block, 0, sizeof(AudioLink));
        DC_FlushRange(block, sizeof(AudioLink));
        AudioLink *link = static_cast<AudioLink *>(memUncached(block));
        fifoSendValue32(FIFO_USER_01, reinterpret_cast<std::uint32_t>(block));
        // The ARM7 answers by counting its blocks (2 ms each).
        for (int frame = 0; frame < 30; ++frame)
        {
            if (link->blocks != 0)
            {
                link_ = link;
                return true;
            }
            swiWaitForVBlank();
        }
        return false;
    }

    void Arm7Audio::push(const AudioCommand &command)
    {
        if (link_ == nullptr)
            return;
        // (Full: the ARM7 takes them every 2 ms; a frame's worth more than
        // fits only in a storm, and then the moment is waited out.)
        for (int spin = 0; spin < 20000 && !audioPush(*link_, command); ++spin)
            ;
    }

    std::uint16_t Arm7Audio::play(const void *data, std::uint32_t words, std::uint32_t loopWord, std::uint16_t rate,
                                  int volume, int pan, int pitch, bool menu)
    {
        if (link_ == nullptr || data == nullptr)
            return 0;
        int slot = -1;
        for (int k = 0; k < kChannels && slot < 0; ++k)
            slot = live_[k] == 0 ? k : -1;
        if (slot < 0)
            return 0;
        const std::uint16_t token = next_;
        next_ = static_cast<std::uint16_t>(next_ + 1 == 0 ? 1 : next_ + 1);
        AudioPlay p;
        p.address = reinterpret_cast<std::uint32_t>(data);
        p.words = static_cast<std::uint16_t>(words > 0xFFFF ? 0xFFFF : words);
        p.loopWord = loopWord < words && loopWord < kAudioNoLoop ? static_cast<std::uint16_t>(loopWord) : kAudioNoLoop;
        p.rate = rate;
        p.volume = static_cast<std::uint8_t>((volume < 0 ? 0 : (volume > 255 ? 255 : volume)) >> 1);
        p.pan = static_cast<std::uint8_t>((pan < 0 ? 0 : (pan > 255 ? 255 : pan)) >> 1);
        p.pitch = static_cast<std::uint16_t>(pitch < 1 ? 1 : (pitch > 0xFFFF ? 0xFFFF : pitch));
        p.flags = menu ? kAudioPlayUi : 0;
        live_[slot] = token;
        push(audioPlay(0, token, p));
        return token;
    }

    bool Arm7Audio::playing(std::uint16_t token) const
    {
        for (std::uint16_t live : live_)
        {
            if (live == token && token != 0)
                return true;
        }
        return false;
    }

    void Arm7Audio::stop(std::uint16_t token)
    {
        if (!playing(token))
            return;
        for (std::uint16_t &live : live_)
        {
            if (live == token)
                live = 0;
        }
        AudioCommand c;
        c.op = AudioOp::Stop;
        c.token = token;
        push(c);
    }

    void Arm7Audio::setVolume(std::uint16_t token, int volume)
    {
        if (!playing(token))
            return;
        AudioCommand c;
        c.op = AudioOp::Set;
        c.token = token;
        c.a = static_cast<std::uint32_t>((volume < 0 ? 0 : (volume > 255 ? 255 : volume)) >> 1) | kAudioSetVolume << 16;
        push(c);
    }

    void Arm7Audio::setPanning(std::uint16_t token, int pan)
    {
        if (!playing(token))
            return;
        AudioCommand c;
        c.op = AudioOp::Set;
        c.token = token;
        c.a = static_cast<std::uint32_t>((pan < 0 ? 0 : (pan > 255 ? 255 : pan)) >> 1) << 8 | kAudioSetPan << 16;
        push(c);
    }

    void Arm7Audio::setRate(std::uint16_t token, int pitch)
    {
        if (!playing(token))
            return;
        AudioCommand c;
        c.op = AudioOp::Set;
        c.token = token;
        c.a = kAudioSetPitch << 16;
        c.b = static_cast<std::uint32_t>(pitch < 1 ? 1 : (pitch > 0xFFFF ? 0xFFFF : pitch));
        push(c);
    }

    void Arm7Audio::stopAll()
    {
        for (std::uint16_t &live : live_)
            live = 0;
        AudioCommand c;
        c.op = AudioOp::StopAll;
        push(c);
    }

    void Arm7Audio::frame()
    {
        if (link_ == nullptr)
            return;
        audioEnds(*link_, endRead_, [&](const AudioEnd &end) {
            for (std::uint16_t &live : live_)
            {
                if (live == end.token)
                    live = 0;
            }
        });
    }

    void Arm7Audio::musicOpen(const void *ring, std::uint32_t bytes, std::uint32_t rate)
    {
        if (link_ == nullptr)
            return;
        link_->musicWrite = 0;
        AudioCommand c;
        c.op = AudioOp::MusicOpen;
        c.a = reinterpret_cast<std::uint32_t>(ring);
        c.b = bytes;
        c.c = (rate & 0xFFFF) | 1u << 16;
        push(c);
    }

    void Arm7Audio::musicStop()
    {
        AudioCommand c;
        c.op = AudioOp::MusicStop;
        push(c);
    }

    std::uint32_t Arm7Audio::musicRead() const
    {
        return link_ != nullptr ? link_->musicRead : 0;
    }
    std::uint32_t Arm7Audio::musicWritten() const
    {
        return link_ != nullptr ? link_->musicWrite : 0;
    }
    void Arm7Audio::musicWrote(std::uint32_t bytes)
    {
        if (link_ != nullptr)
            link_->musicWrite = link_->musicWrite + bytes;
    }

    std::uint32_t Arm7Audio::late() const
    {
        return link_ != nullptr ? link_->late : 0;
    }
    std::uint32_t Arm7Audio::loadPercent() const
    {
        // (Of the 65536 bus ticks of a block.)
        return link_ != nullptr ? (link_->cpuAvg * 100) >> 16 : 0;
    }
    std::uint32_t Arm7Audio::musicDry() const
    {
        return link_ != nullptr ? link_->musicDry : 0;
    }
} // namespace hlrt
