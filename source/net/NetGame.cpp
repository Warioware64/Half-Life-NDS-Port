#include "net/NetGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <nds.h>

#include "Global.hpp"
#include "Settings.hpp"
#include "data/LevelFile.hpp"
#include "game/Game.hpp"
#include "game/GameApi.hpp"
#include "game/Player.hpp"
#include "render/Camera.hpp"
#include "net/NetLink.hpp"
#include "HlManifest.hpp"
#include "HlUi.hpp"
#include "audio/Sound.hpp"
#include "data/ModelCache.hpp"
#include "render/EffectDraw.hpp"
#include "render/MapCard.hpp"
#include "HlPreview.hpp"
#include <sys/stat.h>
#ifdef HLDS_CHILD
#include "net/GuestLoad.hpp"
#endif

namespace hlrt
{
    using namespace hldata;

    NetGame netGame;

    namespace
    {
        /// Frames a client may be out of touch with its host before it is
        /// taken for lost: two seconds. (A console loading a map says
        /// nothing for longer than that, but stays the host's client all
        /// the while: it is the radio that is asked, not the frames.)
        constexpr int kLostFrames = 120;

        std::uint16_t tokenOfThisConsole()
        {
            std::uint8_t mac[6];
            netlink::address(mac);
            const std::uint16_t token =
                static_cast<std::uint16_t>((mac[4] << 8 | mac[5]) ^ (mac[2] << 8 | mac[3]) ^ (Global::vblanks() * 40503u));
            return token != 0 ? token : 1;
        }

        /// This console's spray: the profile's logo of the card's (HlUi.hpp:
        /// ui/logos.hic), its rows as they are (kNetSprayBytes). False if
        /// the card has none.
        bool ownSpray(std::uint8_t *out)
        {
            std::FILE *file = std::fopen(global.commonPath(kLogosFile).c_str(), "rb");
            if (file == nullptr)
                return false;
            std::fseek(file, 0, SEEK_END);
            const long size = std::ftell(file);
            std::fseek(file, 0, SEEK_SET);
            const long most = static_cast<long>(sizeof(IconsHeader) +
                                                kLogosMost * (kLogoSize * iconRowWords(kLogoSize) * 4 + kLogoNameBytes));
            auto *words = size > 0 && size <= most ? static_cast<std::uint32_t *>(std::malloc(static_cast<std::size_t>(size)))
                                                   : nullptr;
            const bool got = words != nullptr &&
                             std::fread(words, 1, static_cast<std::size_t>(size), file) == static_cast<std::size_t>(size);
            std::fclose(file);
            LogosView logos;
            const bool ok = got && logos.open(reinterpret_cast<const std::uint8_t *>(words), static_cast<std::size_t>(size));
            static_assert(kLogoSize * iconRowWords(kLogoSize) * 4 == kNetSprayBytes, "a spray is a logo's rows");
            if (ok)
            {
                const int logo = settings.spray % logos.count();
                std::memcpy(out, logos.icons.rows + static_cast<std::size_t>(logo) * kLogoSize * iconRowWords(kLogoSize),
                            kNetSprayBytes);
            }
            std::free(words);
            return ok;
        }

        /// The settings' map, if a match can be played on it; else the first.
        const std::string *hostedMap(const std::vector<std::string> &maps)
        {
            for (const std::string &map : maps)
            {
                if (map == settings.hostMap)
                    return &map;
            }
            return maps.empty() ? nullptr : &maps[0];
        }
    } // namespace

    // (The settings' modes and the match's: the same numbers.)
    static_assert(kModeDeathmatch == kNetModeDeathmatch && kModeTeamplay == kNetModeTeamplay);

    void NetGame::sayProfile()
    {
        NetProfile profile;
        profile.model = settings.playerModel;
        profile.top = settings.topColour;
        profile.bottom = settings.bottomColour;
        profile.asked = match_.myProfile.asked;
        profile.sprayColour = settings.sprayColour;
        std::uint8_t spray[kNetSprayBytes];
        netSetProfile(match_, profile, ownSpray(spray) ? spray : nullptr);
    }

    void NetGame::sayRules()
    {
        NetRules rules;
        // (Co-op is not played yet: a free-for-all.)
        rules.mode = settings.hostMode == kModeTeamplay ? kNetModeTeamplay : kNetModeDeathmatch;
        rules.flags = static_cast<std::uint16_t>(
            (settings.weaponsStay ? kRuleWeaponsStay : 0) | (settings.fallDamage != 0 ? kRuleRealisticFall : 0) |
            (settings.friendlyFire ? kRuleFriendlyFire : 0) | (settings.flashlight ? kRuleFlashlight : 0) |
            (settings.hostAutoaim ? kRuleAutoaim : 0) | (settings.allowGuests ? kRuleGuests : 0));
        rules.most = settings.maxPlayers;
        const std::string *hosted = hostedMap(maps_);
        rules.mapHash = hosted != nullptr ? nameHash(hosted->c_str()) : 0;
        netSetRules(match_, rules);
        // What the beacon says of it.
        NetRoomInfo room;
        room.mode = rules.mode;
        room.most = match_.rules.most;
        room.guests = settings.allowGuests;
        const std::string *map = hostedMap(maps_);
        room.mapHash = map != nullptr ? nameHash(map->c_str()) : 0;
        std::uint8_t bytes[kNetRoomBytes];
        netRoomWrite(room, bytes);
        netlink::hostRoom(bytes, kNetRoomBytes);
    }

