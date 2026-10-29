#include "data/LevelFile.hpp"
#include "data/CardRead.hpp"

#include "DebugLog.hpp"
#include "Global.hpp"
#include "global_header.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hlrt
{
    Level level;

    void Level::unload()
    {
        view = hldata::LevelView();
        std::free(block_);
        block_ = nullptr;
        size_ = 0;
    }

    bool Level::peekHeader(const std::string &name, hldata::LevelHeader &header)
    {
        // (A drive of its own, for the length of one read: the map being
        // played stays mounted, and may be this very one.)
        if (!global.hasFat || NEA_NpacMount("peek", global.mapArchive(name).c_str()) != 0)
            return false;
        std::FILE *file = std::fopen("peek:/level.0", "rb");
        const bool got = file != nullptr && std::fread(&header, sizeof(header), 1, file) == 1;
        if (file != nullptr)
            std::fclose(file);
        NEA_NpacUnmount("peek");
        return got &&
               std::memcmp(header.magic, hldata::kLevelMagic, sizeof(hldata::kLevelMagic)) == 0 &&
               hldata::levelVersionOk(header.version, header.minReader);
    }

    bool Level::load(std::string &error)
    {
        unload();

        // The first piece says how long the level is: the card's word, so
        // it is checked before it is trusted with an allocation. No cooked
        // map is anywhere near 8 MB.
        hldata::LevelHeader header{};
        std::FILE *file = std::fopen(global.mapPath("level.0").c_str(), "rb");
        const bool got = file != nullptr && std::fread(&header, sizeof(header), 1, file) == 1;
        if (file != nullptr)
            std::fclose(file);
        if (!got)
        {
            error = "Map not found on the card.";
            return false;
        }
        const std::uint32_t size = header.fileSize;
        if (size < hldata::kLevelDataStart || size > 8 * 1024 * 1024)
        {
            error = "Map file is damaged. Convert again.";
            return false;
        }

        block_ = static_cast<std::uint8_t *>(std::malloc(size));
        if (block_ == nullptr)
        {
            error = "Not enough memory for this map.";
            return false;
        }
        // Piece by piece, each to its place. (A compressed piece is inflated
        // by the archive as it is opened, beside this block, and let go
        // when it is closed: that is why a level is in pieces.)
        for (std::uint32_t piece = 0; piece < hldata::levelPieces(size); ++piece)
        {
            char name[16];
            std::snprintf(name, sizeof(name), "level.%u", static_cast<unsigned>(piece));
            const std::uint32_t bytes = hldata::levelPieceBytes(size, piece);
            file = std::fopen(global.mapPath(name).c_str(), "rb");
            const bool read = file != nullptr &&
                              cardRead(block_ + hldata::levelPieceStart(piece), bytes, file) == bytes;
            if (file != nullptr)
                std::fclose(file);
            if (!read)
            {
                unload();
                error = "Map could not be read from the card.";
                return false;
            }
        }

#ifdef HLDS_PROFILE
        const std::uint32_t before = DebugLog::Perf::now();
#endif
        const hldata::LevelError result = view.open(block_, size);
#ifdef HLDS_PROFILE
        checkMs = static_cast<std::uint32_t>((std::uint64_t(DebugLog::Perf::now() - before) * 2u) / 67000u);
#endif
        if (result != hldata::LevelError::Ok)
        {
            unload();
            if (result == hldata::LevelError::BadVersion)
            {
                char text[96];
                std::snprintf(text, sizeof(text), "Map is format %u (needs %u to %u). Convert again.",
                              static_cast<unsigned>(header.version), static_cast<unsigned>(hldata::kLevelMinVersion),
                              static_cast<unsigned>(hldata::kLevelVersion));
                error = text;
            }
            else
            {
                error = "Map file is damaged. Convert again.";
            }
            return false;
        }
        size_ = size;
        return true;
    }
} // namespace hlrt
