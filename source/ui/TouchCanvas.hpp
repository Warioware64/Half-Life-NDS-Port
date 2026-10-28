#pragma once

// The touch screen as something to draw on: one layer of the sub engine,
// 256 by 192 pixels in sixteen colours, every cell with a tile of its own
// (shared/HlFont.hpp draws on it); and the fonts it is written in.
//
// Video memory: bank H (32 KB, sub background) and nothing else, so that the
// four big banks stay free for 3D textures and capture.
//
//   0 ..  2 KB   the layer's map: cell (x, y) shows tile 64 + y * 32 + x
//   2 .. 26 KB   those 768 tiles
//
// The fonts are the game's own, cooked to the card (ui/font*.hfn). On a card
// that has not got them everything is written in the 8x8 letters that are in
// the ROM anyway, by the same code.

#include "HlFont.hpp"

namespace hlrt
{
    class TouchCanvas
    {
    public:
        /// Set the sub engine up. Idempotent, until reset().
        void create();
        /// The video mode was changed under it: the next create() starts over.
        void reset() { created_ = false; }

        hldata::Canvas &canvas() { return canvas_; }
        /// Drawn off the screen, into `tiles` (laid out as the screen's 768),
        /// until onScreen(); the screen's own tiles, to copy to and from.
        void offScreen(std::uint32_t *tiles) { canvas_.tiles = tiles; }
        void onScreen() { canvas_.tiles = screen_; }
        std::uint32_t *screenTiles() const { return screen_; }
        /// kFontSmall, kFontMedium or kFontTitle -- or the plain letters,
        /// where the card has not got that one.
        const hldata::FontView &font(int which);
        /// The ROM's own 8x8 letters.
        const hldata::FontView &plain();

    private:
        bool created_ = false;
        bool loaded_ = false;
        hldata::Canvas canvas_;
        std::uint32_t *screen_ = nullptr;
        hldata::FontView fonts_[hldata::kFontCount];
        hldata::FontView plain_;
        void loadFonts();
    };

    extern TouchCanvas touchCanvas;
} // namespace hlrt
