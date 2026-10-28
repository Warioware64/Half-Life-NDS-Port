#include "data/TextureStream.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <NEAMain.h>

#include "DebugLog.hpp"
#include "Global.hpp"
#include "HlTexture.hpp"
#include "HlTextureStream.hpp"
#include "data/LevelFile.hpp"
#include "data/ModelCache.hpp"
#include "data/TextureCache.hpp"
#include "data/VramWriter.hpp"

namespace hlrt
{
    TextureStream textureStream;
    std::uint32_t *g_textureSeenAt = nullptr;
    std::uint32_t g_textureFrame = 0;

    namespace
    {
        using namespace hldata;

        /// What is read at the end of a frame waits here for the vertical
        /// blank. (The biggest streamed texture is 128 x 128, about 7 KB.)
        constexpr std::uint32_t kStageBytes = 32 * 1024;
        constexpr int kMaxStaged = 16;
        constexpr int kMaxEvicted = 16;
        /// A texture drawn this recently is wanted above everything; one
        /// drawn within kRememberFrames is remembered a little.
        constexpr std::uint32_t kSeenFrames = 6;
        constexpr std::uint32_t kRememberFrames = 120;
        constexpr std::uint32_t kSeenScore = 1u << 20;
        /// The textures every view wants, put there for good: at most this
        /// share of the memory (in hundredths), of those seen from at least
        /// this share of the map's leaves.
        constexpr int kPinnedPercent = 35;
        constexpr int kPinnedShare = 60;
        /// What a cycle that is drawn is kept ahead by: the pictures of the
        /// next two changes of picture (they come ten times a second).
        constexpr std::uint32_t kPictureTicks = 6;
        /// What a picture of a cycle that is not the one shown is wanted
        /// with (if there is room): above what is only remembered.
        constexpr std::uint32_t kSiblingScore = 1500;
        /// Of the heap's headroom, as much as a third may be the copies of
        /// textures kept in main memory (the cycles' pictures and the tiled
        /// floors' variants), and this much at the very most.
        constexpr std::size_t kCacheMost = 1024 * 1024;

        TextureStreamer planner;
        StreamInfo infos[kStreamMaxTextures];
        std::uint32_t seenAt[kStreamMaxTextures];
        alignas(32) std::uint8_t stage[kStageBytes];

        struct Staged
        {
            int id;
            TextureHeader header;
            std::uint32_t at;          ///< where its body starts in `stage`
            const std::uint8_t *body; ///< or where it is in main memory (then `at` is not used)
            std::uint32_t texelOffset; ///< where the plan put it
            std::uint32_t paletteOffset;
        };
        Staged staged[kMaxStaged];
        int stagedCount = 0;
        std::uint32_t stageUsed = 0;

        /// For a picture of a cycle (the game's '+' textures, and the other
        /// face's): the cycle's first picture and how many pictures it has.
        /// 1 for anything else.
        std::int16_t groupFirst[kStreamMaxTextures];
        std::uint8_t groupCount[kStreamMaxTextures];
        /// A copy of the texture file's header and body in main memory, for
        /// the ones that are swapped most (cycles' pictures, tiled floors'
        /// variants): nothing is read off the card to bring them back.
        std::uint8_t *ram[kStreamMaxTextures];
        int ramCount = 0;
        std::size_t ramBytes = 0;

        int streamedTotal = 0;
        int pinnedTotal = 0;
        int refusals = 0; ///< textures whose file was not what the plan had them as, or that NEA would not bind
        int flatNow = 0;
        std::uint32_t frameNo = 0;
        std::uint8_t failures[kStreamMaxTextures]; ///< how often each could not be loaded (HLDS_PROFILE's report)
        int refusalsLogged = 0;

