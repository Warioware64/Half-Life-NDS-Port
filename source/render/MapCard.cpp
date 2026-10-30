#include "render/MapCard.hpp"

#include <nds.h>

#include <NEAMain.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Global.hpp"
#include "HlFont.hpp"
#include "HlPreview.hpp"
#include "ui/TouchCanvas.hpp"

namespace hlrt
{
    MapCard mapCard;

    namespace
    {
        using namespace hldata;

        // Where it is on the top screen: the picture, then the words.
        constexpr int kTop = 100;
        constexpr int kPictureX = 8, kPictureW = 96, kPictureH = 72;
        constexpr int kTextX = 112, kTextW = 128, kTextH = 88;
        constexpr int kTextureH = 128; ///< (a texture's height is a power of two: the words in its top rows)
    } // namespace

    void MapCard::release()
    {
        if (picture_ != nullptr)
            NEA_MaterialDelete(static_cast<NEA_Material *>(picture_));
        if (text_ != nullptr)
            NEA_MaterialDelete(static_cast<NEA_Material *>(text_));
        if (textPalette_ != nullptr)
            NEA_PaletteDelete(static_cast<NEA_Palette *>(textPalette_));
        picture_ = text_ = textPalette_ = nullptr;
    }

#ifdef HLDS_CHILD
    // (A console sent the game shows none: its maps are the host's.)
    void MapCard::show(const NetMapInfo *, const char *) {}
    void MapCard::draw(void (*)(int, int, int, int)) {}
#else
    void MapCard::show(const NetMapInfo *map, const char *elsewhere)
    {
        const std::string name = map != nullptr ? map->name : (elsewhere != nullptr ? elsewhere : "");
        if (name == shownName_ && (shown() || name.empty()))
            return;
        release();
        shownName_ = name;
        if (name.empty())
            return;

        // The picture, if this console has taken one.
        bool pictured = false;
        if (map != nullptr && global.hasFat)
        {
            const std::string path = global.savesDir() + kPreviewFolder + map->name + ".hpv";
            if (std::FILE *file = std::fopen(path.c_str(), "rb"))
            {
                auto *bytes = static_cast<std::uint8_t *>(std::malloc(kPreviewBytes));
                // (A texture is 128 square: the picture in its top 96 rows.)
                auto *texels = static_cast<std::uint16_t *>(std::calloc(128 * 128, 2));
                if (bytes != nullptr && texels != nullptr && std::fread(bytes, 1, kPreviewBytes, file) == kPreviewBytes &&
                    previewValid(bytes, kPreviewBytes))
                {
                    std::memcpy(texels, bytes + sizeof(PreviewHeader), kPreviewBytes - sizeof(PreviewHeader));
                    NEA_Material *material = NEA_MaterialCreate();
                    if (material != nullptr &&
                        NEA_MaterialTexLoad(material, NEA_A1RGB5, 128, 128, NEA_TEXGEN_TEXCOORD, texels) == 1)
                    {
                        picture_ = material;
                        pictured = true;
                    }
                    else if (material != nullptr)
                        NEA_MaterialDelete(material);
                }
                std::free(bytes);
                std::free(texels);
                std::fclose(file);
            }
        }

        // The words: drawn as on the touch screen, into a canvas of their
        // own, then made the texture's (A3I5: the UI's sixteen colours, the
        // background see-through by half).
        static std::uint32_t tiles[(kTextW / 8) * (kTextH / 8) * 8];
        Canvas canvas;
        canvas.tiles = tiles;
        canvas.width = kTextW;
        canvas.height = kTextH;
        canvas.clear(kUiBack);
        canvas.frame(0, 0, kTextW, kTextH, kUiEdge);
        const FontView &medium = touchCanvas.font(kFontMedium);
        const FontView &small = touchCanvas.font(kFontSmall);
        char line[48];
        int y = 4;
        std::snprintf(line, sizeof(line), "%s", name.c_str());
        for (char &c : line)
            c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        drawText(canvas, medium, 6, y, line, inkFor(medium, kUiOrange));
        y += medium.height() + 3;
        auto say = [&](const char *text, int ramp)
        {
            drawText(canvas, small, 6, y, text, inkFor(small, ramp));
            y += small.height() + 2;
        };
        if (map != nullptr)
        {
            std::snprintf(line, sizeof(line), "For up to %d players", netMapPlayers(*map));
            say(line, kUiWhite);
            if (map->guestFits)
            {
                say("Fits a console", kUiWhite);
                say("without the game", kUiWhite);
            }
            if (!pictured)
                say("No picture yet", kUiGrey);
        }
        else
            say("Not on this card", kUiGrey);
        static std::uint8_t texels[kTextW * kTextureH];
        std::memset(texels, 0, sizeof(texels));
        for (int py = 0; py < kTextH; ++py)
        {
            for (int px = 0; px < kTextW; ++px)
            {
                const int c = canvas.at(px, py);
                texels[py * kTextW + px] = static_cast<std::uint8_t>((c == kUiBack ? 5 << 5 : 7 << 5) | c);
            }
        }
        std::uint16_t colours[32];
        for (int i = 0; i < 32; ++i)
            colours[i] = uiColour(i & 15);
        NEA_Material *material = NEA_MaterialCreate();
        NEA_Palette *palette = NEA_PaletteCreate();
        if (material != nullptr && palette != nullptr &&
            NEA_MaterialTexLoad(material, NEA_A3PAL32, kTextW, kTextureH, NEA_TEXGEN_TEXCOORD, texels) == 1 &&
            NEA_PaletteLoad(palette, colours, 32, NEA_A3PAL32) == 1)
        {
            NEA_MaterialSetPalette(material, palette);
            text_ = material;
            textPalette_ = palette;
        }
        else
        {
            if (material != nullptr)
                NEA_MaterialDelete(material);
            if (palette != nullptr)
                NEA_PaletteDelete(palette);
        }
    }

