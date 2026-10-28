#pragma once

// The room the player is in, heard: the game's room DSP (HlRoomPresets.hpp)
// on what the console is playing. The ARM7's audio core runs this on the dry
// mix it captures (arm7/source), and the host tests run the same code: what
// they check is what the console does, sample for sample.
//
// The engine runs its DSP on the whole mix, in stages, in this order:
//  1. the sound's own colour: a lowpass under water, a wandering loudness
//     (weirdo 1);
//  2. a reverb: two delay lines fed with the mix in mono, of the room's size
//     and seven tenths of it, each going round with its feedback (and dulled
//     each time, if the room says so); what comes out of both, at 11/64,
//     added to both sides;
//  3. an echo: one delay line, up to 0.4 s, fed with the mix and the reverb,
//     its feedback dulled or not; a quarter of it added to both sides;
//  4. the left side a little late (up to a tenth of a second): wider.
//
// Here stages 1 and 4 touch the sound itself, so they run at the output rate
// (32728 or 47605 Hz); 2 and 3 only add to it, so they run at the game's own
// base rate -- the output rate over three or four, about 11 kHz -- on the mix
// averaged down, and what they add is drawn back up in straight lines. The
// lowpasses inside the loops are the gentle ones a 44 kHz engine's two- and
// three-sample averages are, said again at 11 kHz.
//
// A room changed: what the reverb and echo add fades out (a few milliseconds),
// the lines are emptied, the new room's numbers are taken, and it fades back
// in; the left side's lateness slides from the old to the new. Nothing clicks.
//
// Integer only, no division per sample (the ARM7 has none): std-only, for the
// host tests.

#include <cstdint>
#include <cstring>

#include "HlRoomPresets.hpp"

namespace hldata
{
    class RoomDsp
    {
    public:
        static constexpr int kReverbMost = 1200; ///< 100 ms at 11.9 kHz
        static constexpr int kDelayMost = 4800;  ///< 400 ms
        static constexpr int kLeftRing = 4096;   ///< more than 50 ms at 47.6 kHz (the most a room asks)
        static constexpr int kLowpassMost = 6;

        /// `rate`: the output rate (32728, 47605); `factor`: how many output
        /// samples to one of the reverb's (3 or 4).
        void init(int rate, int factor)
        {
            std::memset(this, 0, sizeof(*this));
            rate_ = rate;
            factor_ = factor < 1 ? 1 : (factor > 4 ? 4 : factor);
            low_ = rate_ / factor_;
            // (The average of 2 x factor samples, in Q15.)
            meanQ15_ = (32768 + factor_) / (2 * factor_);
            for (int k = 0; k < factor_; ++k)
                stepQ8_[k] = static_cast<std::int16_t>((k + 1) * 256 / factor_);
            // The engine's lowpass is six samples at 44.1 kHz: as many here
            // as cover the same time, its gain kept (6/4).
            taps_ = (6 * rate_ + 22050) / 44100;
            taps_ = taps_ < 2 ? 2 : (taps_ > kLowpassMost ? kLowpassMost : taps_);
            lowpassQ8_ = 384 / taps_;
            modGain_ = modTarget_ = 255;
            random_ = 0x1234567u;
            wetGain_ = 256;
            room_ = 0;
            pending_ = -1;
            apply(0);
        }

        /// The room to go to (0..28; anything else is 0). It is taken a few
        /// milliseconds on, once what the last one added has faded.
        void setRoom(int room)
        {
            room = room < 0 || room >= kRoomCount ? 0 : room;
            if (room != room_ || pending_ >= 0)
                pending_ = room;
        }
        int room() const { return pending_ >= 0 ? pending_ : room_; }