        /// The video memory that is the plan's own: the plan's offsets are
        /// places in it. Texels (with their index where NEA says), in up to
        /// kMostRegions pieces that the plan sees one after another, a
        /// barrier between each two (TextureStreamer::addBarrier); and
        /// palettes. The weapons' skin slots are at the top of the first.
        constexpr int kMostRegions = 4;
        /// A piece smaller than this is left to NEA.
        constexpr int kLeastRegion = 1024;
        NEA_TextureRegion regions[kMostRegions]{};
        std::uint32_t regionAt[kMostRegions]; ///< where each starts in the plan's offsets
        std::uint32_t regionBytes[kMostRegions]; ///< how much of it the plan has
        int regionCount = 0;
        int regionTotal = 0; ///< texel bytes taken, skin slots and all
        NEA_PaletteRegion paletteRegion{};
        std::uint32_t skinBytes = 0;
        int skinSlots = 0;

        /// What was read, and when: it is queued to be written two frames
        /// after (the picture on the screen until then may be drawn from
        /// where it goes), `stagedNext` of them so far; and drawn once the
        /// last write's ticket is done.
        std::uint32_t stagedBlank = 0;
        int stagedNext = 0;
        std::uint32_t stagedTicket = 0;

        /// What was let go, freed two blanks after.
        struct Retired
        {
            NEA_Material *material;
            NEA_Palette *palette;
            std::uint32_t blank;
        };
        constexpr int kMostRetired = 64;
        Retired retired[kMostRetired];
        int retiredCount = 0;

        /// What a streamed texture costs NEA, and what the plan counts. The
        /// palette is a guess from above (the cooker's own rule for how many
        /// colours a texture of that many texels gets: the file has the true
        /// number, and is never bigger).
        StreamInfo infoFor(const TextureRef &ref)
        {
            StreamInfo info;
            const std::uint32_t texels = static_cast<std::uint32_t>(ref.width) * ref.height;
            const std::uint32_t colours = std::min<std::uint32_t>(512, std::max<std::uint32_t>(16, texels / 16)) & ~1u;
            info.texelBytes = texels / 4; // 4x4: the texels (the index goes where they say)
            info.paletteBytes = colours * 2;
            info.stageBytes = info.texelBytes + info.paletteBytes;
            info.streamed = true;
            return info;
        }

        bool fileFor(int id, TextureHeader &header, std::uint8_t *body, std::size_t room)
        {
            const TextureRef &ref = level.view.textures[id];
            char name[40];
            hldata::textureFileName(ref.name, ref.width, ref.height, false, name);
            const std::string path = global.texDir() + name;
            return TextureCache::readFile(path.c_str(), ref.width, ref.height, 0, header, body, room);
        }
    } // namespace

    namespace
    {
        /// The file is what the plan took it to be: no bigger than its places.
        bool fitsPlan(int id, const TextureHeader &h)
        {
            const TextureRef &ref = level.view.textures[id];
            return h.width == ref.width && h.height == ref.height && h.indexBytes == h.texelBytes / 2 &&
                   h.texelBytes <= infos[id].texelBytes && h.paletteBytes <= ByteArena<kStreamMaxBlocks>::round(infos[id].paletteBytes) &&
                   ((h.texelBytes | h.paletteBytes) & 3) == 0;
        }

        std::uint8_t *texelsAt(std::uint32_t offset)
        {
            int r = 0;
            while (r + 1 < regionCount && offset >= regionAt[r + 1])
                ++r;
            return static_cast<std::uint8_t *>(regions[r].texels) + (offset - regionAt[r]);
        }
        std::uint8_t *paletteAt(std::uint32_t offset) { return static_cast<std::uint8_t *>(paletteRegion.base) + offset; }

        /// A texture that could not be loaded: let go, and tried again later.
        void refuse(int id)
        {
            planner.failed(id);
            failures[id] = static_cast<std::uint8_t>(failures[id] < 255 ? failures[id] + 1 : 255);
            ++refusals;
#ifdef HLDS_PROFILE
            if (refusalsLogged < 24)
            {
                ++refusalsLogged;
                textureStream.report("refused");
            }
#endif
        }

