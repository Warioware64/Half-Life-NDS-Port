#include "hlds/Pack.hpp"

#include <algorithm>
#include <cstring>
#include <set>

#include "HlLevelReader.hpp"
#include "HlModel.hpp"
#include "HlSound.hpp"
#include "HlSprite.hpp"
#include "HlUi.hpp"
#include "hlds/AtomicWrite.hpp"
#include "hlds/NpacWriter.hpp"

namespace fs = std::filesystem;

namespace hlds
{
    namespace
    {
        struct Packing
        {
            std::vector<NpacMember> members;
            std::set<std::string> named;
            PackReport *report = nullptr;

            /// False if the file is not in the stage (and that is counted,
            /// unless it was only something to try).
            bool add(const fs::path &file, const std::string &path, bool mayCompress, bool expected = true)
            {
                if (named.count(path) != 0)
                    return true;
                NpacMember member;
                std::string ignored;
                if (!readFile(file, member.data, ignored))
                {
                    if (expected)
                        ++report->missing;
                    return false;
                }
                member.path = path;
                member.mayCompress = mayCompress;
                named.insert(path);
                members.push_back(std::move(member));
                return true;
            }
        };

        bool finish(Packing &packing, const fs::path &out, bool compress, std::string &error)
        {
            std::vector<std::uint8_t> archive;
            if (!buildNpac(packing.members, compress, archive, error))
                return false;
            // What was just built is read back before it is put on the card.
            std::vector<NpacMember> back;
            if (!readNpac(archive, back, error) || back.size() != packing.members.size())
            {
                error = "the archive does not read back: " + error;
                return false;
            }
            PackReport &report = *packing.report;
            for (std::size_t i = 0; i < back.size(); ++i)
            {
                if (back[i].path != packing.members[i].path || back[i].data != packing.members[i].data)
                {
                    error = "the archive does not read back: " + packing.members[i].path;
                    return false;
                }
                ++report.members;
                report.compressed += back[i].method != kNpacStored ? 1 : 0;
                report.rawBytes += back[i].data.size();
            }
            report.cardBytes = archive.size();
            return writeFileAtomic(out, archive, error);
        }
    } // namespace

        /// The sounds that are only words of what the people say (the talk
    /// table after the soldiers' in Section::Speech): the same in every
    /// map that has them, so kept once, in the common archive, which a
    /// word not in the map's own is read from.
    void talkWords(const hldata::LevelView &view, std::vector<char> &word)
    {
        word.assign(view.soundCount + 1, 0);
        const std::uint32_t start = hldata::kTalkTableAt + hldata::kTalkHeading;
        for (std::uint32_t i = start; i < view.speechCount; ++i)
        {
            const hldata::SentenceRecord &sentence = view.sentences[view.speech[i]];
            for (int w = 0; w < sentence.wordCount; ++w)
            {
                const std::uint32_t sound = view.sentenceWords[sentence.firstWord + w].sound;
                if (sound < view.soundCount)
                    word[sound] = 1;
            }
        }
    }


