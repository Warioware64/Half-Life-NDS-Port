#pragma once

#include "EngineObject.hpp"

// The top screen's brightness, which three things want a say in: a fade the
// map has asked for, the dimness of being under water, and the curtain that
// is down while a map is loaded (the 3D picture left on the screen is drawn
// again every frame out of video memory, and video memory is being written
// over). One owner, so that none of them undoes another: the darkest wins.
//
// A fade is the game's env_fade: a colour over the picture, by how much of
// 255. Into black or white it is the hardware's brightness, over everything
// on the screen; into any other colour, a layer of that one colour (BG1, out
// of Bank D, which is otherwise only the frozen picture's) blended over the
// 3D picture and the sprites -- the HUD and the writing, which are put
// behind it for that (sprites and BG0 at priority 1, BG1 at 0). The hardware
// has 16 steps of either; the steps between them are made by showing the one
// below and the one above in turn, frame by frame, as often as each is
// nearer.

#include <cstdint>

namespace hlrt
{
    class ScreenFade
    {
    public:
        /// How far the map has the screen faded: 0 (not at all) to 255 (all
        /// of it), into `colour` (RGB15). Shown from the next tick().
        void setFade(int alpha, std::uint16_t colour);
        void setDim(bool under);
        /// Down: black, whatever else (and the map's fade is over: the
        /// next map, or the title, starts without it). `frames`, when it
        /// is let up: how many more pictures are to be handed to the
        /// hardware first (see tick()), so that the first thing seen is a
        /// whole new one.
        void curtainDown();
        void curtainUp(int frames = 0);
        /// Once for each picture handed to the hardware.
        void tick();
        bool curtained() const { return curtain_; }
        /// Bank D has been taken (FrameFreeze): the colour's layer is gone,
        /// and is set up again by the next tick() that wants it.
        void layerLost();

    private:
        int fade_ = 0;
        std::uint16_t colour_ = 0;
        bool dim_ = false;
        bool curtain_ = false;
        int wait_ = 0;
        int shown_ = 0;
        int carried_ = 0; ///< what is owed of a step between two (of 256): the next frame's
        bool layer_ = false;
        int blend_ = -1; ///< the layer's EVA as last set
        /// Show it all; `advance`: a new picture, so the steps between two
        /// move on.
        void apply(bool advance);
        void showLayer(int eva);
        void hideLayer();
    };

    HL_ENGINE_OBJECT(ScreenFade, screenFade);
} // namespace hlrt