        /// Its pieces are in video memory: NEA is told where, and it is drawn.
        void bind(int id, const TextureHeader &h, std::uint32_t texelOffset, std::uint32_t paletteOffset)
        {
            NEA_Material *material = NEA_MaterialCreate();
            NEA_Palette *palette = NEA_PaletteCreate();
            const bool ok =
                material != nullptr && palette != nullptr &&
                NEA_MaterialTexBindFixed(material, NEA_TEX4X4, h.width, h.height,
                                         static_cast<NEA_TextureFlags>(NEA_TEXTURE_WRAP_S | NEA_TEXTURE_WRAP_T),
                                         texelsAt(texelOffset)) == 1 &&
                NEA_PaletteBindFixed(palette, paletteAt(paletteOffset), h.paletteColours, NEA_TEX4X4) == 1;
            if (!ok)
            {
                if (material != nullptr)
                    NEA_MaterialDelete(material);
                if (palette != nullptr)
                    NEA_PaletteDelete(palette);
                refuse(id);
                return;
            }
            NEA_MaterialSetPalette(material, palette);
            textureCache.attachStreamed(static_cast<unsigned>(id), material, palette);
            planner.loaded(id);
        }
    } // namespace

    bool TextureStream::loadNow(int id, std::uint32_t texelOffset, std::uint32_t paletteOffset)
    {
        TextureHeader header;
        const std::uint8_t *body = stage;
        if (ram[id] != nullptr)
        {
            std::memcpy(&header, ram[id], sizeof(header));
            body = ram[id] + sizeof(header);
        }
        else if (!fileFor(id, header, stage, kStageBytes))
        {
            planner.failed(id);
            return false;
        }
        if (!fitsPlan(id, header))
        {
            refuse(id);
            return false;
        }
        // (While a map loads: nothing is being drawn from the region yet.)
        NEA_VramWrite(texelsAt(texelOffset), body, header.texelBytes);
        NEA_VramWrite(NEA_Texture4x4IndexAddress(texelsAt(texelOffset)), body + header.texelBytes, header.indexBytes);
        NEA_VramWrite(paletteAt(paletteOffset), body + header.texelBytes + header.indexBytes, header.paletteBytes);
        bind(id, header, texelOffset, paletteOffset);
        return planner.resident(id);
    }