    void NetGame::hostChanged()
    {
        if (phase_ == Phase::Hosting)
            sayRules();
    }

    void NetGame::askTeam(int team)
    {
        if (on())
            netAskTeam(match_, team);
    }

    int NetGame::bars(int p) const
    {
        if (p < 0 || p >= kNetPlayers || !on())
            return -1;
        if (isHost())
            return p == 0 ? 4 : netShareBars(netLinkShare(match_.host, p));
        return p == 0 ? netBars(netlink::strength()) : -1;
    }

    /// (For the menu, which says why hosting could not begin.)
    int netlinkFailStage()
    {
        return netlink::failStage();
    }

    // (The profile's name is a match's: the same room.)
    static_assert(hldata::kProfileNameBytes == static_cast<std::size_t>(kNetTextBytes));

    bool NetGame::host()
    {
        char name[kNetTextBytes];
        std::snprintf(name, sizeof(name), "%s", settings.playerName());
        // (The radio for as many as a match can have: how many may play is the
        // rules', which can change in the lobby, and the host refuses the
        // rest -- the radio's own count could not change without everyone
        // being let go.)
        if (!netlink::host(name, kNetPlayers))
            return false;
        netBegin(match_, NetMatch::Role::Host, 0, name);
#ifndef HLDS_CHILD
        airEnd();
        guests_ = 0;
#endif
        sayRules();
        sayProfile();
        profilesSeen_ = 0;
        phase_ = Phase::Hosting;
        clientsWere_ = 0;
        starting_ = goAsked_ = cancelled_ = false;
        ended_ = "";
        startPending_ = false;
        return true;
    }

    bool NetGame::scan()
    {
        if (!netlink::scan())
            return false;
        hldata::renew(match_);
        phase_ = Phase::Scanning;
        ended_ = "";
        startPending_ = false;
        return true;
    }

    int NetGame::found(Found *out, int most)
    {
        int count = 0;
        const int all = phase_ == Phase::Scanning ? netlink::found() : 0;
        for (int i = 0; i < all && count < most; ++i)
        {
            Found &one = out[count];
            int players = 0, room = 0;
            if (!netlink::hostAt(i, one.name, sizeof(one.name), &players, &room))
                continue;
            std::uint8_t bytes[16]; // (the radio's most: DSWIFI_BEACON_GAME_DATA_MAX)
            int size = 0, rssi = 0;
            netlink::hostRoomAt(i, bytes, size, rssi);
            netRoomRead(bytes, size, one.room);
            one.map = nullptr;
            for (const std::string &map : maps_)
            {
                if (one.room.mapHash != 0 && nameHash(map.c_str()) == one.room.mapHash)
                    one.map = map.c_str();
            }
            one.players = players;
            one.most = room;
            one.bars = netBars(rssi);
            std::snprintf(one.line, sizeof(one.line), "%s  %d/%d", one.name[0] ? one.name : "A game", players, room);
            if (!one.name[0])
                std::snprintf(one.name, sizeof(one.name), "A game");
            one.index = i;
            ++count;
        }
        return count;
    }

    const char *NetGame::lobbyMap() const
    {
        if (isHost())
        {
            const std::string *map = hostedMap(maps_);
            return map != nullptr ? map->c_str() : "";
        }
        // (What the host's rules say now; else what its beacon said; else,
        // a map this card has not got, what the host's picture of it said.)
        for (const std::string &map : maps_)
        {
            if (match_.rules.mapHash != 0 && nameHash(map.c_str()) == match_.rules.mapHash)
                return map.c_str();
        }
        if (!previewName_.empty() && nameHash(previewName_.c_str()) == match_.rules.mapHash)
            return previewName_.c_str();
        return lobbyMap_.c_str();
    }

    void NetGame::previewEnd()
    {
        std::free(previewRing_);
        previewRing_ = nullptr;
        preview_.ring = nullptr;
    }