    void MapCard::draw(void (*corner)(int sx, int sy, int s, int t))
    {
        if (text_ == nullptr)
            return;
        // Over the title's picture, at its depth, as the title's own logo is
        // -- and as it is, see-through (by a hair): an opaque polygon drawn
        // where another is, at that depth, was not seen.
        NEA_PolyFormat(30, 2, static_cast<NEA_LightEnum>(0), NEA_CULL_NONE, NEA_DEPTH_TEST_EQUAL);
        GFX_COLOR = 0x7FFF;
        if (picture_ != nullptr)
        {
            NEA_MaterialUse(static_cast<const NEA_Material *>(picture_));
        }
        else
        {
            // (None yet: a plain dark plate where it would be.)
            GFX_TEX_FORMAT = 0;
            GFX_COLOR = RGB15(7, 7, 8);
        }
        GFX_BEGIN = GL_QUADS;
        corner(kPictureX, kTop, 0, 0);
        corner(kPictureX, kTop + kPictureH, 0, kPreviewHeight);
        corner(kPictureX + kPictureW, kTop + kPictureH, kPreviewWidth, kPreviewHeight);
        corner(kPictureX + kPictureW, kTop, kPreviewWidth, 0);
        GFX_COLOR = 0x7FFF;
        NEA_PolyFormat(31, 3, static_cast<NEA_LightEnum>(0), NEA_CULL_NONE, NEA_DEPTH_TEST_EQUAL);
        NEA_MaterialUse(static_cast<const NEA_Material *>(text_));
        GFX_BEGIN = GL_QUADS;
        corner(kTextX, kTop, 0, 0);
        corner(kTextX, kTop + kTextH, 0, kTextH);
        corner(kTextX + kTextW, kTop + kTextH, kTextW, kTextH);
        corner(kTextX + kTextW, kTop, kTextW, 0);
    }
#endif
} // namespace hlrt