    void TextureStream::begin()
    {
        end();
        const int count = std::min<int>(level.view.textureCount, kStreamMaxTextures);
        streamedTotal = 0;
        for (int i = 0; i < count; ++i)
        {
            infos[i] = StreamInfo();
            seenAt[i] = 0xFFFFFFFFu; // never
            if (textureCache.streamed(static_cast<unsigned>(i)))
            {
                infos[i] = infoFor(level.view.textures[i]);
                ++streamedTotal;
            }
        }
        // The region: what the streamed textures take, and the slots of the
        // weapons' skins, as far as what video memory has left will go.
        std::uint32_t texelNeed = 0, paletteNeed = 0;
        for (int i = 0; i < count; ++i)
        {
            if (!infos[i].streamed)
                continue;
            texelNeed += ByteArena<kStreamMaxBlocks>::round(infos[i].texelBytes);
            paletteNeed += ByteArena<kStreamMaxBlocks>::round(infos[i].paletteBytes);
        }
        skinBytes = modelCache.largestSkin();
        regionCount = 0;
        regionTotal = 0;
        {
            // The largest places first, until there is room for all.
            const int want = static_cast<int>(texelNeed + 2 * skinBytes);
            while (regionCount < kMostRegions && regionTotal < want)
            {
                NEA_TextureRegion &r = regions[regionCount];
                if (NEA_TextureRegionReserve4x4(want - regionTotal, &r) < kLeastRegion)
                {
                    NEA_TextureRegionRelease(&r);
                    break;
                }
                regionTotal += r.texelBytes;
                ++regionCount;
            }
        }
        // (Palettes: a little is left to NEA, as there always was -- and
        // room for the colours of a weapon the map has not, if it names one.)
        if (streamedTotal != 0)
            NEA_PaletteRegionReserve(std::min(static_cast<int>(paletteNeed),
                                              NEA_PaletteFreeMem() - 512 - modelCache.lazyPaletteBytes()),
                                     &paletteRegion);
        // Two skin slots (the weapon in hand, and the next one written while it
        // is still drawn) where the streamed textures all fit beside them, or
        // the second costs no more than an eighth of the region; else one (a
        // weapon taken up is not drawn for the frames it takes to write). At
        // the top of the first piece, the largest.
        const std::uint32_t first = regionCount > 0 ? static_cast<std::uint32_t>(regions[0].texelBytes) : 0;
        skinSlots = 0;
        if (skinBytes != 0)
        {
            const bool both = first >= 2 * skinBytes &&
                              (static_cast<std::uint32_t>(regionTotal) - 2 * skinBytes >= texelNeed ||
                               skinBytes <= static_cast<std::uint32_t>(regionTotal) / 8);
            skinSlots = both ? 2 : (first >= skinBytes ? 1 : 0);
        }
        {
            const std::uint32_t top = first - static_cast<std::uint32_t>(skinSlots) * skinBytes;
            void *const slots[2] = {static_cast<std::uint8_t *>(regions[0].texels) + top,
                                    static_cast<std::uint8_t *>(regions[0].texels) + top + skinBytes};
            modelCache.setSlots(slots, skinSlots);
        }
        // The plan's offsets: the pieces one after another, a barrier between.
        std::uint32_t room = 0;
        for (int r = 0; r < regionCount; ++r)
        {
            if (r > 0)
                room += TextureStreamer::kBarrierBytes;
            regionAt[r] = room;
            regionBytes[r] = static_cast<std::uint32_t>(regions[r].texelBytes) -
                             (r == 0 ? static_cast<std::uint32_t>(skinSlots) * skinBytes : 0);
            room += regionBytes[r];
        }
        if (streamedTotal == 0)
        {
            report("map loaded");
            return;
        }

        // The cycles: each picture knows its cycle's first and how many there
        // are. (The first picture of a texture says how many it has; the
        // other face's cycle where it starts and how many.)
        for (int i = 0; i < count; ++i)
        {
            groupFirst[i] = static_cast<std::int16_t>(i);
            groupCount[i] = 1;
            ram[i] = nullptr;
        }
        auto markCycle = [&](int first, int pictures)
        {
            if (pictures <= 1 || first < 0 || first + pictures > count)
                return;
            for (int k = 0; k < pictures; ++k)
            {
                groupFirst[first + k] = static_cast<std::int16_t>(first);
                groupCount[first + k] = static_cast<std::uint8_t>(pictures);
            }
        };
        for (int i = 0; i < count; ++i)
        {
            const TextureRef &ref = level.view.textures[i];
            if (ref.frames > 1)
                markCycle(i, ref.frames);
            if (ref.other != kNoTexture && ref.otherFrames > 1)
                markCycle(ref.other, ref.otherFrames);
        }
        // Main-memory copies of the pictures of the cycles and of the variants
        // a floor is tiled with (the game's '-' textures), as far as the heap
        // can spare: a cycle changes picture ten times a second, and a tiled
        // floor wears several, and when video memory is short the ones not
        // wanted at the moment are let go and come back from here, not from the
        // card.
        {
            ramCount = 0;
            ramBytes = 0;
            const std::size_t room = std::min<std::size_t>(global.heapHeadroom() / 3, kCacheMost);
            for (int i = 0; i < count; ++i)
            {
                const TextureRef &ref = level.view.textures[i];
                const bool tiled = ref.name[0] == '-' && ref.name[1] >= '0' && ref.name[1] <= '9';
                if (!infos[i].streamed || (groupCount[i] <= 1 && !tiled))
                    continue;
                TextureHeader header;
                if (!fileFor(i, header, stage, kStageBytes))
                    continue;
                const std::size_t body = header.texelBytes + header.indexBytes + header.paletteBytes;
                if (ramBytes + sizeof(header) + body > room)
                    continue;
                auto *copy = static_cast<std::uint8_t *>(std::malloc(sizeof(header) + body));
                if (copy == nullptr)
                    continue;
                std::memcpy(copy, &header, sizeof(header));
                std::memcpy(copy + sizeof(header), stage, body);
                ram[i] = copy;
                // (Nothing to read, but as much to write in the blank: it
                // counts against what one blank may take as any other.)
                ramBytes += sizeof(header) + body;
                ++ramCount;
            }
        }

        planner.init(infos, count, room, static_cast<std::uint32_t>(paletteRegion.bytes));
        for (int r = 1; r < regionCount; ++r)
            planner.addBarrier(regionAt[r] - TextureStreamer::kBarrierBytes);
        pinnedTotal = 0;
        refusals = 0;
        refusalsLogged = 0;
        std::memset(failures, 0, sizeof(failures));
        flatNow = 0;
        frameNo = 1;
        g_textureFrame = frameNo;

        if (planner.allFit())
        {
            // Nothing to stream: every texture is there for good.
            bool leftOut = false;
            for (int i = 0; i < count; ++i)
            {
                std::uint32_t t, p;
                if (infos[i].streamed && planner.pin(i, t, p))
                {
                    ++pinnedTotal;
                    loadNow(i, t, p);
                }
                else if (infos[i].streamed)
                {
                    leftOut = true;
                }
            }
            // ... unless the sums fit and the pieces of memory did not: then
            // none is there for good, so that the one left out (c0a0e's
            // entrance door) takes the place of one that is not in view the
            // moment it is, instead of being flat for as long as the map is.
            if (leftOut)
            {
                planner.unpinAll();
                pinnedTotal = 0;
            }
        }
        else
        {
            // What most views want is there for good: widest first.
            std::uint32_t pinRoom = room / 100 * kPinnedPercent;
            for (int share = 100; share >= kPinnedShare; --share)
            {
                for (int i = 0; i < count; ++i)
                {
                    if (!infos[i].streamed || groupCount[i] > 1 || level.view.textures[i].share != share ||
                        infos[i].texelBytes > pinRoom)
                        continue;
                    std::uint32_t t, p;
                    if (!planner.pin(i, t, p))
                        continue;
                    pinRoom -= infos[i].texelBytes;
                    ++pinnedTotal;
                    loadNow(i, t, p);
                }
            }
            // The rest by how widely they are wanted, as far as there is room.
            planner.clearScores();
            for (int i = 0; i < count; ++i)
            {
                if (!infos[i].streamed || planner.pinned(i))
                    continue;
                planner.addScore(i, 1u + level.view.textures[i].share);
            }
            planner.rebuild();
            for (int round = 0; round < 64; ++round)
            {
                StreamLoad loads[kMaxStaged];
                int evicted[kMaxEvicted], evictedCount = 0;
                const int made = planner.plan(0xFFFFFFFFu, kStageBytes, loads, kMaxStaged, evicted, kMaxEvicted, evictedCount);
                if (made == 0)
                    break;
                for (int k = 0; k < made; ++k)
                    loadNow(loads[k].id, loads[k].texelOffset, loads[k].paletteOffset);
                planner.rebuild(); // (what did not fit is let alone: Failed)
            }
        }
        // Those that did not fit are tried again once something is seen.
        planner.retryFailed();
        stagedCount = 0;
        stagedNext = 0;
        stageUsed = 0;
        g_textureSeenAt = seenAt;
        active_ = true;
        report("map loaded");
    }

