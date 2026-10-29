#pragma once

// A read from the card in pieces. On a DSi the card is the ARM7's to read:
// while it reads, the ARM7's sound work waits (arm7/source/Pipeline.arm.cpp),
// and a read of tens of kilobytes at once is long enough to be heard as a
// crackle in the music. In pieces of 8 KB, it is never held up for longer
// than its output ring is ahead of the speakers.

#include <cstddef>
#include <cstdio>

namespace hlrt
{
    inline constexpr std::size_t kCardPiece = 8 * 1024;

    /// fread's, `bytes` of them, in pieces: how many were read.
    inline std::size_t cardRead(void *to, std::size_t bytes, std::FILE *file)
    {
        auto *at = static_cast<unsigned char *>(to);
        std::size_t done = 0;
        while (done < bytes)
        {
            const std::size_t want = bytes - done < kCardPiece ? bytes - done : kCardPiece;
            const std::size_t got = std::fread(at + done, 1, want, file);
            done += got;
            if (got != want)
                break;
        }
        return done;
    }
} // namespace hlrt