    void NetGame::previewTick()
    {
        // (A client in its lobby: nothing is asked for while a match begins.)
        const std::uint32_t hash = match_.rules.mapHash;
        if (isHost() || phase_ != Phase::Lobby || starting_ || hash == 0 || hash == previewHad_)
            return;
        if (previewRing_ != nullptr && previewHash_ == hash)
        {
            // (Ten seconds and nothing: the host has no picture of it.)
            if (Global::vblanks() - previewSince_ > 10 * 60)
            {
                previewHad_ = hash;
                previewEnd();
            }
            return;
        }
        // Another map: a picture of it here already, or not the card's?
#ifndef HLDS_CHILD
        const char *map = lobbyMap();
        if (map[0] == 0 || mapPictured(map))
        {
            previewHad_ = hash;
            return;
        }
#endif
        previewEnd();
        previewRing_ = static_cast<std::uint8_t *>(std::malloc(airPieces(kPreviewBytes) * kAirPiece));
        if (previewRing_ == nullptr)
            return;
        preview_.want(AirStream::Preview, "", previewRing_);
        previewHash_ = hash;
        previewSince_ = Global::vblanks();
    }

    void NetGame::previewFrame(const AirHead &head)
    {
        if (previewRing_ == nullptr || nameHash(head.name) != previewHash_ || !preview_.frame(head))
            return;
        if (!preview_.known || preview_.base < preview_.pieces() || preview_.length != kPreviewBytes)
            return;
        // All of it: in the window's slots in order, the whole file. Kept --
        // on the card, beside this console's own; a console without one
        // keeps it for its map card -- and the host told, for a while.
        if (previewValid(previewRing_, kPreviewBytes))
        {
            previewName_ = head.name;
#ifdef HLDS_CHILD
            mapCard.givePicture(head.name, previewRing_);
#else
            const std::string folder = global.savesDir() + kPreviewFolder;
            mkdir(folder.c_str(), 0777);
            if (std::FILE *file = std::fopen((folder + head.name + ".hpv").c_str(), "wb"))
            {
                std::fwrite(previewRing_, 1, kPreviewBytes, file);
                std::fclose(file);
            }
            mapCard.hide();
#endif
        }
        previewHad_ = previewHash_;
        previewLinger_ = 20;
        previewEnd();
    }

    int NetGame::previewReply(std::uint8_t *bytes)
    {
        if (previewLinger_ > 0)
            --previewLinger_;
        else if (previewRing_ == nullptr)
            return 0;
        return preview_.ask(bytes);
    }

    bool NetGame::join(int index)
    {
        // (What its beacon says of its map, kept for the lobby's card.)
        lobbyMap_.clear();
        {
            std::uint8_t bytes[16];
            int size = 0, rssi = 0;
            NetRoomInfo room;
            if (phase_ == Phase::Scanning && netlink::hostRoomAt(index, bytes, size, rssi) && netRoomRead(bytes, size, room))
            {
                for (const std::string &map : maps_)
                {
                    if (room.mapHash != 0 && nameHash(map.c_str()) == room.mapHash)
                        lobbyMap_ = map;
                }
            }
        }
        if (phase_ != Phase::Scanning || !netlink::join(index))
            return false;
        char name[kNetTextBytes];
        std::snprintf(name, sizeof(name), "%s", settings.playerName());
        netBegin(match_, NetMatch::Role::Client, tokenOfThisConsole(), name);
        sayProfile();
        profilesSeen_ = 0;
        phase_ = Phase::Joining;
        lostFrames_ = 0;
        return true;
    }

    bool NetGame::joinStarter()
    {
        // What the loader that started this program left: who sent it.
        std::uint8_t address[6];
        std::memcpy(address, reinterpret_cast<const void *>(0x027FFC46), sizeof(address));
        if (!netlink::joinDirect(address, phase_ == Phase::Joining))
            return false;
        if (phase_ != Phase::Joining)
        {
            char name[kNetTextBytes];
            std::snprintf(name, sizeof(name), "%s", settings.playerName());
            netBegin(match_, NetMatch::Role::Client, tokenOfThisConsole(), name);
            sayProfile();
            profilesSeen_ = 0;
        }
        phase_ = Phase::Joining;
        lostFrames_ = 0;
        ended_ = "";
        return true;
    }

    void NetGame::leave()
    {
        if (phase_ == Phase::Off)
            return;
        // A goodbye, said a few times over: there is no waiting to hear
        // that it arrived.
        if (netPlaced(match_) && (phase_ == Phase::Hosting || phase_ == Phase::Serving || phase_ == Phase::Lobby ||
                                  phase_ == Phase::Playing))
        {
            netSay(match_, netEvent(NetEventType::Bye));
            for (int n = 0; n < 8; ++n)
            {
                send();
                swiWaitForVBlank();
            }
        }
        netlink::powerDown();
        effectDraw.dropSprays();
        hldata::renew(match_);
        phase_ = Phase::Off;
        startPending_ = false;
        starting_ = goAsked_ = cancelled_ = false;
#ifndef HLDS_CHILD
        airEnd();
        guests_ = 0;
#endif
    }