    bool packMap(const fs::path &stage, const fs::path &card, const std::string &map, bool compress,
                 PackReport &report, std::string &error)
    {
        report = PackReport();
        std::vector<std::uint8_t> level;
        if (!readFile(stage / "maps" / (map + ".hlm"), level, error))
            return false;
        hldata::LevelView view;
        if (view.open(level.data(), static_cast<std::uint32_t>(level.size())) != hldata::LevelError::Ok)
        {
            error = "the staged level does not open: " + map;
            return false;
        }
        Packing packing;
        packing.report = &report;

        // The level, in pieces (HlLevel.hpp).
        const std::uint32_t size = static_cast<std::uint32_t>(level.size());
        for (std::uint32_t piece = 0; piece < hldata::levelPieces(size); ++piece)
        {
            NpacMember member;
            member.path = "level." + std::to_string(piece);
            const std::uint32_t start = hldata::levelPieceStart(piece);
            member.data.assign(level.begin() + start,
                               level.begin() + start + hldata::levelPieceBytes(size, piece));
            packing.named.insert(member.path);
            packing.members.push_back(std::move(member));
        }

        for (std::uint32_t i = 0; i < view.textureCount; ++i)
        {
            const hldata::TextureRef &ref = view.textures[i];
            char name[40];
            hldata::textureFileName(ref.name, ref.width, ref.height,
                                    (ref.flags & hldata::kTextureA3I5) != 0, name);
            packing.add(stage / "tex" / name, std::string("tex/") + name, true);
        }

        for (std::uint32_t i = 0; i < view.spriteCount; ++i)
        {
            const hldata::SpriteRef &ref = view.sprites[i];
            if (ref.frames == 0)
                continue;
            const std::string name = hldata::spriteFile(ref.name, ref.width, ref.height);
            packing.add(stage / "spr" / name, "spr/" + name, true);
        }

        // Models as a DS reads them, their skins, then the files a DSi reads
        // instead where there are any.
        std::vector<std::string> skins;
        auto skinOf = [&](const NpacMember &model, const char *name)
        {
            if (model.data.size() < sizeof(hldata::ModelHeader))
                return;
            hldata::ModelHeader header;
            std::memcpy(&header, model.data.data(), sizeof(header));
            if (header.atlasWidth != 0)
                skins.push_back(hldata::modelSkinFile(name, header.atlasWidth, header.atlasHeight));
        };
        // (Not a weapon the map has not: that is in the common archive.)
        for (std::uint32_t i = 0; i < view.modelRefCount; ++i)
        {
            const char *name = view.modelRefs[i].name;
            if (hldata::modelRefLazy(view.modelRefs[i]))
                continue;
            if (packing.add(stage / "mdl" / map / (std::string(name) + ".hmd"),
                            std::string("mdl/") + name + ".hmd", true))
                skinOf(packing.members.back(), name);
        }
        const std::size_t skinsAt = packing.members.size();
        for (std::uint32_t i = 0; i < view.modelRefCount; ++i)
        {
            const char *name = view.modelRefs[i].name;
            if (hldata::modelRefLazy(view.modelRefs[i]))
                continue;
            if (packing.add(stage / "mdl" / map / (std::string(name) + ".dsi.hmd"),
                            std::string("mdl/") + name + ".dsi.hmd", true, false))
                skinOf(packing.members.back(), name);
        }
        // (The skins go between the two: named now that both kinds have
        // said which they want.)
        {
            Packing skinsOnly;
            skinsOnly.report = &report;
            skinsOnly.named = packing.named;
            for (const std::string &skin : skins)
                skinsOnly.add(stage / "mdl" / skin, "mdl/" + skin, true);
            packing.members.insert(packing.members.begin() + skinsAt,
                                   std::make_move_iterator(skinsOnly.members.begin()),
                                   std::make_move_iterator(skinsOnly.members.end()));
        }

        // Sounds: those loaded with the map first (a DSi's better copy
        // beside the plain one), then the words read as they are spoken.
        // Not compressed: they are compressed already, as sound.
        std::vector<char> kept(view.soundCount + 1, 0);
        hldata::soundsKept(view.logic, view.logicCount, view.logicSounds, view.logicSoundCount,
                           view.soundCount, reinterpret_cast<bool *>(kept.data()), view.modelSounds,
                           view.modelSoundCount);
        std::vector<char> talk;
        talkWords(view, talk);
        for (int pass = 0; pass < 2; ++pass)
        {
            for (std::uint32_t i = 0; i < view.soundCount; ++i)
            {
                const hldata::SoundRef &ref = view.sounds[i];
                if (ref.name[0] == '\0' || (kept[i] != 0) != (pass == 0) || hldata::soundLazy(ref))
                    continue;
                // (What the people say is in the common archive.)
                if (pass == 1 && talk[i] != 0)
                    continue;
                char twin[sizeof(hldata::SoundRef)];
                if (pass == 0 && hldata::soundTwinName(ref, twin, sizeof(twin)))
                    packing.add(stage / "snd" / (std::string(twin) + ".hsn"),
                                std::string("snd/") + twin + ".hsn", false, false);
                packing.add(stage / "snd" / (std::string(ref.name) + ".hsn"),
                            std::string("snd/") + ref.name + ".hsn", false);
            }
        }

        return finish(packing, card / "maps" / (map + ".npac"), compress, error);
    }

