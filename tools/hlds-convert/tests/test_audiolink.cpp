// What the two processors say about sound (shared/HlAudioLink.hpp): commands
// come out in the order they went in, a full ring refuses, the indices wrap;
// ends are seen once each, and a reader too long away is told how many it
// missed; a Play's fields come back as they went; the channel timer is the
// hardware's.

#include <cstdio>
#include <cstring>

#include "HlAudioLink.hpp"
#include "TestBspBuilder.hpp"

using namespace hldata;
using namespace hldstest;

int main()
{
    static AudioLink link;
    std::memset(&link, 0, sizeof(link));

    // In order, and a full ring refuses.
    {
        int pushed = 0;
        AudioCommand c;
        c.op = AudioOp::Set;
        while (true)
        {
            c.token = static_cast<std::uint16_t>(pushed);
            if (!audioPush(link, c))
                break;
            ++pushed;
        }
        check(pushed == static_cast<int>(AudioLink::kCommands), "the ring takes as many as it holds, then refuses");
        AudioCommand out;
        bool ordered = true;
        for (int i = 0; i < pushed; ++i)
            ordered = ordered && audioPop(link, out) && out.token == i;
        check(ordered && !audioPop(link, out), "they come out in order, and then there are none");
    }

    // Across the indices' wrap.
    {
        link.commandWrite = 0xFFFFFFF0u;
        link.commandRead = 0xFFFFFFF0u;
        bool ok = true;
        for (int i = 0; i < 1000 && ok; ++i)
        {
            AudioCommand c;
            c.op = AudioOp::Room;
            c.a = static_cast<std::uint32_t>(i);
            AudioCommand out;
            ok = audioPush(link, c) && audioPop(link, out) && out.op == AudioOp::Room && out.a == static_cast<std::uint32_t>(i);
        }
        check(ok, "a thousand round the wrap of the indices");
    }

    // Ends: each seen once; a reader too long away told what it missed.
    {
        std::uint32_t read = link.endWrite;
        for (int i = 0; i < 5; ++i)
            audioEnd(link, static_cast<std::uint8_t>(i), static_cast<std::uint16_t>(100 + i), i == 2);
        int seen = 0;
        bool right = true;
        audioEnds(link, read, [&](const AudioEnd &end) {
            right = right && end.voice == seen && end.token == 100 + seen && (end.stopped != 0) == (seen == 2);
            ++seen;
        });
        check(seen == 5 && right, "five ends, each seen once, as they were");
        audioEnds(link, read, [&](const AudioEnd &) { ++seen; });
        check(seen == 5, "... and not again");
        for (int i = 0; i < 40; ++i)
            audioEnd(link, 1, static_cast<std::uint16_t>(i), false);
        std::uint32_t lost = 0;
        int after = 0, first = -1;
        audioEnds(
            link, read,
            [&](const AudioEnd &end) {
                if (first < 0)
                    first = end.token;
                ++after;
            },
            &lost);
        check(lost == 8 && after == 32 && first == 8, "too long away: the last 32 seen, 8 counted lost");
    }

    // A Play's fields.
    {
        AudioPlay p;
        p.address = 0x02345678;
        p.words = 4321;
        p.loopWord = 17;
        p.rate = 22050;
        p.volume = 99;
        p.pan = 3;
        p.pitch = 1300;
        p.flags = kAudioPlayUi | kAudioPlayHere;
        const AudioPlay back = audioPlayOf(audioPlay(5, 777, p));
        check(back.address == p.address && back.words == p.words && back.loopWord == p.loopWord &&
                  back.rate == p.rate && back.volume == p.volume && back.pan == p.pan && back.pitch == p.pitch &&
                  back.flags == p.flags,
              "a Play's fields come back as they went");
    }

    // The channel timer: 32728 Hz is 512 ticks (the hardware's own rate).
    {
        check(audioTimer(32728, 1024) == static_cast<std::uint16_t>(0x10000 - 512), "32728 Hz: 512 ticks");
        check(audioTimer(11025, 1024) == static_cast<std::uint16_t>(0x10000 - 1520), "11025 Hz: 1520 ticks");
        check(audioTimer(11025, 2048) == static_cast<std::uint16_t>(0x10000 - 760), "... twice as fast: 760");
        check(audioTimer(100, 1024) == 1, "very slow: as slow as the timer goes");
        check(audioTimer(11025, 0) == 0, "no pitch: no timer");
    }

    if (g_failures == 0)
        std::printf("test_audiolink: ok\n");
    return g_failures == 0 ? 0 : 1;
}
