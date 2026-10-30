#pragma once

// The card of a multiplayer map on the title's top screen, in the lobby and
// while looking for a game: its picture (HlPreview.hpp: taken the first time
// this console hosted a match on it), and beside it its name, how many it is
// for, whether a console without the game can be sent it.
//
// Two textures, made when the card is shown and let go when it is not: the
// picture in the console's own colours, and the words, drawn with the touch
// screen's fonts into a canvas off the screen and made a texture of the UI's
// colours. Drawn by HudDraw with the title's picture (menuFrame), over it.

#include <string>

#include "HlNetMaps.hpp"

namespace hlrt
{
    class MapCard
    {
    public:
        /// The card of `map` (null: none), and what to say if its facts are
        /// not known (`elsewhere`: a host's map this card has not got).
        /// Nothing is made again for the card already shown.
        void show(const hldata::NetMapInfo *map, const char *elsewhere = nullptr);
        void hide() { show(nullptr); }
        bool shown() const { return text_ != nullptr; }
        /// Inside NEA_Process, after the picture behind it.
        void draw(void (*corner)(int sx, int sy, int s, int t));

    private:
        std::string shownName_;
        void *picture_ = nullptr; ///< (NEA_Material), when there is a picture
        void *text_ = nullptr;    ///< (NEA_Material)
        void *textPalette_ = nullptr;
        void release();
    };

    extern MapCard mapCard;
} // namespace hlrt