    bool packCommon(const fs::path &stage, const fs::path &card, bool compress, PackReport &report,
                    std::string &error)
    {
        report = PackReport();
        Packing packing;
        packing.report = &report;
        // (Sorted: a folder is listed in whatever order the disk likes.)
        std::vector<fs::path> files;
        std::error_code ec;
        for (auto it = fs::directory_iterator(stage / "ui", ec); !ec && it != fs::directory_iterator();
             it.increment(ec))
        {
            if (it->is_regular_file(ec) && it->path().extension() != ".tmp")
                files.push_back(it->path());
        }
        std::sort(files.begin(), files.end());
        for (const fs::path &file : files)
            packing.add(file, "ui/" + file.filename().string(), true);
        for (const char *name : hldata::kUiSoundFiles)
            packing.add(stage / "snd" / (std::string(name) + ".hsn"), std::string("snd/") + name + ".hsn",
                        false, false);
        // The words of what the people say, of every map staged; and the
        // weapons some map has not, with their sounds (kModelRefLazy).
        {
            std::set<std::string> words, weapons, weaponSounds;
            std::vector<fs::path> levels;
            ec.clear();
            for (auto it = fs::directory_iterator(stage / "maps", ec); !ec && it != fs::directory_iterator();
                 it.increment(ec))
            {
                if (it->path().extension() == ".hlm")
                    levels.push_back(it->path());
            }
            ec.clear();
            for (const fs::path &file : levels)
            {
                std::vector<std::uint8_t> level;
                std::string ignored;
                hldata::LevelView view;
                if (!readFile(file, level, ignored) ||
                    view.open(level.data(), static_cast<std::uint32_t>(level.size())) != hldata::LevelError::Ok)
                    continue;
                std::vector<char> talk;
                talkWords(view, talk);
                for (std::uint32_t i = 0; i < view.soundCount; ++i)
                {
                    if (talk[i] != 0 && view.sounds[i].name[0] != '\0')
                        words.insert(view.sounds[i].name);
                    if (hldata::soundLazy(view.sounds[i]) && view.sounds[i].name[0] != '\0')
                        weaponSounds.insert(view.sounds[i].name);
                }
                for (std::uint32_t i = 0; i < view.modelRefCount; ++i)
                {
                    if (hldata::modelRefLazy(view.modelRefs[i]))
                        weapons.insert(view.modelRefs[i].name);
                }
            }
            for (const std::string &name : words)
                packing.add(stage / "snd" / (name + ".hsn"), "snd/" + name + ".hsn", false, false);
            for (const std::string &name : weaponSounds)
                packing.add(stage / "snd" / (name + ".hsn"), "snd/" + name + ".hsn", false, false);
            // Each weapon as a DS reads it, a DSi's beside it, and its skin.
            std::set<std::string> skins;
            for (const std::string &name : weapons)
            {
                for (const char *kind : {".hmd", ".dsi.hmd"})
                {
                    const std::size_t before = packing.members.size();
                    packing.add(stage / "wpn" / (name + kind), "wpn/" + name + kind, true, kind[1] == 'h');
                    if (packing.members.size() == before ||
                        packing.members.back().data.size() < sizeof(hldata::ModelHeader))
                        continue;
                    hldata::ModelHeader header;
                    std::memcpy(&header, packing.members.back().data.data(), sizeof(header));
                    if (header.atlasWidth != 0)
                        skins.insert(hldata::modelSkinFile(name.c_str(), header.atlasWidth, header.atlasHeight));
                }
            }
            for (const std::string &skin : skins)
                packing.add(stage / "mdl" / skin, "wpn/" + skin, true);
        }
        if (packing.members.empty())
        {
            // Nothing of the game's own to show: the console does without,
            // as it did when these were loose files that were not there.
            std::error_code gone;
            fs::remove(card / "common.npac", gone);
            return true;
        }
        return finish(packing, card / "common.npac", compress, error);
    }

    bool unpackLevel(const fs::path &archive, std::vector<std::uint8_t> &level,
                     std::size_t &archiveBytes, std::string &error)
    {
        level.clear();
        std::vector<std::uint8_t> bytes;
        std::vector<NpacMember> members;
        if (!readFile(archive, bytes, error) || !readNpac(bytes, members, error))
            return false;
        archiveBytes = bytes.size();
        for (std::size_t piece = 0; piece < members.size(); ++piece)
        {
            if (members[piece].path != "level." + std::to_string(piece))
                break;
            level.insert(level.end(), members[piece].data.begin(), members[piece].data.end());
        }
        if (level.size() < sizeof(hldata::LevelHeader))
        {
            error = "the archive holds no level";
            return false;
        }
        return true;
    }
} // namespace hlds
