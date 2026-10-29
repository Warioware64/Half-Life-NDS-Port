// (A console that was sent the game only: HLDS_CHILD.)
#ifdef HLDS_CHILD
#include "net/GuestLoad.hpp"

#include <cstdlib>
#include <cstring>
#include <new>

#include "net/AirDevice.hpp"

namespace hlrt::guest
{
    namespace
    {
        struct Kept
        {
            char name[hldata::kGuestNameMost + 1];
            std::uint8_t *data;
            std::uint32_t size;
        };
        constexpr int kBaseMost = 160;
        Kept base[kBaseMost];
        int baseCount = 0;

        struct Offer
        {
            std::string map;
            const std::uint8_t *data = nullptr;
            std::uint32_t size = 0;
            std::string path; ///< or, the file on a card it is in
        };
        constexpr int kOffersMost = 4;
        Offer offers[kOffersMost];
        int offerCount = 0;

        // The map's stream, while it is being read: one at a time.
        MemorySupply mapSupply;
        FileSupply mapFile;
        hldata::GuestCursor *mapCursor = nullptr;
        alignas(4) std::uint8_t cursorRoom[sizeof(hldata::GuestCursor)];
        Held heldNow;

        /// Every member of a stream, each into a block of its own, handed to
        /// `keep` (which owns it from then on, or frees it).
        template <class Keep>
        bool receiveWhole(hldata::GuestSupply &supply, Keep keep)
        {
            hldata::GuestCursor cursor(supply);
            if (!cursor.start())
                return false;
            while (cursor.next())
            {
                const std::uint32_t size = cursor.member().rawSize;
                auto *block = static_cast<std::uint8_t *>(std::malloc(size > 0 ? size : 1));
                if (block == nullptr || cursor.read(block, size) != size)
                {
                    std::free(block);
                    return false;
                }
                keep(cursor.name(), block, size);
            }
            return cursor.ended();
        }
    } // namespace

    bool FileSupply::more(const std::uint8_t *&out, std::size_t &length)
    {
        if (file == nullptr)
            return false;
        length = std::fread(piece, 1, sizeof(piece), file);
        out = piece;
        return length > 0;
    }

    bool MemorySupply::more(const std::uint8_t *&out, std::size_t &length)
    {
        if (at >= size)
            return false;
        out = data + at;
        length = size - at < piece ? size - at : piece;
        at += static_cast<std::uint32_t>(length);
        return true;
    }

    bool receiveCommon(hldata::GuestSupply &supply)
    {
        return receiveWhole(supply, [](const char *name, std::uint8_t *block, std::uint32_t size)
                            {
                                if (!air::putOwned(name, block, size))
                                    std::free(block);
                            });
    }

    void dropCommon() { air::dropOwned(); }

    bool receiveBase(hldata::GuestSupply &supply)
    {
        return receiveWhole(supply, [](const char *name, std::uint8_t *block, std::uint32_t size)
                            {
                                if (baseCount == kBaseMost)
                                {
                                    std::free(block);
                                    return;
                                }
                                Kept &kept = base[baseCount++];
                                std::strncpy(kept.name, name, sizeof(kept.name) - 1);
                                kept.name[sizeof(kept.name) - 1] = '\0';
                                kept.data = block;
                                kept.size = size;
                                heldNow.baseBytes += size;
                            });
    }

    bool borrow(const char *name, const std::uint8_t *&data, std::uint32_t &size)
    {
        for (int i = 0; i < baseCount; ++i)
        {
            if (std::strcmp(base[i].name, name) == 0)
            {
                data = base[i].data;
                size = base[i].size;
                return true;
            }
        }
        return false;
    }

    void offerMap(const std::string &map, const std::uint8_t *data, std::uint32_t size)
    {
        if (offerCount < kOffersMost)
            offers[offerCount++] = Offer{map, data, size, std::string()};
    }

    void offerMapFile(const std::string &map, const std::string &path)
    {
        if (offerCount < kOffersMost)
            offers[offerCount++] = Offer{map, nullptr, 0, path};
    }

    bool offered(const std::string &map)
    {
        for (int i = 0; i < offerCount; ++i)
        {
            if (offers[i].map == map)
                return true;
        }
        return false;
    }

    int offeredCount() { return offerCount; }
    std::string offeredName(int index) { return index >= 0 && index < offerCount ? offers[index].map : std::string(); }

    bool beginMap(const std::string &map)
    {
        endMap();
        for (int i = 0; i < offerCount; ++i)
        {
            if (offers[i].map != map)
                continue;
            hldata::GuestSupply *supply = &mapSupply;
            if (!offers[i].path.empty())
            {
                mapFile.file = std::fopen(offers[i].path.c_str(), "rb");
                if (mapFile.file == nullptr)
                    return false;
                supply = &mapFile;
            }
            else
            {
                mapSupply = MemorySupply();
                mapSupply.data = offers[i].data;
                mapSupply.size = offers[i].size;
            }
            mapCursor = new (cursorRoom) hldata::GuestCursor(*supply);
            if (!mapCursor->start())
            {
                endMap();
                return false;
            }
            for (std::uint32_t &bytes : heldNow.mapBytes)
                bytes = 0;
            air::stream(mapCursor);
            return true;
        }
        return false;
    }

    void endMap()
    {
        air::stream(nullptr);
        if (mapCursor != nullptr)
            mapCursor->~GuestCursor();
        mapCursor = nullptr;
        if (mapFile.file != nullptr)
            std::fclose(mapFile.file);
        mapFile.file = nullptr;
    }

    void count(hldata::GuestKind kind, std::uint32_t bytes) { heldNow.mapBytes[static_cast<int>(kind)] += bytes; }

    const Held &held() { return heldNow; }
} // namespace hlrt::guest
#endif
