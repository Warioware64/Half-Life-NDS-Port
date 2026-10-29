#pragma once

// A player's colours on the player model, as the game puts them there: the
// "top" and "bottom" colours of a player's settings (0..255, a hue each)
// laid over two runs of the colours of the model's skin. A texture named
// "DM_Base" has them at 160..191 (the top) and 192..223 (the bottom); one
// named "RemapN_aaa_bbb_ccc" says where they are, aaa..bbb and bbb+1..ccc.
// Each colour of a run keeps how bright and how strong it is, and takes the
// new hue (xash3d's Image_PaletteHueReplace, which this follows line for
// line, and the game's own studio renderer before it).

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace hldata
{
    /// Where a skin texture's two runs are; `top` false for one that has none.
    struct RemapRuns
    {
        bool any = false;
        int topFirst = 0, topLast = 0;       ///< the top colour's
        int bottomFirst = 0, bottomLast = 0; ///< the bottom colour's
    };

    inline RemapRuns remapRunsOf(const char *textureName)
    {
        RemapRuns runs;
        auto starts = [&](const char *prefix)
        {
            for (std::size_t i = 0; prefix[i] != '\0'; ++i)
            {
                const char a = textureName[i], b = prefix[i];
                if (a == '\0' || (a | 0x20) != (b | 0x20))
                    return false;
            }
            return true;
        };
        if (starts("DM_Base"))
        {
            runs.any = true;
            runs.topFirst = 160;
            runs.topLast = 191;
            runs.bottomFirst = 192;
            runs.bottomLast = 223;
        }
        else if (starts("remap") && std::strlen(textureName) >= 18)
        {
            // "RemapN_aaa_bbb_ccc": three numbers of three figures.
            auto number = [&](int at)
            {
                char text[4] = {textureName[at], textureName[at + 1], textureName[at + 2], '\0'};
                const int n = std::atoi(text);
                return n < 0 ? 0 : (n > 255 ? 255 : n);
            };
            runs.any = true;
            runs.topFirst = number(7);
            runs.topLast = number(11);
            runs.bottomFirst = runs.topLast + 1;
            runs.bottomLast = number(15);
        }
        return runs;
    }

    /// The colours `first`..`last` of `palette` (RGB, three bytes each) given
    /// the hue `colour` (0..255 for the whole circle).
    inline void hueReplace(std::uint8_t (*palette)[3], int colour, int first, int last)
    {
        const float hue = static_cast<float>(colour) * (360.0f / 255);
        for (int i = first; i <= last && i < 256; ++i)
        {
            float r = palette[i][0], g = palette[i][1], b = palette[i][2];
            const float most = (r > g ? (r > b ? r : b) : (g > b ? g : b)) / 255.0f;
            float least = (r < g ? (r < b ? r : b) : (g < b ? g : b)) / 255.0f;
            if (most == 0)
                continue;
            const float value = most;
            const float saturation = (most - least) / most;
            least = value * (1.0f - saturation);
            if (hue <= 120.0f)
            {
                b = least;
                if (hue < 60)
                {
                    r = value;
                    g = least + hue * (value - least) / (120.0f - hue);
                }
                else
                {
                    g = value;
                    r = least + (120.0f - hue) * (value - least) / hue;
                }
            }
            else if (hue <= 240.0f)
            {
                r = least;
                if (hue < 180.0f)
                {
                    g = value;
                    b = least + (hue - 120.0f) * (value - least) / (240.0f - hue);
                }
                else
                {
                    b = value;
                    g = least + (240.0f - hue) * (value - least) / (hue - 120.0f);
                }
            }
            else
            {
                g = least;
                if (hue < 300.0f)
                {
                    b = value;
                    r = least + (hue - 240.0f) * (value - least) / (360.0f - hue);
                }
                else
                {
                    r = value;
                    b = least + (360.0f - hue) * (value - least) / (hue - 240.0f);
                }
            }
            palette[i][0] = static_cast<std::uint8_t>(r * 255);
            palette[i][1] = static_cast<std::uint8_t>(g * 255);
            palette[i][2] = static_cast<std::uint8_t>(b * 255);
        }
    }

    /// A player's skin palette: the model's own 256 colours (`base`, RGB) with
    /// the two runs in the player's colours, as the console's 15-bit colours.
    inline void playerPalette(const std::uint8_t (*base)[3], const RemapRuns &runs, int top, int bottom,
                              std::uint16_t *out)
    {
        std::uint8_t palette[256][3];
        std::memcpy(palette, base, sizeof(palette));
        if (runs.any)
        {
            hueReplace(palette, top, runs.topFirst, runs.topLast);
            hueReplace(palette, bottom, runs.bottomFirst, runs.bottomLast);
        }
        for (int i = 0; i < 256; ++i)
            out[i] = static_cast<std::uint16_t>((palette[i][0] >> 3) | ((palette[i][1] >> 3) << 5) |
                                                ((palette[i][2] >> 3) << 10) | 0x8000);
    }
} // namespace hldata
