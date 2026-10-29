#pragma once

// Light that changes (the level's LightPatch records): how strong each of
// the game's light "styles" is at a moment, and the colour of a vertex such
// light reaches, worked out again from that.
//
// Styles 1 to 12 are the game's own patterns -- a string of letters, one for
// each tenth of a second, 'a' dark and 'm' as bright as the map's light was
// made ('z' twice that) -- played round and round. 32 and up are the lights a
// switch turns on and off: all or nothing.
//
// std-only and inline: the host tests run it.

#include <cstdint>

#include "HlLevel.hpp"

namespace hldata
{
    /// As strong as the light was made: what 'm' is.
    inline constexpr int kStyleWhole = 256;

    /// The game's patterns (its world.cpp), by style.
    inline constexpr const char *const kStylePattern[13] = {
        "m",
        "mmnmmommommnonmmonqnmmo",                              // 1: flicker
        "abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba", // 2: slow strong pulse
        "mmmmmaaaaammmmmaaaaaabcdefgabcdefg",                  // 3: candle
        "mamamamamama",                                         // 4: fast strobe
        "jklmnopqrstuvwxyzyxwvutsrqponmlkj",                    // 5: gentle pulse
        "nmonqnmomnmomomno",                                    // 6: flicker
        "mmmaaaabcdefgmmmmaaaammmaamm",                         // 7: candle
        "mmmaaammmaaammmaabcdefaaaammmmabcdefmmmaaaa",          // 8: candle
        "aaaaaaaazzzzzzzz",                                     // 9: slow strobe
        "mmamammmmammamamaaamammma",                            // 10: fluorescent flicker
        "abcdefghijklmnopqrrqponmlkjihgfedcba",                 // 11: slow pulse, not to black
        "mmnnmmnnnmmnn"};                                       // 12: underwater

    /// How strong `style` is at tenth-of-a-second `tick` (kStyleWhole: as
    /// made), with the switched ones in `off` (a bit each) dark -- and, on,
    /// each by its pattern in `patterns` (for styles 32 on; null: steady).
    inline int lightStyleValue(int style, std::uint32_t tick, std::uint64_t off,
                               const char *const *patterns = nullptr)
    {
        if (style < 0 || style >= kLightStyles)
            return kStyleWhole;
        const char *pattern = nullptr;
        if (style >= 32)
        {
            if ((off >> style) & 1)
                return 0;
            pattern = patterns != nullptr ? patterns[style - 32] : nullptr;
            if (pattern == nullptr || pattern[0] < 'a' || pattern[0] > 'z')
                return kStyleWhole;
        }
        else if (style == 0 || style > 12)
        {
            return kStyleWhole;
        }
        else
        {
            pattern = kStylePattern[style];
        }
        std::uint32_t length = 0;
        while (pattern[length] != '\0')
            ++length;
        const char letter = pattern[tick % length];
        return (((letter < 'a' ? 'a' : (letter > 'z' ? 'z' : letter)) - 'a') * kStyleWhole) / 12;
    }

    /// Every style's strength at once, into `values` (kLightStyles of
    /// them). True if any is not what `values` had.
    inline bool lightStyleValues(std::uint32_t tick, std::uint64_t off, std::uint16_t *values,
                                 const char *const *patterns = nullptr)
    {
        bool changed = false;
        for (int style = 0; style < kLightStyles; ++style)
        {
            const auto value = static_cast<std::uint16_t>(lightStyleValue(style, tick, off, patterns));
            changed = changed || value != values[style];
            values[style] = value;
        }
        return changed;
    }

    /// The colours of the vertices that changing light reaches, written
    /// into the geometry stream for the strengths in `values`: for each,
    /// what it is with none of that light, and what each style adds, times
    /// how strong that style is now.
    inline void lightPatchApply(const LightPatch *patches, std::uint32_t count, const std::uint16_t *values,
                                std::uint32_t *stream)
    {
        std::uint32_t i = 0;
        while (i < count)
        {
            // (A base, then what is added to it.)
            const std::uint32_t word = patches[i].word;
            int r = patches[i].colour & 31, g = (patches[i].colour >> 5) & 31, b = (patches[i].colour >> 10) & 31;
            for (++i; i < count && patches[i].style != kPatchBase; ++i)
            {
                const int strength = values[patches[i].style < kLightStyles ? patches[i].style : 0];
                r += ((patches[i].colour & 31) * strength) >> 8;
                g += (((patches[i].colour >> 5) & 31) * strength) >> 8;
                b += (((patches[i].colour >> 10) & 31) * strength) >> 8;
            }
            r = r > 31 ? 31 : r;
            g = g > 31 ? 31 : g;
            b = b > 31 ? 31 : b;
            stream[word] = static_cast<std::uint32_t>(r | g << 5 | b << 10);
        }
    }
} // namespace hldata
