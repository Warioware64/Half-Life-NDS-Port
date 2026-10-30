#pragma once

// A multiplayer map's picture, for its card in the menus:
// <card>/_nds/hl_nds/data/saves/previews/<map>.hpv
//
// Taken by the console that hosts a match on the map the first time it does
// (NetGame.cpp): the map seen from where the game shows it between matches
// (its info_intermission; else where a player first comes in), the screen's
// 3D shrunk by half. A header, then kPreviewWidth by kPreviewHeight pixels of
// the console's 15-bit colour, row by row.
//
// std-only: the host tests make one and read it back.

#include <cstdint>
#include <cstring>

namespace hldata
{
    inline constexpr char kPreviewMagic[4] = {'H', 'L', 'P', 'V'};
    inline constexpr std::uint16_t kPreviewVersion = 1;
    inline constexpr int kPreviewWidth = 128;
    inline constexpr int kPreviewHeight = 96;
    inline constexpr const char *kPreviewFolder = "previews/";

    struct PreviewHeader
    {
        char magic[4];
        std::uint16_t version;
        std::uint16_t width;
        std::uint16_t height;
        std::uint16_t pad;
    };
    static_assert(sizeof(PreviewHeader) == 12);
    inline constexpr std::size_t kPreviewBytes =
        sizeof(PreviewHeader) + static_cast<std::size_t>(kPreviewWidth) * kPreviewHeight * 2;

    inline void previewHeader(PreviewHeader &header)
    {
        std::memcpy(header.magic, kPreviewMagic, 4);
        header.version = kPreviewVersion;
        header.width = kPreviewWidth;
        header.height = kPreviewHeight;
        header.pad = 0;
    }

    /// Whether `bytes` is a picture of this version and size.
    inline bool previewValid(const std::uint8_t *bytes, std::size_t size)
    {
        if (bytes == nullptr || size != kPreviewBytes)
            return false;
        PreviewHeader header;
        std::memcpy(&header, bytes, sizeof(header));
        return std::memcmp(header.magic, kPreviewMagic, 4) == 0 && header.version == kPreviewVersion &&
               header.width == kPreviewWidth && header.height == kPreviewHeight;
    }

    /// The screen (256 by 192, 15-bit colour, as the display capture writes
    /// it) shrunk by half: each pixel the average of the four it stands for.
    /// `out`: kPreviewWidth * kPreviewHeight.
    inline void previewShrink(const std::uint16_t *screen, std::uint16_t *out)
    {
        for (int y = 0; y < kPreviewHeight; ++y)
        {
            for (int x = 0; x < kPreviewWidth; ++x)
            {
                int sum[3] = {0, 0, 0};
                for (int dy = 0; dy < 2; ++dy)
                {
                    for (int dx = 0; dx < 2; ++dx)
                    {
                        const std::uint16_t c = screen[(y * 2 + dy) * 256 + x * 2 + dx];
                        sum[0] += c & 31;
                        sum[1] += (c >> 5) & 31;
                        sum[2] += (c >> 10) & 31;
                    }
                }
                out[y * kPreviewWidth + x] =
                    static_cast<std::uint16_t>(((sum[0] + 2) / 4) | ((sum[1] + 2) / 4) << 5 | ((sum[2] + 2) / 4) << 10 | 0x8000);
            }
        }
    }
} // namespace hldata
