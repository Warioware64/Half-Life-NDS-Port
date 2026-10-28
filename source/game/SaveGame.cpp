#include "global_header.hpp"

#include "game/SaveGame.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>

#include "Global.hpp"
#include "game/Game.hpp"

namespace hlrt::SaveGame
{
    namespace
    {
        /// The saves' folder on the card.
        struct FatCard : hldata::SaveCard
        {
            std::string path(const char *name) const { return global.savesDir() + name; }

            // The files being written and read a piece at a time (SaveWriter,
            // the maps left: HlMapStore.hpp), kept open from one piece to the
            // next -- opening one is the slow part: one being written, and one
            // being read (a map left's file copied into a save, a save's out
            // into the run's folder: both at once).
            struct Open
            {
                std::FILE *file = nullptr;
                std::string name;
                bool failed = false;
            };
            Open writing_, reading_;
            static bool shut(Open &open)
            {
                if (open.file == nullptr)
                    return true;
                const bool ok = std::fclose(open.file) == 0 && !open.failed;
                open = Open();
                return ok;
            }
            bool close()
            {
                const bool wrote = shut(writing_);
                shut(reading_);
                return wrote;
            }
            /// The folder `name` is to be in, made if it is not there (it
            /// usually is): the saves', or a folder in it (run/).
            void makeFolder(const char *name) const
            {
                std::string dir = global.savesDir();
                if (!dir.empty() && dir.back() == '/')
                    dir.pop_back();
                mkdir(dir.c_str(), 0777);
                const char *slash = std::strrchr(name, '/');
                if (slash != nullptr)
                    mkdir((global.savesDir() + std::string(name, slash)).c_str(), 0777);
            }
            bool append(const char *name, const std::uint8_t *bytes, std::size_t size, bool first) override
            {
                if (first)
                {
                    shut(writing_);
                    if (reading_.name == name)
                        shut(reading_);
                    makeFolder(name);
                    writing_.file = std::fopen(path(name).c_str(), "wb");
                    if (writing_.file == nullptr)
                        return false;
                    writing_.name = name;
                }
                else if (writing_.file == nullptr || writing_.name != name)
                {
                    return false; // (it was let go of in between: begun again by the writer)
                }
                if (std::fwrite(bytes, 1, size, writing_.file) != size)
                {
                    writing_.failed = true;
                    return false;
                }
                return true;
            }
            bool readAt(const char *name, std::size_t offset, std::size_t size, std::vector<std::uint8_t> &out) override
            {
                out.clear();
                if (reading_.file == nullptr || reading_.name != name)
                {
                    shut(reading_);
                    // (A file being written is read back from what is on the
                    // card: let go of first.)
                    if (writing_.name == name && !shut(writing_))
                        return false;
                    reading_.file = std::fopen(path(name).c_str(), "rb");
                    if (reading_.file == nullptr)
                        return false;
                    reading_.name = name;
                }
                if (std::fseek(reading_.file, static_cast<long>(offset), SEEK_SET) != 0)
                    return false;
                out.resize(size);
                out.resize(std::fread(out.data(), 1, size, reading_.file));
                return true;
            }
            bool finish(const char *name) override
            {
                bool ok = true;
                if (writing_.name == name)
                    ok = shut(writing_);
                if (reading_.name == name)
                    shut(reading_);
                return ok;
            }

            bool read(const char *name, std::vector<std::uint8_t> &out, std::size_t most) override
            {
                close();
                out.clear();
                std::FILE *file = std::fopen(path(name).c_str(), "rb");
                if (file == nullptr)
                    return false;
                // (As long as it is, and no longer: a save is tens of
                // kilobytes, the most one may be far more.)
                std::fseek(file, 0, SEEK_END);
                const long length = std::ftell(file);
                std::fseek(file, 0, SEEK_SET);
                const std::size_t want = length < 0 ? 0 : (static_cast<std::size_t>(length) < most ? length : most);
                out.resize(want);
                const std::size_t got = want != 0 ? std::fread(out.data(), 1, want, file) : 0;
                std::fclose(file);
                out.resize(got);
                return got == want;
            }
            bool write(const char *name, const std::uint8_t *bytes, std::size_t size) override
            {
                close();
                makeFolder(name);
                std::FILE *file = std::fopen(path(name).c_str(), "wb");
                if (file == nullptr)
                    return false;
                const bool wrote = std::fwrite(bytes, 1, size, file) == size;
                return std::fclose(file) == 0 && wrote;
            }
            bool remove(const char *name) override
            {
                close();
                return unlink(path(name).c_str()) == 0;
            }
            bool rename(const char *from, const char *to) override
            {
                close();
                return ::rename(path(from).c_str(), path(to).c_str()) == 0;
            }
            /// The files in the saves' folder (one look at the folder, not a
            /// hundred files asked for one by one).
            bool list(std::vector<std::string> &names) override
            {
                close();
                names.clear();
                std::string dir = global.savesDir();
                if (!dir.empty() && dir.back() == '/')
                    dir.pop_back();
                DIR *folder = opendir(dir.c_str());
                if (folder == nullptr)
                    return true; // (no folder: no saves)
                while (const dirent *entry = readdir(folder))
                {
                    if (entry->d_type != DT_DIR)
                        names.emplace_back(entry->d_name);
                }
                closedir(folder);
                return true;
            }
        };
        FatCard card;
        hldata::SaveIndex list;
        hldata::SaveWriter writer;
        bool listed = false;

