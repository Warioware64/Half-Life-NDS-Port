#include "hlds/Pack.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <set>

#include "HlGuest.hpp"
#include "HlLevelReader.hpp"
#include "HlModel.hpp"
#include "HlSound.hpp"
#include "HlSprite.hpp"
#include "HlUi.hpp"
#include "hlds/AtomicWrite.hpp"
#include "hlds/Lz77.hpp"
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

        // (Beside the kinds, in a report's `raw`: what is kept packed, and the
        // biggest of it undone.)
        constexpr int kGuestPacked = static_cast<int>(hldata::GuestKind::Count);
        constexpr int kGuestUnpackedMost = kGuestPacked + 1;

        /// A guest stream (HlGuest.hpp) of `members`, in their order: each
        /// LZ77 where that makes it a good deal smaller (not a sound: they
        /// are compressed already, as sound). What each kind comes to,
        /// undone, into `raw`.
        bool writeGuest(const std::vector<NpacMember> &members, const fs::path &out, std::size_t *raw,
                        std::size_t &streamBytes, std::string &error)
        {
            std::vector<std::uint8_t> stream(sizeof(hldata::GuestHeader));
            hldata::GuestHeader header{};
            std::memcpy(header.magic, hldata::kGuestMagic, 4);
            header.version = hldata::kGuestVersion;
            header.levelVersion = hldata::kLevelVersion;
            for (const NpacMember &member : members)
            {
                if (member.path.size() > hldata::kGuestNameMost)
                {
                    error = "a name too long for a guest stream: " + member.path;
                    return false;
                }
                const hldata::GuestKind kind = hldata::guestKindOf(member.path.c_str());
                // (A weapon lying about is kept packed: LZ77, however little
                // that saves.)
                const bool keepPacked = member.path.rfind("wpn/", 0) == 0;
                std::vector<std::uint8_t> packed;
                if (kind != hldata::GuestKind::Sound && member.data.size() >= 256)
                {
                    packed = lz77Compress(member.data.data(), member.data.size());
                    if (!keepPacked && packed.size() * 8 > member.data.size() * 7)
                        packed.clear();
                }
                hldata::GuestMember record{};
                record.kind = static_cast<std::uint8_t>(kind);
                record.flags = static_cast<std::uint8_t>((packed.empty() ? 0 : hldata::kGuestLz77) |
                                                         (keepPacked && !packed.empty() ? hldata::kGuestKeepPacked : 0));
                record.nameBytes = static_cast<std::uint16_t>(member.path.size());
                record.rawSize = static_cast<std::uint32_t>(member.data.size());
                const std::vector<std::uint8_t> &bytes = packed.empty() ? member.data : packed;
                record.packedSize = static_cast<std::uint32_t>(bytes.size());
                const auto *r = reinterpret_cast<const std::uint8_t *>(&record);
                stream.insert(stream.end(), r, r + sizeof(record));
                stream.insert(stream.end(), member.path.begin(), member.path.end());
                stream.insert(stream.end(), bytes.begin(), bytes.end());
                ++header.members;
                header.rawBytes += record.rawSize;
                // (What a weapon kept packed takes of the memory is what it
                // came as; and while it is in hand, itself besides.)
                if (raw != nullptr && (record.flags & hldata::kGuestKeepPacked))
                {
                    raw[kGuestPacked] += record.packedSize;
                    if (record.rawSize > raw[kGuestUnpackedMost])
                        raw[kGuestUnpackedMost] = record.rawSize;
                }
                else if (raw != nullptr)
                    raw[static_cast<int>(kind)] += member.data.size();
            }
            header.streamBytes = static_cast<std::uint32_t>(stream.size());
            std::memcpy(stream.data(), &header, sizeof(header));
            streamBytes = stream.size();
            return writeFileAtomic(out, stream, error);
        }

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


    namespace
    {
        /// Everything a map's archive holds, in the order the console loads
        /// it. `air`: only what a DS reads (a console sent the game over the
        /// air runs as one) -- no DSi model, no DSi copy of a sound.
        bool collectMap(const fs::path &stage, const std::string &map, bool air, Packing &packing,
                        std::string &error)
        {
            std::vector<std::uint8_t> level;
            if (!readFile(stage / "maps" / (map + ".hlm"), level, error))
                return false;
            hldata::LevelView view;
            if (view.open(level.data(), static_cast<std::uint32_t>(level.size())) != hldata::LevelError::Ok)
            {
                error = "the staged level does not open: " + map;
                return false;
            }

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
            for (std::uint32_t i = 0; i < view.modelRefCount && !air; ++i)
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
                skinsOnly.report = packing.report;
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
                    if (pass == 0 && !air && hldata::soundTwinName(ref, twin, sizeof(twin)))
                        packing.add(stage / "snd" / (std::string(twin) + ".hsn"),
                                    std::string("snd/") + twin + ".hsn", false, false);
                    packing.add(stage / "snd" / (std::string(ref.name) + ".hsn"),
                                std::string("snd/") + ref.name + ".hsn", false);
                }
            }
            return true;
        }
    } // namespace

    namespace
    {
        /// The level a console sent the map for a game between consoles is
        /// given: the host's own, index for index -- every record where it was,
        /// for what they say to each other names records by number -- with
        /// only what it has no use for taken out:
        ///  - the hull of something large (a bullsquid's: the world's hull
        ///    2), which only the monsters walk by, where nothing else reaches
        ///    into its nodes;
        ///  (Its sounds are the host's own, at the host's own rate: a guest
        ///  never carries more of one than the host.)
        bool guestLevel(std::vector<std::uint8_t> &level)
        {
            if (level.size() < hldata::kLevelDataStart)
                return false;
            hldata::LevelHeader header;
            std::memcpy(&header, level.data(), sizeof(header));
            auto *dir = reinterpret_cast<hldata::SectionDir *>(level.data() + sizeof(hldata::LevelHeader));
            const auto at = [&](hldata::Section s) -> hldata::SectionDir & { return dir[static_cast<int>(s)]; };
            // The large hull's nodes, where they are its alone.
            std::vector<std::vector<std::uint8_t>> bytes(hldata::kSectionCount);
            for (int s = 0; s < hldata::kSectionCount; ++s)
                bytes[s].assign(level.begin() + dir[s].offset, level.begin() + dir[s].offset + dir[s].size);
            {
                auto *models = reinterpret_cast<hldata::Model *>(bytes[static_cast<int>(hldata::Section::Models)].data());
                const std::uint32_t modelCount = at(hldata::Section::Models).count;
                std::vector<hldata::ClipNode> nodes(at(hldata::Section::ClipNodes).count);
                std::memcpy(nodes.data(), bytes[static_cast<int>(hldata::Section::ClipNodes)].data(),
                            nodes.size() * sizeof(hldata::ClipNode));
                const std::int32_t root = modelCount > 0 ? models[0].clipLarge : -1;
                std::vector<char> large(nodes.size(), 0);
                bool alone = root >= 0 && static_cast<std::size_t>(root) < nodes.size();
                if (alone)
                {
                    std::vector<std::int32_t> open{root};
                    while (!open.empty())
                    {
                        const std::int32_t n = open.back();
                        open.pop_back();
                        if (n < 0 || large[static_cast<std::size_t>(n)])
                            continue;
                        large[static_cast<std::size_t>(n)] = 1;
                        for (std::int16_t child : nodes[static_cast<std::size_t>(n)].children)
                            open.push_back(child);
                    }
                    for (std::size_t n = 0; n < nodes.size() && alone; ++n)
                    {
                        for (std::int16_t child : nodes[n].children)
                            alone = alone && (large[n] || child < 0 || !large[static_cast<std::size_t>(child)]);
                    }
                    for (std::uint32_t m = 0; m < modelCount && alone; ++m)
                    {
                        for (std::int32_t r : {models[m].clipStanding, models[m].clipCrouched})
                            alone = alone && (r < 0 || !large[static_cast<std::size_t>(r)]);
                    }
                }
                if (alone)
                {
                    std::vector<std::int32_t> moved(nodes.size(), -1);
                    std::vector<hldata::ClipNode> kept;
                    for (std::size_t n = 0; n < nodes.size(); ++n)
                    {
                        if (!large[n])
                        {
                            moved[n] = static_cast<std::int32_t>(kept.size());
                            kept.push_back(nodes[n]);
                        }
                    }
                    for (hldata::ClipNode &node : kept)
                    {
                        for (std::int16_t &child : node.children)
                            child = child < 0 ? child : static_cast<std::int16_t>(moved[static_cast<std::size_t>(child)]);
                    }
                    for (std::uint32_t m = 0; m < modelCount; ++m)
                    {
                        for (std::int32_t *r : {&models[m].clipStanding, &models[m].clipCrouched})
                            *r = *r < 0 ? *r : moved[static_cast<std::size_t>(*r)];
                    }
                    models[0].clipLarge = hldata::kContentsEmpty;
                    auto &clip = bytes[static_cast<int>(hldata::Section::ClipNodes)];
                    clip.assign(reinterpret_cast<const std::uint8_t *>(kept.data()),
                                reinterpret_cast<const std::uint8_t *>(kept.data() + kept.size()));
                    at(hldata::Section::ClipNodes).count = static_cast<std::uint32_t>(kept.size());
                }
            }
            // Laid out again, each section where its turn puts it, as the
            // writer does (LevelWriter.cpp): in the order they were, on words.
            std::vector<int> order(hldata::kSectionCount);
            for (int s = 0; s < hldata::kSectionCount; ++s)
                order[s] = s;
            std::sort(order.begin(), order.end(), [&](int a, int b) { return dir[a].offset < dir[b].offset; });
            std::vector<std::uint8_t> out(level.begin(), level.begin() + hldata::kLevelDataStart);
            auto *outDir = reinterpret_cast<hldata::SectionDir *>(out.data() + sizeof(hldata::LevelHeader));
            for (int s : order)
            {
                while (out.size() % 4 != 0)
                    out.push_back(0);
                outDir = reinterpret_cast<hldata::SectionDir *>(out.data() + sizeof(hldata::LevelHeader));
                outDir[s].offset = static_cast<std::uint32_t>(out.size());
                outDir[s].size = static_cast<std::uint32_t>(bytes[s].size());
                outDir[s].count = dir[s].count;
                out.insert(out.end(), bytes[s].begin(), bytes[s].end());
            }
            while (out.size() % 4 != 0)
                out.push_back(0);
            header.fileSize = static_cast<std::uint32_t>(out.size());
            std::memcpy(out.data(), &header, sizeof(header));
            header.crc32 = hldata::crc32(out.data() + sizeof(header), static_cast<std::uint32_t>(out.size() - sizeof(header)));
            std::memcpy(out.data(), &header, sizeof(header));
            hldata::LevelView check;
            if (check.open(out.data(), static_cast<std::uint32_t>(out.size())) != hldata::LevelError::Ok)
                return false;
            level = std::move(out);
            return true;
        }

        /// What of a map a console sent it for a game between consoles is
        /// given -- what the match can show and play there, and nothing more:
        ///  - the guns it can be held: the crowbar and the 9 mm everyone
        ///    starts with, and those lying about to be taken up. The other
        ///    first six go (their models, skins, sounds); those that came
        ///    later, the map has lying about, come -- kept packed ("wpn/"), as
        ///    they are loaded only when taken up;
        ///  - its sounds kept loaded, less those of guns it has not, of
        ///    ladders and water it has not, and the suit's words (the game's
        ///    SetSuitUpdate: "We don't play HEV sounds in multiplayer"); none
        ///    of what is only spoken.
        bool guestMatch(const fs::path &stage, const std::string &map, Packing &packing, std::string &error)
        {
            std::vector<std::uint8_t> level;
            hldata::LevelView view;
            if (!readFile(stage / "maps" / (map + ".hlm"), level, error) || !guestLevel(level) ||
                view.open(level.data(), static_cast<std::uint32_t>(level.size())) != hldata::LevelError::Ok)
            {
                error = "the staged level does not open, or its guest's does not: " + map;
                return false;
            }
            // (The guest's level, in pieces, where the host's were.)
            {
                std::vector<NpacMember> pieces;
                const std::uint32_t size = static_cast<std::uint32_t>(level.size());
                for (std::uint32_t piece = 0; piece < hldata::levelPieces(size); ++piece)
                {
                    NpacMember member;
                    member.path = "level." + std::to_string(piece);
                    const std::uint32_t start = hldata::levelPieceStart(piece);
                    member.data.assign(level.begin() + start, level.begin() + start + hldata::levelPieceBytes(size, piece));
                    pieces.push_back(std::move(member));
                }
                std::vector<NpacMember> rest;
                for (NpacMember &member : packing.members)
                {
                    if (member.path.rfind("level.", 0) != 0)
                        rest.push_back(std::move(member));
                }
                pieces.insert(pieces.end(), std::make_move_iterator(rest.begin()), std::make_move_iterator(rest.end()));
                packing.members = std::move(pieces);
            }
            std::set<std::string> held = {"v_crowbar", "v_9mmhandgun"};
            for (std::uint32_t i = 0; i < view.actorCount; ++i)
            {
                const hldata::ActorRecord &actor = view.actors[i];
                if (actor.kind != static_cast<std::uint8_t>(hldata::ActorKind::Item))
                    continue;
                if (const char *model = hldata::itemViewModel(static_cast<hldata::ItemKind>(actor.item & 0xFF)))
                    held.insert(model);
            }
            if (held.count("v_satchel") != 0)
                held.insert("v_satchel_radio");
            // Sounds: what a match plays there.
            std::vector<char> kept(view.soundCount + 1, 0);
            hldata::soundsKept(view.logic, view.logicCount, view.logicSounds, view.logicSoundCount, view.soundCount,
                               reinterpret_cast<bool *>(kept.data()), view.modelSounds, view.modelSoundCount);
            auto drop = [&](hldata::CommonSound first, hldata::CommonSound last)
            {
                for (int i = static_cast<int>(first); i <= static_cast<int>(last) && i < static_cast<int>(view.soundCount);
                     ++i)
                    kept[static_cast<std::size_t>(i)] = 0;
            };
            for (const hldata::GunSounds &gun : hldata::kGunSounds)
            {
                if (held.count(gun.viewModel) == 0)
                    drop(gun.first, gun.last);
            }
            bool ladder = false, water = false;
            for (std::uint32_t i = 0; i < view.logicCount; ++i)
                ladder = ladder || view.logic[i].kind == static_cast<std::uint8_t>(hldata::LogicKind::Ladder);
            for (std::uint32_t i = 0; i < view.leafCount; ++i)
            {
                const std::int8_t c = view.leaves[i].contents;
                water = water || c == hldata::kContentsWater || c == hldata::kContentsSlime || c == hldata::kContentsLava;
            }
            for (std::uint32_t i = 0; i < view.brushCount; ++i)
                water = water || view.brushes[i].kind != 0;
            if (!ladder)
                drop(hldata::CommonSound::Ladder1, hldata::CommonSound::Ladder4);
            if (!water)
                drop(hldata::CommonSound::Wade1, hldata::CommonSound::Slosh4);
            std::set<std::string> sounds;
            for (std::uint32_t i = 0; i < view.soundCount; ++i)
            {
                if (kept[i] != 0 && view.sounds[i].name[0] != '\0' && !hldata::soundLazy(view.sounds[i]))
                    sounds.insert(std::string("snd/") + view.sounds[i].name + ".hsn");
            }
            // The models and skins of the first six it has not.
            std::set<std::string> gone;
            for (const NpacMember &member : packing.members)
            {
                if (member.path.rfind("mdl/v_", 0) != 0)
                    continue;
                const std::string name = member.path.substr(4, member.path.find('.') - 4);
                if (held.count(name) == 0)
                    gone.insert(member.path);
            }
            // (The sounds again, in the level's order: the order the console
            // asks for them in.)
            std::vector<NpacMember> kept_;
            for (NpacMember &member : packing.members)
            {
                if (member.path.rfind("snd/", 0) != 0 && gone.count(member.path) == 0)
                    kept_.push_back(std::move(member));
            }
            std::set<std::string> listed;
            for (std::uint32_t i = 0; i < view.soundCount; ++i)
            {
                const std::string path = std::string("snd/") + view.sounds[i].name + ".hsn";
                if (sounds.count(path) == 0 || !listed.insert(path).second)
                    continue;
                NpacMember member;
                std::string ignored;
                member.path = path;
                member.mayCompress = false;
                if (readFile(stage / "snd" / (std::string(view.sounds[i].name) + ".hsn"), member.data, ignored))
                    kept_.push_back(std::move(member));
            }
            packing.members = std::move(kept_);
            // The guns held, as a guest has them: the lighter cook (stage
            // gst/, the same on every map -- so in the base), with its skin.
            auto skinOf = [](const std::vector<std::uint8_t> &model, const std::string &name)
            {
                if (model.size() < sizeof(hldata::ModelHeader))
                    return std::string();
                hldata::ModelHeader header;
                std::memcpy(&header, model.data(), sizeof(header));
                return header.atlasWidth == 0 ? std::string()
                                              : hldata::modelSkinFile(name.c_str(), header.atlasWidth, header.atlasHeight);
            };
            for (std::size_t at = 0; at < packing.members.size(); ++at)
            {
                NpacMember &member = packing.members[at];
                if ((member.path.rfind("mdl/v_", 0) != 0 && member.path != "mdl/player.hmd") ||
                    member.path.size() < 8 || member.path.compare(member.path.size() - 4, 4, ".hmd") != 0)
                    continue;
                const std::string name = member.path.substr(4, member.path.size() - 8);
                std::vector<std::uint8_t> lean;
                std::string ignored;
                if (!readFile(stage / "gst" / (name + ".hmd"), lean, ignored))
                    continue;
                const std::string oldSkin = skinOf(member.data, name), newSkin = skinOf(lean, name);
                member.data = std::move(lean);
                for (NpacMember &skin : packing.members)
                {
                    if (skin.path != "mdl/" + oldSkin)
                        continue;
                    // (The player's in 256 colours, its own, where the stage has it.)
                    std::vector<std::uint8_t> bytes;
                    if (!newSkin.empty() &&
                        (readFile(stage / "gst" / newSkin, bytes, ignored) || readFile(stage / "mdl" / newSkin, bytes, ignored)))
                    {
                        skin.path = "mdl/" + newSkin;
                        skin.data = std::move(bytes);
                    }
                    break;
                }
            }
            // ... and those that came later, it has lying about: packed, last.
            for (std::uint32_t i = 0; i < view.modelRefCount; ++i)
            {
                const char *name = view.modelRefs[i].name;
                if (!hldata::modelRefLazy(view.modelRefs[i]) || held.count(name) == 0)
                    continue;
                // (The lighter cook, if the stage has it; else the card's.)
                const fs::path lean = stage / "gst" / (std::string(name) + ".hmd");
                if (!packing.add(fs::exists(lean) ? lean : stage / "wpn" / (std::string(name) + ".hmd"),
                                 std::string("wpn/") + name + ".hmd", true))
                    continue;
                const NpacMember &model = packing.members.back();
                if (model.data.size() < sizeof(hldata::ModelHeader))
                    continue;
                hldata::ModelHeader header;
                std::memcpy(&header, model.data.data(), sizeof(header));
                if (header.atlasWidth != 0)
                {
                    const std::string skin = hldata::modelSkinFile(name, header.atlasWidth, header.atlasHeight);
                    packing.add(stage / "mdl" / skin, "wpn/" + skin, true);
                }
            }
            return true;
        }
    } // namespace

    bool packMap(const fs::path &stage, const fs::path &card, const std::string &map, bool compress,
                 PackReport &report, std::string &error)
    {
        report = PackReport();
        Packing packing;
        packing.report = &report;
        if (!collectMap(stage, map, false, packing, error))
            return false;
        return finish(packing, card / "maps" / (map + ".npac"), compress, error);
    }

    bool packAir(const fs::path &stage, const fs::path &card, const std::string &group,
                 const std::vector<std::string> &maps, bool multiplayer, AirReport &report, std::string &error)
    {
        report = AirReport();
        if (maps.empty())
            return true;
        std::vector<Packing> each(maps.size());
        std::vector<PackReport> reports(maps.size());
        for (std::size_t m = 0; m < maps.size(); ++m)
        {
            each[m].report = &reports[m];
            if (!collectMap(stage, maps[m], true, each[m], error))
                return false;
            if (multiplayer && !guestMatch(stage, maps[m], each[m], error))
                return false;
        }
        // A model is cooked for each map with a skin as big as that map has
        // room for: here every map has the same one, the smallest any of
        // them was given -- so the model is the same everywhere, and in the
        // base.
        {
            struct Variant
            {
                std::size_t map = 0;
                std::uint32_t texels = 0;
            };
            std::map<std::string, Variant> smallest;
            auto skinPathOf = [](const NpacMember &model, const std::string &name, std::uint32_t *texels)
            {
                hldata::ModelHeader header;
                std::memcpy(&header, model.data.data(), sizeof(header));
                if (texels != nullptr)
                    *texels = static_cast<std::uint32_t>(header.atlasWidth) * header.atlasHeight;
                return header.atlasWidth == 0
                           ? std::string()
                           : "mdl/" + hldata::modelSkinFile(name.c_str(), header.atlasWidth, header.atlasHeight);
            };
            auto modelName = [](const std::string &path)
            {
                const bool model = path.rfind("mdl/", 0) == 0 && path.size() > 8 &&
                                   path.compare(path.size() - 4, 4, ".hmd") == 0;
                return model ? path.substr(4, path.size() - 8) : std::string();
            };
            for (std::size_t m = 0; m < maps.size(); ++m)
            {
                for (const NpacMember &member : each[m].members)
                {
                    const std::string name = modelName(member.path);
                    if (name.empty() || member.data.size() < sizeof(hldata::ModelHeader))
                        continue;
                    std::uint32_t texels = 0;
                    skinPathOf(member, name, &texels);
                    auto it = smallest.find(name);
                    if (it == smallest.end() || texels < it->second.texels)
                        smallest[name] = Variant{m, texels};
                }
            }
            auto find = [](std::vector<NpacMember> &members, const std::string &path)
            {
                return std::find_if(members.begin(), members.end(),
                                    [&](const NpacMember &other) { return other.path == path; });
            };
            for (std::size_t m = 0; m < maps.size(); ++m)
            {
                for (const auto &[name, variant] : smallest)
                {
                    if (variant.map == m)
                        continue;
                    std::vector<NpacMember> &mine = each[m].members;
                    std::vector<NpacMember> &theirs = each[variant.map].members;
                    const auto own = find(mine, "mdl/" + name + ".hmd");
                    const auto chosen = find(theirs, "mdl/" + name + ".hmd");
                    if (own == mine.end() || chosen == theirs.end() || own->data == chosen->data)
                        continue;
                    const std::string ownSkin = skinPathOf(*own, name, nullptr);
                    const std::string chosenSkin = skinPathOf(*chosen, name, nullptr);
                    const auto chosenSkinAt = find(theirs, chosenSkin);
                    if (!chosenSkin.empty() && chosenSkinAt == theirs.end())
                        continue; // (its skin is not there to go with it)
                    own->data = chosen->data;
                    if (ownSkin != chosenSkin)
                    {
                        // (In its place among the models: an archive's
                        // folders are each in one run.)
                        const NpacMember skin = chosenSkinAt != theirs.end() ? *chosenSkinAt : NpacMember();
                        const auto ownSkinAt = find(mine, ownSkin);
                        std::size_t at = ownSkinAt != mine.end() ? static_cast<std::size_t>(ownSkinAt - mine.begin())
                                                                 : static_cast<std::size_t>(own - mine.begin()) + 1;
                        if (ownSkinAt != mine.end())
                            mine.erase(ownSkinAt);
                        if (!chosenSkin.empty() && find(mine, chosenSkin) == mine.end())
                            mine.insert(mine.begin() + static_cast<std::ptrdiff_t>(at), skin);
                    }
                }
            }
        }
        // The base: what every map of the group has, byte for byte -- the
        // weapons, the items, their sounds. A console sent the group keeps
        // it from one map to the next; only the rest of each is sent again.
        // (Never a map's own level or textures, even where two agree.)
        // (Only what stays in RAM: a model's block, a sound's clip. Skins and
        // sprites are in video memory, which is laid out again for every
        // map: they come with each map.)
        auto shareable = [](const std::string &path)
        {
            const hldata::GuestKind kind = hldata::guestKindOf(path.c_str());
            return kind == hldata::GuestKind::Model || kind == hldata::GuestKind::Sound;
        };
        std::set<std::string> base;
        for (const NpacMember &member : each[0].members)
        {
            if (!shareable(member.path))
                continue;
            bool everywhere = true;
            for (std::size_t m = 1; m < maps.size() && everywhere; ++m)
            {
                const auto it = std::find_if(each[m].members.begin(), each[m].members.end(),
                                             [&](const NpacMember &other) { return other.path == member.path; });
                everywhere = it != each[m].members.end() && it->data == member.data;
            }
            if (everywhere)
                base.insert(member.path);
        }
        std::vector<NpacMember> shared;
        for (const NpacMember &member : each[0].members)
        {
            if (base.count(member.path) != 0)
                shared.push_back(member);
        }
        fs::create_directories(card / "air");
        if (!writeGuest(shared, card / "air" / (group + ".base.hgs"), report.baseRaw, report.baseBytes, error))
            return false;
        for (std::size_t m = 0; m < maps.size(); ++m)
        {
            std::vector<NpacMember> own;
            for (NpacMember &member : each[m].members)
            {
                if (base.count(member.path) == 0)
                    own.push_back(std::move(member));
            }
            AirReport::Map line;
            line.name = maps[m];
            line.missing = reports[m].missing;
            if (!writeGuest(own, card / "air" / (maps[m] + ".hgs"), line.raw, line.bytes, error))
                return false;
            report.maps.push_back(line);
        }
        return true;
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

    bool packAirCommon(const fs::path &stage, const fs::path &card, PackReport &report, std::string &error)
    {
        report = PackReport();
        Packing packing;
        packing.report = &report;
        // The pictures and letters of the game, not of its title (the menu's
        // background and the animated name are never shown on a console
        // that was sent the game: it goes straight into the match); the
        // menu's sounds. Stored: read in place.
        std::vector<fs::path> files;
        std::error_code ec;
        for (auto it = fs::directory_iterator(stage / "ui", ec); !ec && it != fs::directory_iterator();
             it.increment(ec))
        {
            const std::string name = "ui/" + it->path().filename().string();
            if (it->is_regular_file(ec) && it->path().extension() != ".tmp" && name != hldata::kLogoFile &&
                name != hldata::kMenuFile)
                files.push_back(it->path());
        }
        std::sort(files.begin(), files.end());
        for (const fs::path &file : files)
            packing.add(file, "ui/" + file.filename().string(), false);
        for (const char *name : hldata::kUiSoundFiles)
            packing.add(stage / "snd" / (std::string(name) + ".hsn"), std::string("snd/") + name + ".hsn", false,
                        false);
        fs::create_directories(card / "air");
        std::size_t raw[static_cast<int>(hldata::GuestKind::Count)] = {};
        for (const NpacMember &member : packing.members)
            report.rawBytes += member.data.size();
        report.members = static_cast<int>(packing.members.size());
        return writeGuest(packing.members, card / "air" / "common.hgs", raw, report.cardBytes, error);
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