    void NetGame::lost(const char *why)
    {
#ifndef HLDS_CHILD
        airEnd();
        guests_ = 0;
#endif
        netlink::powerDown();
        effectDraw.dropSprays();
        hldata::renew(match_);
        phase_ = Phase::Off;
        startPending_ = false;
        starting_ = goAsked_ = cancelled_ = false;
        ended_ = why;
    }

#ifndef HLDS_CHILD
    void NetGame::start(const std::string &map, int fragLimit, int minuteLimit, int skill)
    {
        if (phase_ != Phase::Hosting)
            return;
        // Every console told at once, and each loads its map now -- those
        // with the game from their cards, those without it as this one sends
        // it -- and says when it has (Ready). This one loads its own last:
        // it cannot send and load at once. (Nobody new meanwhile.)
        // (The rules as they are now, before the start: in that order on
        // every console.)
        sayRules();
        const std::uint32_t hash = nameHash(map.c_str());
        // (How hard: every console runs its own monsters and chargers.)
        netSay(match_, netEvent(NetEventType::Start, 0, static_cast<std::int16_t>(hash & 0xFFFF),
                                static_cast<std::int16_t>(hash >> 16), fragLimit, minuteLimit, skill));
        pendingSkill_ = skill;
        match_.fragLimit = fragLimit;
        match_.minuteLimit = minuteLimit;
        netlink::admit(false);
        pendingMap_ = map;
        serveSkill_ = skill;
        serveStage_ = 0;
        starting_ = true;
        ready_ = 0;
        goAsked_ = false;
        phase_ = Phase::Serving;
        // (Said before anything else: a console sent the game is told before
        // its stream begins, and the others load at once.)
        for (int n = 0; n < 6; ++n)
        {
            frame();
            swiWaitForVBlank();
        }
        // (A console still being sent the game itself: that first. A map's
        // picture is given up.)
        if (air_.sending() && air_.stream == AirStream::Preview)
            airEnd();
        if (!air_.sending())
            serveNext();
    }

    void NetGame::cancelStart()
    {
        if (!starting_ || !isHost() || phase_ == Phase::Playing)
            return;
        netSay(match_, netEvent(NetEventType::Cancel));
        airEnd();
        starting_ = false;
        startPending_ = false;
        phase_ = Phase::Hosting;
        netlink::admit(true);
    }

    void NetGame::serveNext()
    {
        const unsigned mask = guests_ & netlink::clients();
        const NetMapInfo *info = nullptr;
        for (const NetMapInfo &one : mapInfo_)
        {
            if (one.name == pendingMap_)
                info = &one;
        }
        // The group's base (those who have it say so at once), then the map.
        if (serveStage_ == 0 && mask != 0)
        {
            serveStage_ = 1;
            const int group = info != nullptr ? info->airGroup : 0;
            if (group > 0 && group < static_cast<int>(sizeof(kAirGroupNames) / sizeof(kAirGroupNames[0])) &&
                airBegin(AirStream::Base, kAirGroupNames[group], std::string("air/") + kAirGroupNames[group] + ".base.hgs",
                         mask))
                return;
        }
        if (serveStage_ <= 1 && mask != 0)
        {
            serveStage_ = 2;
            if (airBegin(AirStream::Map, pendingMap_, "air/" + pendingMap_ + ".hgs", mask, serveSkill_))
                return;
            // (Not on this card to be sent: they cannot play it.)
            for (int c = 1; c < kNetPlayers; ++c)
            {
                if ((mask >> c) & 1)
                    netlink::kick(c);
            }
        }
        // Sent (or nobody to send it to): this console's own map now.
        serveStage_ = 3;
        phase_ = Phase::Hosting;
        startPending_ = true;
    }

    bool NetGame::airBegin(AirStream stream, const std::string &name, const std::string &path, unsigned mask, int skill)
    {
        airEnd();
        airFile_ = std::fopen(global.dataPath(path).c_str(), "rb");
        if (airFile_ == nullptr)
            return false;
        std::fseek(airFile_, 0, SEEK_END);
        const long length = std::ftell(airFile_);
        if (length <= 0)
        {
            airEnd();
            return false;
        }
        serves_ = static_cast<std::uint16_t>(serves_ + 1 == 0 ? 1 : serves_ + 1);
        air_.begin(stream, name.c_str(), static_cast<std::uint32_t>(length), serves_, mask,
                   static_cast<std::uint8_t>(skill));
        return true;
    }

    void NetGame::airEnd()
    {
        if (airFile_ != nullptr)
            std::fclose(airFile_);
        airFile_ = nullptr;
        air_.stop();
    }