    void TextureStream::retire(NEA_Material *material, NEA_Palette *palette)
    {
        if (material == nullptr && palette == nullptr)
            return;
        if (retiredCount >= kMostRetired)
            freeRetired(true); // (never: but rather a flicker than a leak)
        retired[retiredCount++] = {material, palette, blank_};
    }

    void TextureStream::freeRetired(bool all)
    {
        int kept = 0;
        for (int k = 0; k < retiredCount; ++k)
        {
            const Retired &r = retired[k];
            if (!all && blank_ - r.blank < 2)
            {
                retired[kept++] = r;
                continue;
            }
            if (r.material != nullptr)
                NEA_MaterialDelete(r.material);
            if (r.palette != nullptr)
                NEA_PaletteDelete(r.palette);
        }
        retiredCount = kept;
    }

    void TextureStream::end()
    {
        // Nothing more is written to the region, and it goes back to NEA
        // (what was bound in it goes with TextureCache and ModelCache).
        vramWriter.clear();
        modelCache.dropSlots();
        for (int r = 0; r < regionCount; ++r)
            NEA_TextureRegionRelease(&regions[r]);
        regionCount = 0;
        regionTotal = 0;
        NEA_PaletteRegionRelease(&paletteRegion);
        skinSlots = 0;
        skinBytes = 0;
        freeRetired(true);
        for (int i = 0; i < kStreamMaxTextures; ++i)
        {
            std::free(ram[i]);
            ram[i] = nullptr;
        }
        ramCount = 0;
        ramBytes = 0;
        g_textureSeenAt = nullptr;
        active_ = false;
        stagedCount = 0;
        stagedNext = 0;
        stageUsed = 0;
        streamedTotal = 0;
        pinnedTotal = 0;
        // (The materials go with TextureCache.)
    }

