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
#include "HlUi.hpp"
#include "audio/Sound.hpp"
#include "data/ModelCache.hpp"
#include "render/EffectDraw.hpp"

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
        if (!netlink::host(name, settings.maxPlayers))
            return false;
        netBegin(match_, NetMatch::Role::Host, 0, name);
        sayRules();
        sayProfile();
        profilesSeen_ = 0;
        phase_ = Phase::Hosting;
        clientsWere_ = 0;
        ended_ = "";
        startPending_ = false;
        return true;
    }

    bool NetGame::scan()
    {
        if (!netlink::scan())
            return false;
        match_ = NetMatch();
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

    bool NetGame::join(int index)
    {
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
        if (netPlaced(match_) && (phase_ == Phase::Hosting || phase_ == Phase::Lobby || phase_ == Phase::Playing))
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
        match_ = NetMatch();
        phase_ = Phase::Off;
        startPending_ = false;
    }

    void NetGame::lost(const char *why)
    {
        netlink::powerDown();
        effectDraw.dropSprays();
        match_ = NetMatch();
        phase_ = Phase::Off;
        startPending_ = false;
        ended_ = why;
    }

    void NetGame::start(const std::string &map, int fragLimit, int minuteLimit, int skill)
    {
        if (phase_ != Phase::Hosting)
            return;
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
        startPending_ = true;
        // (Said before this console goes quiet to load its own map.)
        for (int n = 0; n < 6; ++n)
        {
            frame();
            swiWaitForVBlank();
        }
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

        // What has come.
        std::uint8_t bytes[kNetCmdBytes];
        int from = 0;
        for (int size; (size = netlink::take(bytes, from)) > 0;)
        {
            gameApi.netRead(match_, bytes, size, from);
            ++heard_;
        }

        // The lobby's part of it; with a map loaded, the game's tick does
        // the rest.
        if (!match_.playing)
        {
            static LogicWorld none;
            static PlayerState nobody;
            gameApi.netLobby(match_, none, nobody);
            send();
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
            if (event.type == NetEventType::Start && !isHost())
            {
                const std::uint32_t hash = static_cast<std::uint16_t>(event.v[0]) |
                                           (static_cast<std::uint32_t>(static_cast<std::uint16_t>(event.v[1])) << 16);
                pendingMap_.clear();
                for (const std::string &map : maps_)
                {
                    if (nameHash(map.c_str()) == hash)
                        pendingMap_ = map;
                }
                match_.fragLimit = event.v[2];
                match_.minuteLimit = event.v[3];
                // (A host from before there was a choice says 0: MEDIUM.)
                pendingSkill_ = event.v[4] >= 1 && event.v[4] <= 3 ? event.v[4] : 2;
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