    int NetGame::airSend(int frames)
    {
        std::uint8_t piece[kAirPiece];
        std::uint8_t bytes[kNetCmdBytes];
        std::uint32_t index;
        int n = 0;
        for (; n < frames && airFile_ != nullptr && air_.next(index); ++n)
        {
            const int count = air_.pieceBytes(index);
            if (std::fseek(airFile_, static_cast<long>(index) * kAirPiece, SEEK_SET) != 0 ||
                std::fread(piece, 1, static_cast<std::size_t>(count), airFile_) != static_cast<std::size_t>(count))
                break;
            const int size = air_.piece(index, piece, count, bytes);
            if (size > 0)
            {
                netlink::send(bytes, size);
                ++sent_;
            }
        }
        return n;
    }

    void NetGame::airAsk(int from, const std::uint8_t *bytes, int size)
    {
        AirStream stream;
        std::uint16_t serve;
        if (!airAskOf(bytes, size, stream, serve) || from < 1 || from >= kNetPlayers)
            return;
        // (A console that asks for any of the game is one sent it; asking
        // for a map's picture, any console does.)
        if (stream != AirStream::Preview)
            guests_ |= 1u << from;
        if (air_.asked(from, bytes, size))
            return;
        // The picture of the lobby's map, for a console that has none: sent
        // to all who ask while it is, and only in the lobby.
        if (stream == AirStream::Preview && serve == 0 && phase_ == Phase::Hosting)
        {
            const std::string *map = hostedMap(maps_);
            if (map == nullptr)
                return;
            if (air_.sending() && air_.stream == AirStream::Preview && *map == air_.name)
                air_.add(from);
            else if (!air_.sending() && mapPictured(*map))
                airBegin(AirStream::Preview, *map, "saves/" + std::string(kPreviewFolder) + *map + ".hpv", 1u << from);
            return;
        }
        // A console that has just come asks for the game itself.
        if (stream == AirStream::Common && serve == 0)
        {
            if (air_.sending() && air_.stream == AirStream::Common)
                air_.add(from);
            else if (!air_.sending())
                airBegin(AirStream::Common, "common", "air/common.hgs", 1u << from);
            // (Else it waits: what is being sent is sent first. It asks again.)
        }
    }

    bool NetGame::sending(const char *&what, int &consoles, int &percent) const
    {
        if (!air_.sending())
            return false;
        // (A map's picture is not worth saying: it is a second's.)
        if (air_.stream == AirStream::Preview)
            return false;
        what = air_.stream == AirStream::Common ? "the game" : air_.name;
        consoles = air_.waiting();
        percent = air_.percent();
        return true;
    }

    int NetGame::guests() const
    {
        int n = 0;
        for (int c = 1; c < kNetPlayers; ++c)
            n += ((guests_ & netlink::clients()) >> c) & 1;
        return n;
    }

    int NetGame::startStatus(int p, char *line, int room) const
    {
        // 0: nothing to say of p (not there); 1: still on its way; 2: ready.
        if (p < 0 || p >= kNetPlayers || !netHere(match_, p))
            return 0;
        if (p == 0)
        {
            std::snprintf(line, static_cast<std::size_t>(room), "%s", air_.sending() ? "sending" : "loading");
            return 1;
        }
        if ((ready_ >> p) & 1)
        {
            std::snprintf(line, static_cast<std::size_t>(room), "ready");
            return 2;
        }
        const AirSender::Want &want = air_.wants[p];
        if (air_.sending() && want.on && !want.done)
            std::snprintf(line, static_cast<std::size_t>(room), "receiving %d%%",
                          air_.pieces() == 0 ? 0 : static_cast<int>(want.base * 100 / air_.pieces()));
        else
            std::snprintf(line, static_cast<std::size_t>(room), "loading");
        return 1;
    }
#else
    // (A console that was sent the game is never a host.)
    void NetGame::start(const std::string &, int, int, int) {}
    void NetGame::cancelStart() {}
    int NetGame::startStatus(int, char *, int) const { return 0; }
    bool NetGame::sending(const char *&, int &, int &) const { return false; }
    int NetGame::guests() const { return 0; }

    bool NetGame::airAsked(AirHead &head)
    {
        if (!airPending_)
            return false;
        airPending_ = false;
        head = airAsked_;
        return true;
    }
#endif

    void NetGame::loadedMap()
    {
        // A client says so; the host gives the others a while from now.
        if (isHost())
            loadedAt_ = Global::vblanks();
        else
            netSay(match_, netEvent(NetEventType::Ready));
    }

    bool NetGame::goAsked()
    {
        if (!starting_)
            return false;
        if (isHost())
        {
            // Everyone there ready -- or twenty seconds since this console
            // was: those not yet begin when they are (Go waits for them).
            const unsigned here = netlink::clients();
            if ((ready_ & here) != here && Global::vblanks() - loadedAt_ < kReadyVblanks)
                return false;
            netSay(match_, netEvent(NetEventType::Go));
            goAsked_ = true;
        }
        if (!goAsked_)
            return false;
        starting_ = false;
        goAsked_ = false;
        return true;
    }

    bool NetGame::cancelled()
    {
        const bool was = cancelled_;
        cancelled_ = false;
        return was;
    }