        /// `count` samples of the dry mix in, the room's out (the same
        /// arrays may be given for both).
        void process(const std::int16_t *inL, const std::int16_t *inR, std::int16_t *outL, std::int16_t *outR,
                     int count)
        {
            const RoomPreset *p = &kRoomPresets[room_];
            for (int i = 0; i < count; ++i)
            {
                int l = inL[i], r = inR[i];
                // 1. The sound's own colour.
                if (p->lowpass)
                {
                    sumL_ += l - histL_[lpAt_];
                    sumR_ += r - histR_[lpAt_];
                    histL_[lpAt_] = static_cast<std::int16_t>(l);
                    histR_[lpAt_] = static_cast<std::int16_t>(r);
                    lpAt_ = lpAt_ + 1 == taps_ ? 0 : lpAt_ + 1;
                    l = clamp((sumL_ * lowpassQ8_) >> 8);
                    r = clamp((sumR_ * lowpassQ8_) >> 8);
                }
                if (p->modulate)
                {
                    if (modGain_ == modTarget_)
                    {
                        random_ = random_ * 1664525u + 1013904223u;
                        modTarget_ = 32 + static_cast<int>((random_ >> 24) * 224 >> 8);
                    }
                    modGain_ += modGain_ < modTarget_ ? 1 : -1;
                    l = (l * modGain_) >> 8;
                    r = (r * modGain_) >> 8;
                }
                // 2, 3. Down to the reverb's rate, once in `factor_`.
                acc_ += l + r;
                if (++phase_ == factor_)
                {
                    phase_ = 0;
                    wetFrom_ = wetTo_;
                    // (At most 8 x 32768 x 4096: 32 bits are enough.)
                    wetTo_ = lowSample((acc_ * meanQ15_) >> 15);
                    acc_ = 0;
                    p = &kRoomPresets[room_]; // (a room may have been taken)
                }
                // ... and what they add, drawn back up (a reverb sample late).
                const int wet = wetFrom_ + (((wetTo_ - wetFrom_) * stepQ8_[phase_]) >> 8);
                l = clamp(l + wet);
                r = clamp(r + wet);
                // 4. The left side late.
                leftRing_[leftAt_] = static_cast<std::int16_t>(l);
                if (leftLen_ != 0 || leftFade_ != 0)
                {
                    const int now = leftRing_[(leftAt_ - leftLen_) & (kLeftRing - 1)];
                    if (leftFade_ != 0)
                    {
                        // (Sliding from the old lateness to the new.)
                        const int was = leftRing_[(leftAt_ - leftOld_) & (kLeftRing - 1)];
                        l = was + (((now - was) * (128 - leftFade_)) >> 7);
                        --leftFade_;
                    }
                    else
                    {
                        l = now;
                    }
                }
                leftAt_ = (leftAt_ + 1) & (kLeftRing - 1);
                outL[i] = static_cast<std::int16_t>(l);
                outR[i] = static_cast<std::int16_t>(r);
            }
        }

    private:
        static int clamp(int v) { return v < -32768 ? -32768 : (v > 32767 ? 32767 : v); }

        /// One sample of the mix (mono, the reverb's rate) through the reverb
        /// and the echo: what they add.
        int lowSample(int m)
        {
            // A room changing: what is added fades, the room is taken, it
            // comes back.
            if (pending_ >= 0)
            {
                wetGain_ -= 16;
                if (wetGain_ <= 0)
                {
                    wetGain_ = 0;
                    apply(pending_);
                    pending_ = -1;
                }
            }
            else if (wetGain_ < 256)
            {
                wetGain_ += 16;
            }
            int added = 0;
            if (rvbLen1_ != 0)
            {
                int d1 = rvb1_[rvbAt1_], d2 = rvb2_[rvbAt2_];
                if (rvbLow_)
                {
                    // (Dulled each time round: three parts of the new to one
                    // of the last.)
                    d1 = (3 * d1 + rvbLast1_) >> 2;
                    d2 = (3 * d2 + rvbLast2_) >> 2;
                    rvbLast1_ = d1;
                    rvbLast2_ = d2;
                }
                rvb1_[rvbAt1_] = static_cast<std::int16_t>(clamp(m + ((d1 * rvbFb_) >> 8)));
                rvb2_[rvbAt2_] = static_cast<std::int16_t>(clamp(m + ((d2 * rvbFb_) >> 8)));
                rvbAt1_ = rvbAt1_ + 1 == rvbLen1_ ? 0 : rvbAt1_ + 1;
                rvbAt2_ = rvbAt2_ + 1 == rvbLen2_ ? 0 : rvbAt2_ + 1;
                added += ((d1 + d2) * 11) >> 6;
            }
            if (dlyLen_ != 0)
            {
                int d = dly_[dlyAt_];
                if (dlyLow_)
                {
                    // (Two parts of the new to one each of the last two.)
                    const int was = d;
                    d = (2 * d + dlyLast1_ + dlyLast2_) >> 2;
                    dlyLast2_ = dlyLast1_;
                    dlyLast1_ = was;
                }
                dly_[dlyAt_] = static_cast<std::int16_t>(clamp(m + added + ((d * dlyFb_) >> 8)));
                dlyAt_ = dlyAt_ + 1 == dlyLen_ ? 0 : dlyAt_ + 1;
                added += d >> 2;
            }
            return (clamp(added) * wetGain_) >> 8;
        }