    void TextureStream::demand()
    {
        planner.clearScores();
        flatNow = 0;
        for (int id = 0; id < planner.count(); ++id)
        {
            if (!infos[id].streamed || planner.pinned(id))
                continue;
            const std::uint32_t at = seenAt[id];
            if (at == 0xFFFFFFFFu)
                continue;
            const std::uint32_t age = frameNo - at;
            std::uint32_t score = 0;
            if (age <= kSeenFrames)
                score = kSeenScore;
            else if (age <= kRememberFrames)
                score = 1000 - age;
            if (score != 0)
                planner.addScore(id, score);
            if (age <= 2)
                planner.setRecent(id);
            if (age <= kSeenFrames && !planner.resident(id))
                ++flatNow;
        }
        // A cycle that is being drawn: the pictures it will show in the next
        // changes are wanted as much as the one shown; its other pictures a
        // little, so that they stay if there is room and go first if not.
        const std::uint32_t blanks = static_cast<std::uint32_t>(Global::vblanks());
        for (int first = 0; first < planner.count(); ++first)
        {
            const int pictures = groupCount[first];
            if (pictures <= 1 || groupFirst[first] != first)
                continue;
            bool drawn = false;
            for (int k = 0; k < pictures && !drawn; ++k)
            {
                const std::uint32_t at = seenAt[first + k];
                drawn = at != 0xFFFFFFFFu && frameNo - at <= kSeenFrames;
            }
            if (!drawn)
                continue;
            for (int k = 0; k < pictures; ++k)
            {
                const int id = first + k;
                if (infos[id].streamed && !planner.pinned(id))
                    planner.addScore(id, kSiblingScore);
            }
            // (The picture of the next frame, and of the two changes after it: a
            // picture comes in at the vertical blank before the frame that
            // shows it.)
            static const std::uint32_t kAhead[3] = {1, 4, 7};
            for (const std::uint32_t ahead : kAhead)
            {
                const int id = first + static_cast<int>(((blanks + ahead) / kPictureTicks) % static_cast<std::uint32_t>(pictures));
                if (infos[id].streamed && !planner.pinned(id))
                    planner.addScore(id, kSeenScore);
            }
        }
    }

