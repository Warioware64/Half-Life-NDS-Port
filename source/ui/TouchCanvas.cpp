#include "global_header.hpp"

#include "ui/TouchCanvas.hpp"

#include "Global.hpp"

namespace hlrt
{
    TouchCanvas touchCanvas;

    namespace
    {
        constexpr int kFirstTile = 64; // (the map is where tiles 0 to 63 would be)
        std::uint32_t plainWords[hldata::kPlainFontWords];
    } // namespace

    void TouchCanvas::create()
    {
        if (created_)
            return;
        created_ = true;
        videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE);
        vramSetBankH(VRAM_H_SUB_BG);
        REG_BG0CNT_SUB = BG_32x32 | BG_COLOR_16 | BG_MAP_BASE(0) | BG_TILE_BASE(0) | BG_PRIORITY(0);
        REG_BG0HOFS_SUB = 0;
        REG_BG0VOFS_SUB = 0;
        for (int i = 0; i < 16; ++i)
            BG_PALETTE_SUB[i] = hldata::uiColour(i);
        canvas_.tiles = reinterpret_cast<std::uint32_t *>(reinterpret_cast<std::uint8_t *>(BG_TILE_RAM_SUB(0)) +
                                                          kFirstTile * 32);
        screen_ = canvas_.tiles;
        canvas_.width = 256;
        canvas_.height = 192;
        canvas_.clear();
        std::uint16_t *map = BG_MAP_RAM_SUB(0);
        for (int y = 0; y < 32; ++y)
        {
            for (int x = 0; x < 32; ++x)
                map[y * 32 + x] = static_cast<std::uint16_t>(kFirstTile + (y < 24 ? y * 32 + x : 0));
        }
        loadFonts();
    }

    void TouchCanvas::tint(int x, int y, int w, int h, const std::uint16_t *three)
    {
        // (The second sixteen colours: the first, three of them changed.)
        if (three != nullptr)
        {
            for (int i = 0; i < 16; ++i)
                BG_PALETTE_SUB[16 + i] = i >= 1 && i <= 3 ? three[i - 1] : hldata::uiColour(i);
        }
        std::uint16_t *map = BG_MAP_RAM_SUB(0);
        for (int cy = y; cy < y + h && cy < 24; ++cy)
        {
            for (int cx = x; cx < x + w && cx < 32; ++cx)
                map[cy * 32 + cx] = static_cast<std::uint16_t>((kFirstTile + cy * 32 + cx) | (three != nullptr ? 1 << 12 : 0));
        }
    }

    void TouchCanvas::loadFonts()
    {
        if (loaded_)
            return;
        loaded_ = true;
        // The console's letters are a bit a pixel, eight bytes a letter,
        // from the space on.
        const ConsoleFont &own = consoleGetDefault()->font;
        if (own.gfx != nullptr && own.bpp == 1 && own.numChars >= 96)
        {
            hldata::plainFont(static_cast<const std::uint8_t *>(own.gfx), false, plainWords);
            plain_.open(reinterpret_cast<const std::uint8_t *>(plainWords), sizeof(plainWords));
        }
        if (!global.hasData)
            return;
        for (int n = 0; n < hldata::kFontCount; ++n)
        {
            std::FILE *file = std::fopen(global.commonPath(hldata::kFontFiles[n]).c_str(), "rb");
            if (file == nullptr)
                continue;
            std::fseek(file, 0, SEEK_END);
            const long size = std::ftell(file);
            std::fseek(file, 0, SEEK_SET);
            // (The size is the card's word: bounded before it is believed.)
            std::uint8_t *block = size > 0 && size < 64 * 1024 ? static_cast<std::uint8_t *>(std::malloc(size)) : nullptr;
            const bool got = block != nullptr &&
                             std::fread(block, 1, static_cast<std::size_t>(size), file) == static_cast<std::size_t>(size);
            std::fclose(file);
            // (Kept for good if it is a font: these are for as long as the
            // game runs.)
            if (!got || !fonts_[n].open(block, static_cast<std::size_t>(size)))
                std::free(block);
        }
    }

    const hldata::FontView &TouchCanvas::font(int which)
    {
        loadFonts();
        if (which >= 0 && which < hldata::kFontCount && fonts_[which].ok())
            return fonts_[which];
        return plain_;
    }

    const hldata::FontView &TouchCanvas::plain()
    {
        loadFonts();
        return plain_;
    }
} // namespace hlrt