        int samplesOf(int ms, int rate, int most) const
        {
            if (ms <= 0)
                return 0;
            const int n = (ms * rate + 500) / 1000;
            return n < 1 ? 1 : (n > most ? most : n);
        }

        /// A room's numbers taken (between samples: not per sample).
        void apply(int room)
        {
            room_ = room;
            const RoomPreset &p = kRoomPresets[room];
            rvbLen1_ = samplesOf(p.reverbMs, low_, kReverbMost);
            rvbLen2_ = rvbLen1_ != 0 ? (rvbLen1_ * 71 + 50) / 100 : 0;
            rvbLen2_ = rvbLen1_ != 0 && rvbLen2_ < 1 ? 1 : rvbLen2_;
            rvbFb_ = p.reverbFb * 255 / 1000;
            rvbLow_ = p.reverbLow != 0;
            dlyLen_ = samplesOf(p.delayMs, low_, kDelayMost);
            dlyFb_ = p.delayFb * 255 / 1000;
            dlyLow_ = p.delayLow != 0;
            std::memset(rvb1_, 0, sizeof(rvb1_));
            std::memset(rvb2_, 0, sizeof(rvb2_));
            std::memset(dly_, 0, sizeof(dly_));
            rvbAt1_ = rvbAt2_ = dlyAt_ = 0;
            rvbLast1_ = rvbLast2_ = dlyLast1_ = dlyLast2_ = 0;
            std::memset(histL_, 0, sizeof(histL_));
            std::memset(histR_, 0, sizeof(histR_));
            sumL_ = sumR_ = 0;
            lpAt_ = 0;
            const int left = samplesOf(p.leftMs, rate_, kLeftRing - 1);
            if (left != leftLen_)
            {
                leftOld_ = leftLen_;
                leftLen_ = left;
                leftFade_ = 128;
            }
        }

        int rate_, factor_, low_, meanQ15_, taps_, lowpassQ8_;
        std::int16_t stepQ8_[4];
        int room_, pending_, wetGain_;
        int phase_, acc_, wetFrom_, wetTo_;
        // 1.
        std::int16_t histL_[kLowpassMost], histR_[kLowpassMost];
        int sumL_, sumR_, lpAt_;
        int modGain_, modTarget_;
        std::uint32_t random_;
        // 2.
        std::int16_t rvb1_[kReverbMost], rvb2_[kReverbMost];
        int rvbLen1_, rvbLen2_, rvbAt1_, rvbAt2_, rvbFb_, rvbLast1_, rvbLast2_;
        bool rvbLow_;
        // 3.
        std::int16_t dly_[kDelayMost];
        int dlyLen_, dlyAt_, dlyFb_, dlyLast1_, dlyLast2_;
        bool dlyLow_;
        // 4.
        std::int16_t leftRing_[kLeftRing];
        int leftAt_, leftLen_, leftOld_, leftFade_;
    };
} // namespace hldata