        void need()
        {
            if (listed || !global.hasFat)
                return;
            hldata::saveIndexLoad(card, list);
            listed = true;
        }

        // The maps left, kept to come back to (HlMapStore.hpp): the index of
        // them, read once; a piece of a file at a time, here.
        hldata::RunIndex run;
        bool runKnown = false;
        std::uint8_t piece[hldata::kMapStorePiece];
        // (What a piece is read into: let go of after each use -- a block
        // left in the middle of the heap is one the next map's level may
        // not have room for, on a DS.)
        std::vector<std::uint8_t> scratch;
        void letGo()
        {
            std::vector<std::uint8_t>().swap(scratch);
        }
        void needRun()
        {
            if (runKnown || !global.hasFat)
                return;
            hldata::runIndexRead(card, run);
            runKnown = true;
        }
    } // namespace

    bool leaveMap(const std::string &name, std::uint32_t level)
    {
        if (!global.hasFat)
            return false;
        finish();
        needRun();
        const std::uint32_t bytes = game.storedBytes();
        return hldata::runStore(card, run, name.c_str(), level, bytes, game.leftWith(), piece,
                                [](hldata::detail::StreamOut &out) { game.storeMap(out); });
    }

    int arriveMap(const std::string &name, std::uint32_t level)
    {
        if (!global.hasFat)
            return 0;
        finish();
        needRun();
        const int k = hldata::runFind(run, name.c_str());
        if (k < 0)
            return 0;
        const hldata::RunEntry entry = run.entries[k];
        int laid = 0;
        if (hldata::runCheck(card, entry, level, scratch))
        {
            hldata::runLay(card, entry, scratch, piece,
                           [&](hldata::detail::StreamIn &in)
                           {
                               laid = game.layStored(in, entry.sentAway, entry.landed) ? 1 : (in.ok ? 0 : -1);
                               return laid > 0;
                           });
        }
        // (Laid, or of no use: either way not kept now. The map the player
        // is in is in memory, and in the saves.)
        hldata::runDrop(card, run, k);
        hldata::runIndexWrite(card, run);
        letGo();
        return laid;
    }

    void landed(const std::string &left, std::uint8_t mask)
    {
        if (!global.hasFat)
            return;
        needRun();
        const int k = hldata::runFind(run, left.c_str());
        if (k < 0 || run.entries[k].landed == mask)
            return;
        run.entries[k].landed = mask;
        hldata::runIndexWrite(card, run);
    }

    void newRun(std::uint32_t runId)
    {
        if (!global.hasFat)
            return;
        finish();
        needRun();
        hldata::runClear(card, run, runId);
    }

    void fromSave(int slot, const hldata::SaveData &save, std::uint32_t runId)
    {
        if (!global.hasFat)
            return;
        finish();
        needRun();
        hldata::runFromSave(card, slot, save, run, runId, scratch);
        letGo();
    }

    const hldata::RunIndex &runIndex()
    {
        needRun();
        return run;
    }

    const hldata::SaveIndex &index()
    {
        need();
        return list;
    }

    bool read(hldata::SaveData &out, int slot)
    {
        if (!global.hasFat)
            return false;
        need();
        finish();
        return hldata::saveSlotRead(card, slot, out);
    }

    bool write(const hldata::SaveData &save, int slot, const hldata::SaveHeading &heading)
    {
        if (!global.hasFat)
            return false;
        need();
        finish();
        needRun();
        // (With the maps left: copied in -- or, for the one coming into a
        // map, only named: the folder changes only at a way out or a load,
        // and either makes that one again.)
        return hldata::saveSlotWrite(card, list, slot, save, heading, &run, slot == hldata::kSlotArrival);
    }

    bool remove(int slot)
    {
        if (!global.hasFat)
            return false;
        need();
        finish();
        return hldata::saveSlotDelete(card, list, slot);
    }

    bool begin(const hldata::SaveData &save, int slot, const hldata::SaveHeading &heading)
    {
        if (!global.hasFat || writer.busy())
            return false;
        need();
        needRun();
        return writer.begin(card, list, slot, save, heading, &run, slot == hldata::kSlotArrival);
    }

    hldata::SaveWriter::Result step()
    {
        return writer.step();
    }

    bool busy()
    {
        return writer.busy();
    }

    bool finish()
    {
        return !writer.busy() || writer.finish();
    }
} // namespace hlrt::SaveGame
