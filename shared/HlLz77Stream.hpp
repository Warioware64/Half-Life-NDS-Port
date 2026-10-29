#pragma once

// The DS BIOS's LZ77 ("LZ10", tools/hlds-convert's Lz77.hpp), undone a piece
// at a time: for what a console is sent over the air, which arrives a few
// hundred bytes at a time and goes straight to where it is to stay -- the
// level's block, a model's, a sound's -- with nothing of it kept on the way.
//
// The stream is a word (0x10 and the size it comes back to), then groups of
// eight items under a flag byte, most significant bit first: a clear bit a
// byte as it is, a set bit two bytes naming a run of 3 to 18 bytes that
// begins 1 to 4096 bytes back. What it comes back to may be taken in pieces
// of any size, anywhere (a reader's own buffer, a block a little at a time):
// the 4096 bytes a run may reach back over are kept here, in a ring, and
// nothing else is.

#include <cstddef>
#include <cstdint>

namespace hldata
{
    class Lz77Stream
    {
    public:
        /// A new stream, which must come back to `expect` bytes.
        void begin(std::uint32_t expect)
        {
            *this = Lz77Stream();
            expect_ = expect;
        }

        /// Undo what can be of `in` (`inLeft` bytes, both moved on past what
        /// was taken) into `out`, at most `room` bytes. What was written.
        /// Stops when `out` is full, `in` is spent, or the stream is done or
        /// wrong (failed()).
        std::size_t pump(const std::uint8_t *&in, std::size_t &inLeft, std::uint8_t *out, std::size_t room)
        {
            std::size_t wrote = 0;
            while (wrote < room && !failed_)
            {
                // A run under way, first.
                if (runLeft_ > 0)
                {
                    const std::uint8_t byte = history_[(written_ - back_) & kMask];
                    put(byte, out, wrote);
                    --runLeft_;
                    continue;
                }
                if (headerAt_ == 4 && written_ == expect_)
                    break;
                if (inLeft == 0)
                    break;
                const std::uint8_t byte = *in++;
                --inLeft;
                if (headerAt_ < 4)
                {
                    if (headerAt_ == 0 && byte != 0x10)
                        failed_ = true;
                    else if (headerAt_ > 0)
                        size_ |= static_cast<std::uint32_t>(byte) << (8 * (headerAt_ - 1));
                    if (++headerAt_ == 4 && size_ != expect_)
                        failed_ = true;
                    continue;
                }
                if (itemsLeft_ == 0)
                {
                    flags_ = byte;
                    itemsLeft_ = 8;
                    continue;
                }
                if (!(flags_ & 0x80))
                {
                    put(byte, out, wrote);
                    nextItem();
                    continue;
                }
                if (!haveFirst_)
                {
                    first_ = byte;
                    haveFirst_ = true;
                    continue;
                }
                haveFirst_ = false;
                const std::uint32_t length = (first_ >> 4) + 3u;
                back_ = (((first_ & 0x0Fu) << 8) | byte) + 1u;
                if (back_ > written_ || length > expect_ - written_)
                {
                    failed_ = true;
                    break;
                }
                runLeft_ = length;
                nextItem();
            }
            return wrote;
        }

        /// The whole of it has come back (any padding after it is the
        /// caller's to pass over).
        bool done() const { return !failed_ && headerAt_ == 4 && written_ == expect_ && runLeft_ == 0; }
        bool failed() const { return failed_; }
        std::uint32_t written() const { return written_; }

    private:
        static constexpr std::uint32_t kWindow = 4096;
        static constexpr std::uint32_t kMask = kWindow - 1;

        void put(std::uint8_t byte, std::uint8_t *out, std::size_t &wrote)
        {
            history_[written_ & kMask] = byte;
            ++written_;
            out[wrote++] = byte;
        }

        void nextItem()
        {
            flags_ = static_cast<std::uint8_t>(flags_ << 1);
            --itemsLeft_;
        }

        std::uint8_t history_[kWindow];
        std::uint32_t expect_ = 0;
        std::uint32_t size_ = 0;
        std::uint32_t written_ = 0;
        std::uint32_t back_ = 0;
        std::uint32_t runLeft_ = 0;
        int headerAt_ = 0;
        int itemsLeft_ = 0;
        std::uint8_t flags_ = 0;
        std::uint8_t first_ = 0;
        bool haveFirst_ = false;
        bool failed_ = false;
    };
} // namespace hldata