    bool NetGame::startAsked(std::string &map, int &skill)
    {
        if (!startPending_)
            return false;
        startPending_ = false;
        map = pendingMap_;
        skill = pendingSkill_;
        return true;
    }

    void NetGame::mapLoaded()
    {
        const LevelView &view = level.view;
        spawns_.clear();
        for (std::uint32_t e = 0; e < view.entityCount; ++e)
        {
            const EntityRecord &entity = view.entities[e];
            if (entity.classId != static_cast<std::uint16_t>(EntityClass::InfoPlayerDeathmatch))
                continue;
            NetSpawn spawn;
            for (int i = 0; i < 3; ++i)
                spawn.pos[i] = entity.origin[i];
            spawn.yaw = entity.angles[1];
            spawns_.push_back(spawn);
        }
        // (A map with none: where a game alone starts.)
        if (spawns_.empty())
        {
            NetSpawn spawn;
            for (int i = 0; i < 3; ++i)
                spawn.pos[i] = player.state.origin[i];
            spawn.yaw = 0;
            spawns_.push_back(spawn);
        }
        int model = -1;
        for (std::uint32_t m = 0; m < view.modelRefCount; ++m)
        {
            if (std::strcmp(view.modelRefs[m].name, "player") == 0)
                model = static_cast<int>(m);
        }
        gameApi.netStart(match_, game.logic, player.state, static_cast<int>(view.actorCount), model, spawns_.data(),
                 static_cast<int>(spawns_.size()), tokenOfThisConsole());
        phase_ = Phase::Playing;
        // Everyone's spray, made into its picture now, while the map is
        // being put up (nobody comes once the match has begun).
        effectDraw.loadSprays(match_);
        profilesSeen_ = 0;
        overMs_ = 0;
        lobbyAsked_ = false;

        // A development aid (tools/emu/play.sh with two consoles): "x y z
        // yaw" in debug_net.txt, whole units and degrees, puts the host
        // there and every client 160 units in front of it, facing it -- so
        // that the two see each other from the first frame.
        if (std::FILE *file = std::fopen(global.dataPath("debug_net.txt").c_str(), "r"))
        {
            int x, y, z, yaw;
            if (std::fscanf(file, "%d %d %d %d", &x, &y, &z, &yaw) == 4)
            {
                const std::uint16_t turn = static_cast<std::uint16_t>(yaw * 65536 / 360);
                fx sine, cosine;
                fxSinCos(turn, sine, cosine);
                const int far = match_.me == 0 ? 0 : 160 * match_.me;
                player.state.origin[0] = fxFromInt(x) + far * cosine;
                player.state.origin[1] = fxFromInt(y) + far * sine;
                player.state.origin[2] = fxFromInt(z);
                match_.respawned = false;
                camera.yaw = static_cast<std::uint16_t>(match_.me == 0 ? turn : turn + 32768);
                camera.pitch = 0;
            }
            std::fclose(file);
        }
    }

    void NetGame::send()
    {
        std::uint8_t bytes[kNetCmdBytes];
        const int size = netWrite(match_, bytes);
        if (size > 0)
        {
            netlink::send(bytes, size);
            ++sent_;
        }
    }

