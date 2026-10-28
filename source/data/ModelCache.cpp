#include <cstring>
#include "data/ModelCache.hpp"
#include "data/TextureStream.hpp"

#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>

#include "Global.hpp"
#include "data/LevelFile.hpp"
#include "data/TextureCache.hpp"
#include "data/VramWriter.hpp"

namespace hlrt
{
    ModelCache modelCache;

    namespace
    {
        /// A model is tens of kilobytes; one that claims to be megabytes is
        /// not one.
        constexpr long kMaxModelBytes = 1024 * 1024;

        /// A model file, read whole into a block of its own; null if it is
        /// not there, too big, or `spare` bytes would not be left after it.
        std::uint8_t *readModel(const std::string &path, std::uint32_t &size, std::size_t spare = 0)
        {
            std::FILE *file = std::fopen(path.c_str(), "rb");
            if (file == nullptr)
                return nullptr;
            std::fseek(file, 0, SEEK_END);
            const long length = std::ftell(file);
            std::fseek(file, 0, SEEK_SET);
            // The size is the card's word: bounded before it is trusted with
            // an allocation. (malloc's blocks are aligned for the words the
            // file is made of.)
            std::uint8_t *block = nullptr;
            if (length > 0 && length <= kMaxModelBytes &&
                global.heapHeadroom() >= static_cast<std::size_t>(length) + spare)
                block = static_cast<std::uint8_t *>(std::malloc(static_cast<std::size_t>(length)));
            const bool got = block != nullptr && std::fread(block, 1, static_cast<std::size_t>(length), file) ==
                                                     static_cast<std::size_t>(length);
            std::fclose(file);
            if (!got)
            {
                std::free(block);
                return nullptr;
            }
            size = static_cast<std::uint32_t>(length);
            return block;
        }
    } // namespace

    void ModelCache::release(bool engineGone)
    {
        for (int i = 0; i < count_; ++i)
        {
            if (blocks_ != nullptr)
                std::free(blocks_[i]);
            if (!engineGone && materials_ != nullptr && materials_[i] != nullptr)
                NEA_MaterialDelete(materials_[i]);
            if (!engineGone && palettes_ != nullptr && palettes_[i] != nullptr)
                NEA_PaletteDelete(palettes_[i]);
        }
        std::free(views_);
        std::free(blocks_);
        std::free(materials_);
        std::free(palettes_);
        std::free(kept_);
        kept_ = nullptr;
        wanted_ = resident_ = residentSlot_ = writing_ = writingSlot_ = -1;
        slotCount_ = 0;
        views_ = nullptr;
        blocks_ = nullptr;
        materials_ = nullptr;
        palettes_ = nullptr;
        count_ = 0;
        missing_ = 0;
        anyLazy_ = false;
    }

    void ModelCache::destroy() { release(false); }
    void ModelCache::forget() { release(true); }

    void ModelCache::forgetSkins()
    {
        for (int i = 0; i < count_; ++i)
        {
            materials_[i] = nullptr;
            palettes_[i] = nullptr;
        }
        // (Gone with the old engine, and the slots with it; wanted_ stays, and
        // is put back once there are slots again.)
        resident_ = residentSlot_ = writing_ = writingSlot_ = -1;
        slotCount_ = 0;
    }

    std::uint32_t ModelCache::largestSkin() const
    {
        std::uint32_t most = 0;
        for (int i = 0; kept_ != nullptr && i < count_; ++i)
        {
            if (kept_[i] == 0 || materials_[i] == nullptr)
                continue;
            const NEA_Material *m = materials_[i];
            const std::uint32_t bytes = static_cast<std::uint32_t>(m->ram_sizex * m->ram_sizey / 4);
            most = bytes > most ? bytes : most;
        }
        // (... and of one the map has not, which may be taken up: all of
        // those are made at one size.)
        if (anyLazy_)
            most = most > hldata::kLazySkinTexels / 4 ? most : hldata::kLazySkinTexels / 4;
        return (most + 31u) & ~31u;
    }

    void ModelCache::dropSlots()
    {
        if (resident_ >= 0 && materials_ != nullptr && materials_[resident_] != nullptr)
            NEA_MaterialTexVramUnload(materials_[resident_]);
        resident_ = residentSlot_ = writing_ = writingSlot_ = -1;
        slotCount_ = 0;
    }

    void ModelCache::setSlots(void *const *texels, int count)
    {
        // (Whatever was in the old ones is gone with them.)
        dropSlots();
        slotCount_ = count < 0 ? 0 : (count > 2 ? 2 : count);
        for (int k = 0; k < 2; ++k)
            slots_[k] = Slot{k < slotCount_ ? texels[k] : nullptr, 0, false};
        if (slotCount_ == 0)
        {
            // No room for any: drawn bare, as a skin that did not fit always was.
            for (int i = 0; kept_ != nullptr && i < count_; ++i)
            {
                if (kept_[i] != 0)
                    ++bare_;
                kept_[i] = 0;
            }
        }
    }