    void TextureStream::upload()
    {
        ++blank_;
        freeRetired(false);
        if (!active_ || stagedCount == 0 || blank_ - stagedBlank < 2)
            return;
        // Queued to be written (VramWriter, in the vertical blanks to come);
        // what the queue has no room for, next frame.
        while (stagedNext < stagedCount)
        {
            const Staged &s = staged[stagedNext];
            const std::uint8_t *body = s.body != nullptr ? s.body : stage + s.at;
            std::uint8_t *texels = texelsAt(s.texelOffset);
            const TextureHeader &h = s.header;
            if (vramWriter.write(texels, body, h.texelBytes) == 0)
                break;
            const std::uint32_t index =
                vramWriter.write(NEA_Texture4x4IndexAddress(texels), body + h.texelBytes, h.indexBytes);
            const std::uint32_t colours =
                index != 0 ? vramWriter.write(paletteAt(s.paletteOffset), body + h.texelBytes + h.indexBytes,
                                              h.paletteBytes)
                           : 0;
            if (colours == 0)
            {
                // (Half queued: what was queued is written again with the rest.)
                break;
            }
            stagedTicket = colours;
            ++stagedNext;
        }
        if (stagedNext < stagedCount || !vramWriter.done(stagedTicket))
            return;
        // All there: drawn from the next frame on.
        for (int k = 0; k < stagedCount; ++k)
            bind(staged[k].id, staged[k].header, staged[k].texelOffset, staged[k].paletteOffset);
        stagedCount = 0;
        stagedNext = 0;
        stageUsed = 0;
    }

    void TextureStream::update(int budgetBytes)
    {
        if (!active_)
            return;
        ++frameNo;
        g_textureFrame = frameNo;
        if (stagedCount != 0)
            return;
        demand();
        if (frameNo % 8 == 0)
            planner.retryFailed();
#ifdef HLDS_PROFILE
        if (frameNo % 1024 == 0)
            report("every 1024 frames");
#endif
        if (frameNo % 4 == 0 || frameNo < 16)
            planner.rebuild();

        StreamLoad loads[kMaxStaged];
        int evicted[kMaxEvicted], evictedCount = 0;
        const int made = planner.plan(static_cast<std::uint32_t>(budgetBytes), kStageBytes, loads, kMaxStaged, evicted,
                                      kMaxEvicted, evictedCount);
        // Those that make way are drawn flat from now on; what takes their
        // place is written no sooner than two frames from now (upload), when
        // no picture drawn with them is still being drawn.
        for (int k = 0; k < evictedCount; ++k)
            textureCache.detachStreamed(static_cast<unsigned>(evicted[k]));
        for (int k = 0; k < made; ++k)
        {
            Staged &s = staged[stagedCount];
            s.id = loads[k].id;
            s.at = stageUsed;
            s.body = nullptr;
            s.texelOffset = loads[k].texelOffset;
            s.paletteOffset = loads[k].paletteOffset;
            if (ram[s.id] != nullptr)
            {
                // In main memory already: nothing to read.
                std::memcpy(&s.header, ram[s.id], sizeof(s.header));
                s.body = ram[s.id] + sizeof(s.header);
                if (!fitsPlan(s.id, s.header))
                {
                    refuse(s.id);
                    continue;
                }
                ++stagedCount;
                continue;
            }
            if (!fileFor(s.id, s.header, stage + stageUsed, kStageBytes - stageUsed))
            {
                planner.failed(s.id);
                continue;
            }
            if (!fitsPlan(s.id, s.header))
            {
                refuse(s.id);
                continue;
            }
            const std::uint32_t body = s.header.texelBytes + s.header.indexBytes + s.header.paletteBytes;
            stageUsed += (body + 31u) & ~31u;
            ++stagedCount;
        }
        stagedNext = 0;
        stagedBlank = blank_;
    }

    StreamStats TextureStream::stats() const
    {
        StreamStats s;
        s.streamed = streamedTotal;
        s.resident = planner.residentCount();
        s.pinned = pinnedTotal;
        s.loads = planner.loads();
        s.evictions = planner.evictions();
        s.refused = refusals;
        s.flatNow = flatNow;
        s.copies = ramCount;
        s.copiesKb = static_cast<int>(ramBytes / 1024);
        s.texelKb = static_cast<int>(planner.texelBytesUsed() / 1024);
        s.capacityKb = static_cast<int>(planner.texelCapacity() / 1024);
        const VramWriter::Stats w = vramWriter.stats();
        s.late = static_cast<int>(w.late);
        s.lastLine = w.mostLine;
        s.dropped = planner.dropped();
        for (int i = 0; i < planner.count(); ++i)
            s.attached += infos[i].streamed && textureCache.material(static_cast<unsigned>(i)) != nullptr ? 1 : 0;
        return s;
    }