    void NetGame::frame()
    {
        if (phase_ == Phase::Off || phase_ == Phase::Scanning)
            return;
        if (phase_ == Phase::Joining)
        {
            if (netlink::joined())
            {
                phase_ = Phase::Lobby;
                lostFrames_ = 0;
            }
            else if (netlink::refused())
            {
                lost("The host would not have it.");
                return;
            }
            else
                return;
        }

        // Who is there.
        if (isHost())
        {
            const unsigned now = netlink::clients();
            for (int c = 1; c < kNetPlayers; ++c)
            {
                const unsigned bit = 1u << c;
                if ((now & bit) && !(clientsWere_ & bit))
                    netHostJoin(match_.host, c);
                else if (!(now & bit) && (clientsWere_ & bit))
                {
#ifndef HLDS_CHILD
                    air_.drop(c);
                    guests_ &= ~bit;
#endif
                    netHostLeave(match_.host, c);
                    match_.names[c][0] = 0;
                    NetEvent gone;
                    gone.type = NetEventType::Roster;
                    gone.a = static_cast<std::uint8_t>(c);
                    match_.host.out.add(gone);
                    if (match_.noticeCount < NetMatch::kMostNotices)
                    {
                        match_.notices[match_.noticeCount].type = NetEventType::Bye;
                        match_.notices[match_.noticeCount++].who = static_cast<std::uint8_t>(c);
                    }
                }
            }
            clientsWere_ = now;
        }
        else if (!netlink::joined())
        {
            if (++lostFrames_ >= kLostFrames)
            {
                lost("The link to the host was lost.");
                return;
            }
        }
        else
            lostFrames_ = 0;

        // What has come: the game's, and what is said of the streams sent
        // to consoles that were given the game (HlNetAir.hpp).
        std::uint8_t bytes[kNetCmdBytes];
        int from = 0;
        for (int size; (size = netlink::take(bytes, from)) > 0;)
        {
            ++heard_;
#ifndef HLDS_CHILD
            if (size > 3 && bytes[3] == kNetFrameAirAsk)
            {
                if (isHost())
                    airAsk(from, bytes, size);
                continue;
            }
#endif
            if (size > 3 && bytes[3] == kNetFrameAirData)
            {
                AirHead head;
                if (!isHost() && airRead(bytes, size, head) && head.stream == AirStream::Preview)
                {
                    previewFrame(head);
                    continue;
                }
#ifndef HLDS_CHILD
                continue;
#else
                int reply = 0;
                if (guest::airFrame(bytes, size, airReply_, reply, head))
                {
                    airAsked_ = head;
                    airPending_ = true;
                }
                if (reply > 0)
                    airReplySize_ = reply;
                continue;
#endif
            }
            gameApi.netRead(match_, bytes, size, from);
        }
#ifndef HLDS_CHILD
        // Who is left out of a stream for saying nothing is let go; a
        // stream that all have is done with, and the next sent.
        if (air_.sending())
        {
            // (Not a console that stopped asking for a picture: only one
            // the match, or the game itself, is waiting for.)
            const unsigned out = air_.frame();
            for (int c = 1; c < kNetPlayers && air_.stream != AirStream::Preview; ++c)
            {
                if ((out >> c) & 1)
                {
                    netlink::kick(c);
                    guests_ &= ~(1u << c);
                }
            }
            if (air_.done())
            {
                airEnd();
                if (phase_ == Phase::Serving)
                    serveNext();
            }
        }
#endif

        // The lobby's part of it; with a map loaded, the game's tick does
        // the rest.
        if (!match_.playing)
        {
            static LogicWorld none;
            static PlayerState nobody;
            gameApi.netLobby(match_, none, nobody);
            if (isHost())
            {
#ifndef HLDS_CHILD
                // A stream being sent: as fast as the air takes it while a
                // match waits for it (the game's frames now and then, for
                // those who have the game), every other frame in the lobby.
                // (A frame of some kind every frame, whatever: a client
                // answers only a frame of the host's -- one that has nothing
                // to say for itself would wait for nothing.)
                if (phase_ == Phase::Serving && air_.sending())
                {
                    if ((++ticks_ & 7) == 0)
                        send();
                    if (airSend((ticks_ & 7) == 0 ? 1 : 2) == 0 && (ticks_ & 7) != 0)
                        send();
                }
                else if (!(air_.sending() && (++ticks_ & 1) && airSend(1) > 0))
                    send();
#endif
            }
            else
            {
                // A client: what a stream's frame asked to be answered, in
                // place of the game's; and every other frame, while it asks
                // for the host's picture of its map, that.
                previewTick();
                std::uint8_t reply[kNetReplyBytes];
                int size = 0;
#ifdef HLDS_CHILD
                if (airReplySize_ > 0)
                {
                    std::memcpy(reply, airReply_, static_cast<std::size_t>(airReplySize_));
                    size = airReplySize_;
                    airReplySize_ = 0;
                }
#endif
                if (size == 0 && (++ticks_ & 1))
                    size = previewReply(reply);
                if (size > 0)
                    netlink::send(reply, size);
                else
                    send();
            }
        }

        // Everyone in their own colours, as their profiles say.
        if (match_.profileStamp != profilesSeen_)
        {
            profilesSeen_ = match_.profileStamp;
            for (int p = 0; p < kNetPlayers; ++p)
            {
                const NetProfile &profile = match_.profiles[p];
                if (profile.known)
                    modelCache.setPlayerColours(p, profile.top, profile.bottom);
            }
        }
        if (match_.refused)
        {
            match_.passedCount = 0;
            lost("The game is full.");
            return;
        }

        // What the match leaves to whoever runs the screens.
        for (int n = 0; n < match_.passedCount; ++n)
        {
            const NetEvent &event = match_.passed[n];
            if (event.type == NetEventType::Ready && isHost() && event.from > 0 && event.from < kNetPlayers)
            {
                ready_ |= 1u << event.from;
                continue;
            }
            if (event.type == NetEventType::Go && !isHost())
            {
                goAsked_ = true;
                continue;
            }
            if (event.type == NetEventType::Cancel && !isHost())
            {
                starting_ = goAsked_ = startPending_ = false;
                cancelled_ = true;
                continue;
            }
            if (event.type == NetEventType::Start && !isHost())
            {
                starting_ = true;
                goAsked_ = cancelled_ = false;
                const std::uint32_t hash = static_cast<std::uint16_t>(event.v[0]) |
                                           (static_cast<std::uint32_t>(static_cast<std::uint16_t>(event.v[1])) << 16);
                pendingMap_.clear();
                match_.fragLimit = event.v[2];
                match_.minuteLimit = event.v[3];
                // (A host from before there was a choice says 0: MEDIUM.)
                pendingSkill_ = event.v[4] >= 1 && event.v[4] <= 3 ? event.v[4] : 2;
#ifdef HLDS_CHILD
                // (Sent the game: its map comes as the host sends it -- airAsked.)
                (void)hash;
                continue;
#endif
                for (const std::string &map : maps_)
                {
                    if (nameHash(map.c_str()) == hash)
                        pendingMap_ = map;
                }
                if (pendingMap_.empty())
                {
                    match_.passedCount = 0;
                    leave();
                    ended_ = "This card has not the host's map.";
                    return;
                }
                startPending_ = true;
            }
            else if (event.type == NetEventType::Bye && !isHost() && event.from == 0)
            {
                match_.passedCount = 0;
                lost("The host has left.");
                return;
            }
        }
        match_.passedCount = 0;
    }