    void ModelCache::putAway(std::uint32_t now)
    {
        if (resident_ < 0)
            return;
        if (materials_[resident_] != nullptr)
            NEA_MaterialTexVramUnload(materials_[resident_]);
        // (Written over no sooner than two frames on: the pictures until
        // then were drawn with it.)
        slots_[residentSlot_].used = false;
        slots_[residentSlot_].freeSince = now;
        resident_ = residentSlot_ = -1;
    }

    void ModelCache::apply()
    {
        if (kept_ == nullptr || slotCount_ == 0)
            return;
        const std::uint32_t now = textureStream.blanks();
        // A skin written: it is the one in hand now.
        if (writing_ >= 0 && vramWriter.done(ticket_))
        {
            const NEA_Material *m = materials_[writing_];
            putAway(now);
            if (NEA_MaterialTexBindFixed(materials_[writing_], NEA_TEX4X4, m->ram_sizex, m->ram_sizey, m->ram_flags,
                                         slots_[writingSlot_].texels) == 1)
            {
                resident_ = writing_;
                residentSlot_ = writingSlot_;
            }
            else
            {
                slots_[writingSlot_].used = false;
                slots_[writingSlot_].freeSince = now;
            }
            writing_ = writingSlot_ = -1;
        }
        if (writing_ >= 0 || resident_ == wanted_)
            return;
        if (wanted_ < 0)
        {
            putAway(now);
            return;
        }
        NEA_Material *m = wanted_ < count_ && kept_[wanted_] != 0 ? materials_[wanted_] : nullptr;
        const std::uint32_t texels = m != nullptr ? static_cast<std::uint32_t>(m->ram_sizex * m->ram_sizey / 4) : 0;
        if (m == nullptr || m->ram_data == nullptr || m->ram_format != NEA_TEX4X4 ||
            m->ram_size < texels + texels / 2 || texels > largestSkin())
        {
            if (m != nullptr && kept_[wanted_] != 0)
            {
                kept_[wanted_] = 0; // (not one this can write: drawn bare)
                ++bare_;
            }
            wanted_ = -1; // not one that is kept in main memory: it is drawn as it is
            return;
        }
        // A slot that nothing on the screen draws from; with one slot only,
        // the weapon in hand is put away first.
        int slot = -1;
        for (int k = 0; k < slotCount_ && slot < 0; ++k)
        {
            if (!slots_[k].used && now - slots_[k].freeSince >= 2)
                slot = k;
        }
        if (slot < 0)
        {
            if (slotCount_ == 1)
                putAway(now);
            return; // (a frame or two on)
        }
        // [texels][index], as NEA keeps it.
        const auto *data = static_cast<const std::uint8_t *>(m->ram_data);
        void *at = slots_[slot].texels;
        const std::uint32_t first = vramWriter.write(at, data, texels);
        const std::uint32_t second =
            first != 0 ? vramWriter.write(NEA_Texture4x4IndexAddress(at), data + texels, texels / 2) : 0;
        if (second == 0)
            return; // (the queue is full: next frame)
        slots_[slot].used = true;
        writing_ = wanted_;
        writingSlot_ = slot;
        ticket_ = second;
    }

    bool ModelCache::lazy(int index) const
    {
        return index >= 0 && index < count_ && hldata::modelRefLazy(level.view.modelRefs[index]);
    }

    std::string ModelCache::skinPath(int i) const
    {
        const std::string skin = hldata::modelSkinFile(level.view.modelRefs[i].name, views_[i].header->atlasWidth,
                                                       views_[i].header->atlasHeight);
        return lazy(i) ? global.commonPath("wpn/" + skin) : global.mapPath("mdl/") + skin;
    }

    void ModelCache::wield(int index)
    {
        wanted_ = index;
        if (lazy(index) && views_[index].header == nullptr)
            loadLazy(index);
    }

