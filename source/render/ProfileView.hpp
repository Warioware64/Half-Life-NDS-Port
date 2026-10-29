#pragma once

// PROFILE's picture of the player on the top screen: the model the others
// see in a match, turning slowly where the title's picture is, in the
// colours the profile has (HlRemap.hpp's, as the match lays them over it).
//
// Its model and its skin in 256 colours are the common archive's
// (HlUi.hpp: kProfileModelFile, kProfileSkinFile), there where the card has
// a multiplayer map. They are read when PROFILE opens and let go when it
// closes: nothing of it stays beside a map. Drawn by HudDraw with the
// title's picture (menuFrame), after it.

#include <cstdint>

namespace hlrt
{
    class ProfileView
    {
    public:
        /// Read the model and its skin, and show it in these colours. False
        /// (and nothing shown) if the card has not got them, or there is no
        /// room.
        bool open(int top, int bottom);
        /// The colours changed: the skin's palette made again.
        void setColours(int top, int bottom);
        /// Let it all go.
        void close();
        bool shown() const { return data_ != nullptr; }
        /// Inside NEA_Process, after the picture behind it: the model, a
        /// little further round than last time.
        void draw();

    private:
        std::uint8_t *data_ = nullptr;
        void *view_ = nullptr;     ///< (hldata::ModelView)
        void *material_ = nullptr; ///< (NEA_Material)
        void *palette_ = nullptr;  ///< (NEA_Palette): the skin's own
        void *colours_ = nullptr;  ///< (NEA_Palette): in the profile's colours
        std::uint16_t base_[256] = {};
        int clip_ = 0;
        std::uint32_t frame_ = 0;
        std::uint16_t yaw_ = 0;
        std::uint32_t since_ = 0;
    };

    extern ProfileView profileView;
} // namespace hlrt
