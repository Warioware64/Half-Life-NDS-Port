#pragma once

// The music in stereo, as the ARM7 plays it (arm7/source/Pipeline.arm.cpp):
// the track's own blocks (HlSound.hpp: StereoHeader, kMusicBlockWords) as
// they are in the file, in a ring the ARM9 keeps filled, decoded here and
// drawn out to the output rate in straight lines, as loud as the player has
// the music.
//
// A block is the two decoders' states at its start (left sample, left step,
// right sample, right step) and then its samples, a byte a pair (left in
// the low four bits), two pairs a word. The decoder is the DSP's (the
// multiply kind: (2c + 1) / 8 of the step; shared/dsp/hl_dsp.h), and comes
// out the same, sample for sample -- the host tests say so. Each block takes
// its states afresh, so nothing that goes wrong outlasts one.
//
// Integer only, no division per sample: std-only, for the host tests.

#include <cstdint>

#include "HlSound.hpp"

namespace hldata
{
    class StereoMusic
    {
    public:
        static constexpr std::uint32_t kBlockWords = kMusicBlockWords + 4; ///< as in the file
        static constexpr std::uint32_t kPairsPerBlock = kMusicBlockWords * 2;

        /// A ring of `ringBlocks` blocks at `ring` (the ARM9's), the track
        /// `rate` Hz, `totalPairs` long from where it starts, played at
        /// `outRate`, `volume` 0..256.
        void open(const std::uint16_t *ring, std::uint32_t ringBlocks, std::uint32_t rate, std::uint32_t outRate,
                  std::uint32_t totalPairs, int volume)
        {
            ring_ = ring;
            ringBlocks_ = ringBlocks;
            total_ = totalPairs;
            // (Pairs of the track a sample of the output, in 65536ths: once.)
            step_ = static_cast<std::uint32_t>((static_cast<std::uint64_t>(rate) << 16) / outRate);
            phase_ = 0;
            played_ = 0;
            inBlock_ = kPairsPerBlock; // (the first block is begun at the first pair)
            block_ = 0;
            prev_[0] = prev_[1] = cur_[0] = cur_[1] = 0;
            volume_ = volume;
            on_ = ring != nullptr && ringBlocks != 0 && step_ != 0;
            dry_ = 0;
        }
        void close() { on_ = false; }
        void setVolume(int volume) { volume_ = volume; }
        bool on() const { return on_; }
        /// Pairs of the track played, and samples the ring had none for.
        std::uint32_t played() const { return played_; }
        std::uint32_t dry() const { return dry_; }
        bool over() const { return on_ && played_ >= total_; }

        /// `count` samples of music added to `left` and `right` (each
        /// against the stops); `written`: blocks the ARM9 has put in the
        /// ring, all told.
        void mix(std::int32_t *left, std::int32_t *right, int count, std::uint32_t written)
        {
            if (!on_)
                return;
            for (int i = 0; i < count; ++i)
            {
                phase_ += step_;
                while (phase_ >= 0x10000u)
                {
                    phase_ -= 0x10000u;
                    prev_[0] = cur_[0];
                    prev_[1] = cur_[1];
                    if (!nextPair(written))
                    {
                        cur_[0] = cur_[1] = 0;
                        if (!over())
                            ++dry_;
                    }
                }
                const int f = static_cast<int>(phase_ >> 8); // 0..255
                const int l = prev_[0] + (((cur_[0] - prev_[0]) * f) >> 8);
                const int r = prev_[1] + (((cur_[1] - prev_[1]) * f) >> 8);
                left[i] += (l * volume_) >> 8;
                right[i] += (r * volume_) >> 8;
            }
        }

        static constexpr std::int16_t kSteps[89] = {
            7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
            25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
            88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
            307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
            1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
            3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
            12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

        /// One sample of one decoder out of its four bits (hl_dsp.h's
        /// hlDecode, the same arithmetic).
        static void decode(std::int32_t &value, std::int32_t &index, unsigned code)
        {
            static constexpr std::int8_t kIndexStep[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
            const std::int32_t step = kSteps[index];
            const std::int32_t diff = (step * static_cast<std::int32_t>(((code & 7) << 1) + 1)) >> 3;
            value = (code & 8) ? value - diff : value + diff;
            value = value > 32767 ? 32767 : (value < -32767 ? -32767 : value);
            index += kIndexStep[code & 7];
            index = index < 0 ? 0 : (index > 88 ? 88 : index);
        }

    private:
        /// The next pair into cur_; false if the ring has none yet (or the
        /// track is over).
        bool nextPair(std::uint32_t written)
        {
            if (played_ >= total_)
                return false;
            if (inBlock_ == kPairsPerBlock)
            {
                if (block_ >= written)
                    return false; // (not there yet: the ARM9 is behind)
                const std::uint16_t *b = ring_ + (block_ % ringBlocks_) * kBlockWords;
                value_[0] = static_cast<std::int16_t>(b[0]);
                index_[0] = static_cast<std::int16_t>(b[1]);
                value_[1] = static_cast<std::int16_t>(b[2]);
                index_[1] = static_cast<std::int16_t>(b[3]);
                if (index_[0] < 0 || index_[0] > 88 || index_[1] < 0 || index_[1] > 88)
                {
                    // (A block that is not one: silence, not noise.)
                    index_[0] = index_[1] = 0;
                    value_[0] = value_[1] = 0;
                }
                words_ = b + 4;
                inBlock_ = 0;
                ++block_;
            }
            const unsigned word = words_[inBlock_ >> 1];
            const unsigned byte = (inBlock_ & 1) ? word >> 8 : word & 0xFF;
            decode(value_[0], index_[0], byte & 15);
            decode(value_[1], index_[1], (byte >> 4) & 15);
            cur_[0] = value_[0];
            cur_[1] = value_[1];
            ++inBlock_;
            ++played_;
            return true;
        }

        const std::uint16_t *ring_ = nullptr;
        const std::uint16_t *words_ = nullptr;
        std::uint32_t ringBlocks_ = 0, total_ = 0, step_ = 0, phase_ = 0, played_ = 0, inBlock_ = 0, block_ = 0;
        std::uint32_t dry_ = 0;
        std::int32_t value_[2] = {0, 0}, index_[2] = {0, 0};
        std::int32_t prev_[2] = {0, 0}, cur_[2] = {0, 0};
        int volume_ = 256;
        bool on_ = false;

    public:
        /// Which block the ring's reader is in (it must not be written
        /// over): the ARM9 may write block `n` while n < reading() + blocks.
        std::uint32_t reading() const { return block_ == 0 ? 0 : block_ - 1; }
    };
} // namespace hldata
