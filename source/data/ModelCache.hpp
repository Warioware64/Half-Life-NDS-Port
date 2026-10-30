#pragma once

#include "EngineObject.hpp"

// The models of the loaded map: each one's cooked file (mdl/<map>/<name>.hmd)
// held in memory and opened, and its skin (mdl/<name>.htx) in video memory,
// except weapons' (v_*), of which only the one in hand is in video memory.
//
// One entry per ModelRef of the level, in the level's order. A model that is
// not on the card, or does not fit, is simply not there: its view's header is
// null and the actors made of it are not drawn.
//
// A weapon the map was not cooked with (kModelRefLazy: one a cheat gives) is
// named but not loaded: wield() reads it then, out of the common archive
// (wpn/), one such at a time.

#include <NEAMain.h>

#include <string>

#include "HlModel.hpp"

namespace hlrt
{
    class ModelCache
    {
    public:
        /// Load the models of the level that is loaded, out of its archive.
        bool create();
        void destroy();
        /// As destroy(), for when the 3D engine has been shut down under it:
        /// the skins went with the engine.
        void forget();

        /// For when the 3D engine is brought up again under a map that
        /// stays loaded: the skins are gone with the old engine and are
        /// loaded into the new one. The models themselves are not touched.
        void forgetSkins();
        void loadSkins();

        const hldata::ModelView *views() const { return views_; }
        int count() const { return count_; }
        NEA_Material *skin(unsigned index) const { return materials_[index]; }
        /// Drawing the model `index` as player `slot` of a game between
        /// consoles: the palette word of that player's colours, for a skin
        /// that has them (a 256-colour one: a console sent the game); 0 for
        /// the skin's own.
        std::uint32_t playerPalette(unsigned index, int slot) const
        {
            return static_cast<int>(index) == playerModel_ && slot >= 0 && slot < kPlayerColours
                       ? playerPaletteWords_[slot]
                       : 0;
        }
        /// Player `slot`'s colours (0..255 each, the game's top and bottom):
        /// their palette made again.
        void setPlayerColours(int slot, int top, int bottom);
        static constexpr int kPlayerColours = 8;
        int missing() const { return missing_; }

        /// `index`: weapon in hand; -1: none. A weapon the map has not is
        /// loaded there and then (too little memory: it is not drawn).
        void wield(int index);
        /// The weapon in hand read again if it could not be when it was
        /// taken up (a console sent the game: its weapons come at the end
        /// of the map's stream, after the player is in).
        void rewield()
        {
            if (wanted_ >= 0)
                wield(wanted_);
        }
        /// Do what wield() asked, once a frame: the skin is written into a
        /// slot of video memory by VramWriter, and the weapon is drawn with
        /// it once it is all there.
        void apply();
        /// Whether the model can be drawn: not a weapon whose skin is not
        /// in video memory yet.
        bool drawable(unsigned index) const
        {
            return kept_ == nullptr || index >= static_cast<unsigned>(count_) || kept_[index] == 0 ||
                   resident_ == static_cast<int>(index);
        }
        /// The texel bytes of the largest weapon skin (a 4x4 texture: its
        /// index is half as many again), for the slots it is written to.
        std::uint32_t largestSkin() const;
        /// The palette memory one weapon the map has not needs when it is
        /// taken up (its skin's colours), to be left free; 0 if none.
        int lazyPaletteBytes() const { return anyLazy_ ? 512 * 2 : 0; }
        /// The places a weapon's skin is written to (TextureStream's region;
        /// `count` of them, 0 to 2, of largestSkin() texel bytes each): one
        /// for the weapon in hand, one for the next. No slots: weapons are
        /// drawn bare.
        void setSlots(void *const *texels, int count);
        /// The slots are going (the map is left): nothing more is written to
        /// them, and nothing is drawn from them.
        void dropSlots();
        /// Models whose skin did not load (not on the card, or no room).
        int bare() const { return bare_; }

        ModelCache() = default;
        ModelCache(const ModelCache &) = delete;
        ModelCache &operator=(const ModelCache &) = delete;

    private:
        // A skin a player's colours are laid over (TextureCache::takeRemapBase):
        // its model, its palette as it came, and each player's.
        int playerModel_ = -1;
        std::uint16_t playerBase_[256] = {};
        NEA_Palette *playerPalettes_[kPlayerColours] = {};
        std::uint32_t playerPaletteWords_[kPlayerColours] = {};
        void makePlayerPalettes(int model);
        void release(bool engineGone);
        void putAway(std::uint32_t now);
        bool lazy(int index) const;
        std::string skinPath(int index) const;
        void loadLazy(int index);
        void dropLazy(int index);

        hldata::ModelView *views_ = nullptr;
        std::uint8_t **blocks_ = nullptr;
        NEA_Material **materials_ = nullptr;
        NEA_Palette **palettes_ = nullptr;
        std::uint8_t *kept_ = nullptr;
        int wanted_ = -1;
        int resident_ = -1;     ///< the skin in hand: written, and drawn
        int residentSlot_ = -1;
        int writing_ = -1;      ///< the skin being written ...
        int writingSlot_ = -1;
        std::uint32_t ticket_ = 0; ///< ... done with this VramWriter ticket
        struct Slot
        {
            void *texels = nullptr;
            std::uint32_t freeSince = 0; ///< the frame it was let go (TextureStream::blanks)
            bool used = false;
        };
        Slot slots_[2];
        int slotCount_ = 0;
        int count_ = 0;
        int missing_ = 0;
        int bare_ = 0;
        bool anyLazy_ = false; ///< the level names a weapon it has not
    };

    HL_ENGINE_OBJECT(ModelCache, modelCache);
} // namespace hlrt