    /// A weapon the map has not, read when it is taken up: the one before
    /// it of those let go first (not the tripmine's, which may be on a wall
    /// -- nor one whose skin is being written).
    void ModelCache::loadLazy(int index)
    {
        for (int i = 0; i < count_; ++i)
        {
            if (i != index && lazy(i) && views_[i].header != nullptr && i != writing_ &&
                std::strcmp(level.view.modelRefs[i].name, "v_tripmine") != 0)
                dropLazy(i);
        }
        const char *name = level.view.modelRefs[index].name;
        std::uint32_t size = 0;
        // (The map's own memory comes first: room left beside it, or not
        // at all.)
        constexpr std::size_t kSpare = 200 * 1024;
        std::uint8_t *block = nullptr;
        if (global.isDSi)
            block = readModel(global.commonPath(std::string("wpn/") + name + ".dsi.hmd"), size, kSpare);
        if (block == nullptr)
            block = readModel(global.commonPath(std::string("wpn/") + name + ".hmd"), size, kSpare);
        if (block == nullptr || !views_[index].open(block, size))
        {
            std::free(block);
            views_[index] = hldata::ModelView();
            ++missing_;
            return;
        }
        blocks_[index] = block;
        if (views_[index].header->atlasWidth == 0)
            return;
        // Its skin kept in main memory, as a weapon's is, while there are
        // slots to write it to; else (and the tripmine's: it is on walls
        // too) straight into video memory, where there is room.
        const bool kept = slotCount_ > 0 && std::strcmp(name, "v_tripmine") != 0;
        kept_[index] = kept ? 1 : 0;
        if (!TextureCache::loadFile(skinPath(index).c_str(), false, views_[index].header->atlasWidth,
                                    views_[index].header->atlasHeight, 0, materials_[index], palettes_[index],
                                    nullptr, kept))
        {
            ++bare_;
            kept_[index] = 0;
        }
    }

    void ModelCache::dropLazy(int index)
    {
        if (resident_ == index)
            putAway(textureStream.blanks());
        if (materials_[index] != nullptr)
            NEA_MaterialDelete(materials_[index]);
        if (palettes_[index] != nullptr)
            NEA_PaletteDelete(palettes_[index]);
        materials_[index] = nullptr;
        palettes_[index] = nullptr;
        kept_[index] = 0;
        views_[index] = hldata::ModelView();
        std::free(blocks_[index]);
        blocks_[index] = nullptr;
    }

    void ModelCache::loadSkins()
    {
        bare_ = 0;
        for (int i = 0; i < count_; ++i)
        {
            if (views_[i].header == nullptr || views_[i].header->atlasWidth == 0 ||
                materials_[i] != nullptr)
                continue;
            const std::string skin = skinPath(i);
            // The weapons' are kept in main memory until one is taken up:
            // of the six, one is in hand. (Not the tripmine's: it is also
            // the mine on the wall, the map's own and the player's -- and
            // small.)
            const char *name = level.view.modelRefs[i].name;
            const bool weapon = name[0] == 'v' && name[1] == '_' && std::strcmp(name, "v_tripmine") != 0;
            if (kept_ != nullptr)
                kept_[i] = weapon ? 1 : 0;
            if (!TextureCache::loadFile(skin.c_str(), false, views_[i].header->atlasWidth,
                                        views_[i].header->atlasHeight, 0, materials_[i], palettes_[i], nullptr,
                                        weapon))
            {
                ++bare_;
                if (kept_ != nullptr)
                    kept_[i] = 0;
            }
        }
    }

    bool ModelCache::create()
    {
        destroy();
        const int count = static_cast<int>(level.view.modelRefCount);
        views_ = static_cast<hldata::ModelView *>(std::calloc(count + 1, sizeof(hldata::ModelView)));
        blocks_ = static_cast<std::uint8_t **>(std::calloc(count + 1, sizeof(std::uint8_t *)));
        materials_ = static_cast<NEA_Material **>(std::calloc(count + 1, sizeof(NEA_Material *)));
        palettes_ = static_cast<NEA_Palette **>(std::calloc(count + 1, sizeof(NEA_Palette *)));
        kept_ = static_cast<std::uint8_t *>(std::calloc(count + 1, 1));
        if (views_ == nullptr || blocks_ == nullptr || materials_ == nullptr || palettes_ == nullptr ||
            kept_ == nullptr)
        {
            destroy();
            return false;
        }
        count_ = count;
        for (int i = 0; i < count; ++i)
        {
            new (&views_[i]) hldata::ModelView();
            // (A weapon the map has not: when it is taken up.)
            if (lazy(i))
            {
                anyLazy_ = true;
                continue;
            }
            const char *name = level.view.modelRefs[i].name;
            // A DSi has the memory for the file with the game's own mesh as
            // its finest level, where the converter made one.
            std::uint32_t size = 0;
            std::uint8_t *block = nullptr;
            if (global.isDSi)
                block = readModel(global.mapPath("mdl/") + name + ".dsi.hmd", size);
            if (block == nullptr)
                block = readModel(global.mapPath("mdl/") + name + ".hmd", size);
            if (block == nullptr || !views_[i].open(block, size))
            {
                std::free(block);
                views_[i] = hldata::ModelView();
                ++missing_;
                continue;
            }
            blocks_[i] = block;
        }
        // Their skins. Without one (not on the card, or no room in video
        // memory) a model is drawn in its lighting alone.
        loadSkins();
        return true;
    }
} // namespace hlrt