    void TextureStream::report(const char *why)
    {
#ifdef HLDS_PROFILE
        std::FILE *log = std::fopen(global.dataPath("perf.txt").c_str(), "a");
        if (log == nullptr)
            return;
        const StreamStats st = stats();
        const VramWriter::Stats w = vramWriter.stats();
        std::fprintf(log, "tex %s: frame %lu, %d/%d resident (%d attached), %d flat in view, %d/%d KB of %d KB, "
                          "palettes %d/%d, %d refused, %d retired, staged %d/%d\n",
                     why, static_cast<unsigned long>(frameNo), st.resident, st.streamed, st.attached, st.flatNow,
                     static_cast<int>(planner.texelBytesUsed() / 1024), static_cast<int>(planner.texelCapacity() / 1024),
                     regionTotal / 1024, static_cast<int>(planner.paletteBytesUsed()),
                     static_cast<int>(planner.paletteCapacity()), st.refused, retiredCount, stagedNext, stagedCount);
        std::fprintf(log, "  region: %d KB of texels in %d pieces (%d, %d, %d, %d; %d skin slots of %lu), %d of palettes; "
                          "writes: %lu KB in %lu blanks, %lu pieces, %lu blanks ran out of time, the latest line %d "
                          "(the last %d)\n",
                     regionTotal / 1024, regionCount, regions[0].texelBytes, regions[1].texelBytes,
                     regions[2].texelBytes, regions[3].texelBytes, skinSlots, static_cast<unsigned long>(skinBytes),
                     paletteRegion.bytes,
                     static_cast<unsigned long>(w.bytes / 1024), static_cast<unsigned long>(w.blanks),
                     static_cast<unsigned long>(w.pieces), static_cast<unsigned long>(w.late), w.mostLine, w.lastLine);
        NEA_TextureStats t;
        NEA_TextureGetStats(&t);
        NEA_PaletteStats p;
        NEA_PaletteGetStats(&p);
        std::fprintf(log, "  vram: %d free in %d pieces, largest %d (A %d, B1 %d, B2 %d, C %d), slots %d/%d, "
                          "materials %d, leaked %d, dangling %d; palettes: %d free in %d, largest %d, slots %d/%d\n",
                     t.free, t.freeChunks, t.largest, t.largestIn[0], t.largestIn[1], t.largestIn[2], t.largestIn[3],
                     t.slotsUsed, t.slotsMax, t.materials, t.leaked, t.dangling, p.free, p.freeChunks, p.largest,
                     p.slotsUsed, p.slotsMax);
        // Which of what is in view is flat, and why.
        int listed = 0;
        for (int id = 0; id < planner.count() && listed < 16; ++id)
        {
            if (!infos[id].streamed || seenAt[id] == 0xFFFFFFFFu || frameNo - seenAt[id] > kSeenFrames ||
                planner.resident(id))
                continue;
            static const char *const kStates[] = {"absent", "staged", "resident", "failed"};
            const TextureRef &ref = level.view.textures[id];
            std::fprintf(log, "  flat %d %.16s %dx%d %s, failed %d times, %lu+%lu bytes, score %lu%s\n", id, ref.name,
                         ref.width, ref.height, kStates[static_cast<int>(planner.state(id)) & 3], failures[id],
                         static_cast<unsigned long>(infos[id].texelBytes),
                         static_cast<unsigned long>(infos[id].paletteBytes),
                         static_cast<unsigned long>(planner.score(id)), ram[id] != nullptr ? ", in RAM" : "");
            ++listed;
        }
        std::fclose(log);
#else
        (void)why;
#endif
    }
} // namespace hlrt