    void NetGame::tick(std::uint16_t yaw, std::int32_t pitch, std::uint32_t did, bool lamp, int ms)
    {
        if (phase_ != Phase::Playing)
            return;
        gameApi.netTick(match_, game.logic, player.state, yaw, pitch, did, lamp, ms);
        // The match over: its scores for a while, then the lobby.
        if (match_.over && !lobbyAsked_ && (overMs_ += ms) >= kNetIntermissionMs)
            lobbyAsked_ = true;
        // (More than four: the host's frames every other frame, so that the
        // air has room for everyone's answers. Each glides between.)
        int here = 0;
        for (int p = 0; p < kNetPlayers; ++p)
            here += netHere(match_, p) ? 1 : 0;
        if (!isHost() || here <= 4 || (++ticks_ & 1) == 0)
            send();
    }

    bool NetGame::lobbyAsked()
    {
        if (phase_ != Phase::Playing || !lobbyAsked_)
            return false;
        lobbyAsked_ = false;
        overMs_ = 0;
        // (Asked once: the next is the next match's.)
        match_.over = false;
        return true;
    }

    void NetGame::toLobby()
    {
        if (phase_ != Phase::Playing)
            return;
        effectDraw.dropSprays();
        match_.playing = false;
        match_.over = false;
        match_.firstActor = -1;
        match_.passedCount = 0;
        phase_ = isHost() ? Phase::Hosting : Phase::Lobby;
        // (Its map's picture asked for again: a console sent the game let go
        // of it for the match.)
        previewHad_ = 0;
        // (The host takes newcomers again, between matches.)
        if (isHost())
            netlink::admit(true);
    }

    bool NetGame::spray()
    {
        if (phase_ != Phase::Playing)
            return false;
        if (!netSpray(match_, game.logic, player.state, camera.pos, camera.forward))
            return false;
        sound.ui(kUiSoundSpray);
        return true;
    }

    void NetGame::used(int index)
    {
        if (phase_ == Phase::Playing)
            gameApi.netUsed(match_, index);
    }

    bool NetGame::notice(char *line, int room)
    {
        if (match_.noticeCount == 0)
            return false;
        const NetNotice notice = match_.notices[0];
        for (int n = 1; n < match_.noticeCount; ++n)
            match_.notices[n - 1] = match_.notices[n];
        --match_.noticeCount;
        auto name = [this](int p) { return p < kNetPlayers && match_.names[p][0] ? match_.names[p] : "Someone"; };
        switch (notice.type)
        {
        case NetEventType::Died:
            if (notice.other < kNetPlayers && notice.other != notice.who)
                std::snprintf(line, static_cast<std::size_t>(room), "%s killed %s", name(notice.other), name(notice.who));
            else
                std::snprintf(line, static_cast<std::size_t>(room), "%s died", name(notice.who));
            break;
        case NetEventType::Hello:
            std::snprintf(line, static_cast<std::size_t>(room), "%s is here", name(notice.who));
            break;
        case NetEventType::Bye:
            std::snprintf(line, static_cast<std::size_t>(room), "A player has left");
            break;
        case NetEventType::End:
            std::snprintf(line, static_cast<std::size_t>(room), "THE MATCH IS OVER");
            break;
        default:
            line[0] = 0;
            break;
        }
        return true;
    }

    const char *NetGame::ended()
    {
        const char *why = ended_;
        ended_ = "";
        return why;
    }

    void NetGame::status(char *line, int room) const
    {
        std::snprintf(line, static_cast<std::size_t>(room), "net%d me%d o%lu i%lu x%u q%d",
                      static_cast<int>(phase_), match_.me, static_cast<unsigned long>(sent_),
                      static_cast<unsigned long>(heard_), netlink::dropped(),
                      isHost() ? match_.host.out.count : match_.client.out.count);
    }
} // namespace hlrt
