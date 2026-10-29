#pragma once

// A match between consoles: HlNet.hpp's frames and events, joined to the
// game.
//
// Each console runs the whole game for its own player, as it does alone, and
// this is what it adds each frame (netTick, after the game's own tick):
//
//   - says where its player is (the state in the next frame);
//   - turns what its player did to the others into events: a shot fired, a
//     hit on another player (a CLAIM: the shooter's console found it, with
//     the other's own hit boxes, and the victim's console takes it), a
//     grenade gone off, something of the map used or struck, a pickup
//     wanted, its own death;
//   - takes the others' events: is hurt by their claims, hears their shots,
//     sees their grenades burst, uses and strikes the map as they did;
//   - puts the others where they say they are: each an actor of the kind
//     Player, with the players' model, gliding between what is heard;
//   - has each of them walk into the map's triggers, doors and lifts here
//     too (logicGhostTouch), so that the same things move on every console.
//
// The host, besides, says who has a pickup when it is wanted, brings it
// back after its time, keeps the scores, and ends the match.
//
// std-only: the host tests play matches with it (test_net).

#include "HlMonsters.hpp"
#include "HlNet.hpp"

namespace hldata
{
    inline constexpr int kNetRespawnMs = 3000;   ///< dead this long, then back
    inline constexpr int kNetWeaponBackMs = 20000; ///< a weapon or ammunition taken is back after this
    inline constexpr int kNetItemBackMs = 30000;   ///< ... a medkit or a battery after this
    inline constexpr int kNetBlameMs = 5000;     ///< a death this soon after a hit is the hitter's
    /// The health another player's actor is kept at: nobody dies of it, and
    /// what is missing from it is what this console's player did to them.
    inline constexpr std::int16_t kNetActorHealth = 20000;

    /// Where a player may come into the map (its info_player_deathmatch).
    struct NetSpawn
    {
        fx pos[3];
        std::uint16_t yaw;
    };

    /// Something for whoever draws the screen to say: who killed whom, who
    /// came, who went, the match over.
    struct NetNotice
    {
        NetEventType type = NetEventType::None;
        std::uint8_t who = 0;   ///< the player it is about
        std::uint8_t other = 0; ///< (a death: who did it; kNetPlayers: nobody else)
    };

    struct NetMatch
    {
        enum class Role : std::uint8_t
        {
            Off,
            Host,
            Client,
        };
        Role role = Role::Off;
        int me = 0; ///< which player this console is
        NetHost host;
        NetClient client;

        char names[kNetPlayers][kNetTextBytes] = {};
        char myName[kNetTextBytes] = {};
        std::int16_t frags[kNetPlayers] = {};
        std::int16_t deaths[kNetPlayers] = {};

        bool playing = false; ///< a map is loaded and the match is on
        bool over = false;
        int fragLimit = 0;    ///< 0: none
        int minuteLimit = 0;  ///< 0: none
        std::int32_t playedMs = 0;

        // The others, here.
        int firstActor = -1; ///< player p is actor firstActor + p
        int model = -1;      ///< the players' model, in the map's ModelRefs
        NetGlide glide[kNetPlayers];
        std::uint8_t pose[kNetPlayers] = {}; ///< which clip each is in (kPose*)

        // This console's own player.
        NetPlayer mine;
        std::uint8_t spawns = 0;
        bool wasAlive = false;
        std::int32_t deadMs = 0;
        int blamed = kNetPlayers;  ///< who last hit them
        std::int32_t blameMs = 0;  ///< ... and for how much longer that counts
        /// Set when the player has just been put back into the map: the
        /// console puts the view and the weapon in hand to rights, and
        /// clears it.
        bool respawned = false;

        const NetSpawn *spawnPoints = nullptr;
        int spawnCount = 0;
        std::uint32_t seed = 1;

        // Grenades in flight a tick ago: one that is gone has gone off.
        bool shotWas[kMaxProjectiles] = {};
        fx shotAt[kMaxProjectiles][3] = {};

        // The host: what has been taken, and when it is back.
        struct Taken
        {
            std::int16_t actor = -1;
            std::int32_t ms = 0;
        };
        static constexpr int kMostTaken = 96;
        Taken taken[kMostTaken];

        static constexpr int kMostNotices = 8;
        NetNotice notices[kMostNotices];
        int noticeCount = 0;
        /// Events the match itself does nothing with (the lobby's: a start,
        /// an end, a goodbye), for whoever runs the screens.
        NetEvent passed[kMostNotices];
        int passedCount = 0;
    };

    inline constexpr std::uint8_t kPoseIdle = 1;
    inline constexpr std::uint8_t kPoseWalk = 2;
    inline constexpr std::uint8_t kPoseRun = 3;
    inline constexpr std::uint8_t kPoseCrouch = 4;
    inline constexpr std::uint8_t kPoseCrawl = 5;
    inline constexpr std::uint8_t kPoseJump = 6;
    inline constexpr std::uint8_t kPoseDead = 7;

    /// Player `p` as last heard (this console's own: as it last said).
    inline const NetPlayer &netPlayer(const NetMatch &match, int p)
    {
        if (p == match.me)
            return match.mine;
        return match.role == NetMatch::Role::Host ? match.host.players[p] : match.client.players[p];
    }
    /// Whether this console has its place in the game: the host always; a
    /// client once the host has said which player it is.
    inline bool netPlaced(const NetMatch &match)
    {
        return match.role == NetMatch::Role::Host || (match.role == NetMatch::Role::Client && match.me != 0);
    }
    inline bool netHere(const NetMatch &match, int p)
    {
        return p == match.me || (netPlayer(match, p).flags & kNetHere) != 0;
    }

    /// Say something to everyone.
    inline void netSay(NetMatch &match, NetEvent event)
    {
        event.from = static_cast<std::uint8_t>(match.me);
        if (match.role == NetMatch::Role::Host)
            match.host.out.add(event);
        else if (match.role == NetMatch::Role::Client)
            match.client.out.add(event);
    }
    inline NetEvent netEvent(NetEventType type, int a = 0, int v0 = 0, int v1 = 0, int v2 = 0, int v3 = 0,
                             int v4 = 0, int v5 = 0)
    {
        NetEvent e;
        e.type = type;
        e.a = static_cast<std::uint8_t>(a);
        const int v[6] = {v0, v1, v2, v3, v4, v5};
        for (int i = 0; i < 6; ++i)
            e.v[i] = static_cast<std::int16_t>(v[i]);
        return e;
    }

    namespace detail
    {
        inline void netNotice(NetMatch &match, NetEventType type, int who, int other = kNetPlayers)
        {
            if (match.noticeCount >= NetMatch::kMostNotices)
                return;
            NetNotice &notice = match.notices[match.noticeCount++];
            notice.type = type;
            notice.who = static_cast<std::uint8_t>(who);
            notice.other = static_cast<std::uint8_t>(other);
        }

        /// Where player `p` is, as someone the map's logic can be asked
        /// about: the middle of them, standing or crouched.
        inline PlayerState netGhost(const NetMatch &match, const LogicWorld &w, int p)
        {
            const NetPlayer &heard = netPlayer(match, p);
            PlayerState ghost;
            for (int i = 0; i < 3; ++i)
            {
                ghost.origin[i] = p == match.me ? heard.pos[i] * 1024 : match.glide[p].pos[i];
                ghost.velocity[i] = heard.velocity[i] * 8 * kFxOne;
            }
            ghost.ducked = (heard.flags & kNetCrouched) != 0;
            ghost.onGround = (heard.flags & kNetOnGround) != 0;
            ghost.health = 100;
            fx sine, cosine;
            fxSinCos(heard.yaw, sine, cosine);
            ghost.facingXY[0] = cosine;
            ghost.facingXY[1] = sine;
            // What they stand on, for the lifts: looked for under them.
            if (ghost.onGround && w.world != nullptr)
            {
                const fx below[3] = {ghost.origin[0], ghost.origin[1], ghost.origin[2] - fxFromInt(4)};
                ghost.groundMover = traceAll(*w.world, ghost.ducked, ghost.origin, below).mover;
            }
            return ghost;
        }

        /// The host: player `victim` has died, by `killer` (or by nobody
        /// else). The scores, to everyone.
        inline void netScore(NetMatch &match, int victim, int killer)
        {
            ++match.deaths[victim];
            if (killer < kNetPlayers && killer != victim)
                ++match.frags[killer];
            else
                --match.frags[victim];
            for (int p : {victim, killer})
            {
                if (p < kNetPlayers)
                    match.host.out.add(netEvent(NetEventType::Score, p, match.frags[p], match.deaths[p]));
            }
        }

        /// The host: the item that is actor `index` is `who`'s; it is back
        /// after its time.
        inline void netGrant(NetMatch &match, LogicWorld &w, PlayerState &player, int index, int who)
        {
            ActorWorld &a = *w.actors;
            if (index < 0 || index >= a.count || !a.states[index].active ||
                a.states[index].kind != static_cast<std::uint8_t>(ActorKind::Item))
                return; // (someone else has it already)
            const auto item = static_cast<ItemKind>(a.states[index].item);
            if (who == match.me)
                itemPickUp(w, player, index);
            a.states[index].active = false;
            actorPlaceBox(a, index);
            match.host.out.add(netEvent(NetEventType::Taken, who, index));
            for (NetMatch::Taken &slot : match.taken)
            {
                if (slot.actor >= 0)
                    continue;
                slot.actor = static_cast<std::int16_t>(index);
                slot.ms = item == ItemKind::HealthKit || item == ItemKind::Battery ? kNetItemBackMs : kNetWeaponBackMs;
                break;
            }
        }

        /// An event from another console, taken here.
        inline void netApply(NetMatch &match, LogicWorld &w, PlayerState &player, const NetEvent &event)
        {
            // (No map is loaded in the lobby: `playing` guards everything
            // that touches one.)
            const int from = event.from;
            const bool host = match.role == NetMatch::Role::Host;
            switch (event.type)
            {
            case NetEventType::Hello:
                if (host && from > 0 && from < kNetPlayers)
                {
                    std::memcpy(match.names[from], event.text, kNetTextBytes);
                    match.names[from][kNetTextBytes - 1] = 0;
                    // Everyone's name to everyone: the newcomer has none yet.
                    for (int p = 0; p < kNetPlayers; ++p)
                    {
                        if (!netHere(match, p) && p != from)
                            continue;
                        NetEvent roster;
                        roster.type = NetEventType::Roster;
                        roster.a = static_cast<std::uint8_t>(p);
                        std::memcpy(roster.text, match.names[p], kNetTextBytes);
                        match.host.out.add(roster);
                    }
                    netNotice(match, NetEventType::Hello, from);
                }
                break;
            case NetEventType::Roster:
                if (!host && event.a < kNetPlayers)
                {
                    std::memcpy(match.names[event.a], event.text, kNetTextBytes);
                    match.names[event.a][kNetTextBytes - 1] = 0;
                }
                break;
            case NetEventType::Shot:
                if (from != match.me && from < kNetPlayers && match.playing && w.heard != nullptr &&
                    event.a > kWeaponCrowbar && event.a < kWeaponCount && event.a != kWeaponGrenade)
                    w.heard->add(gunKind(event.a).fire[0], match.glide[from].pos, 110, kRadiusLarge);
                break;
            case NetEventType::Hit:
                if (event.a == match.me && from != match.me && match.playing && player.health > 0 &&
                    static_cast<std::uint8_t>(event.v[1]) == match.spawns)
                {
                    playerHurt(player, event.v[0], from < kNetPlayers ? match.glide[from].pos : nullptr);
                    match.blamed = from;
                    match.blameMs = kNetBlameMs;
                }
                break;
            case NetEventType::Burst:
                if (from != match.me && match.playing)
                {
                    const fx at[3] = {event.v[0] * 1024, event.v[1] * 1024, event.v[2] * 1024};
                    explodeAt(w, player, at, 100, true, true, false);
                }
                break;
            case NetEventType::Died:
                if (from != match.me)
                    netNotice(match, NetEventType::Died, from, event.a);
                if (host)
                    netScore(match, from, event.a);
                break;
            case NetEventType::Score:
                if (!host && event.a < kNetPlayers)
                {
                    match.frags[event.a] = event.v[0];
                    match.deaths[event.a] = event.v[1];
                }
                break;
            case NetEventType::Take:
                if (host && match.playing)
                    netGrant(match, w, player, event.v[0], from);
                break;
            case NetEventType::Taken:
                if (!host && match.playing && event.v[0] >= 0 && event.v[0] < w.actors->count)
                {
                    ActorWorld &a = *w.actors;
                    if (event.a == match.me && a.states[event.v[0]].active)
                        itemPickUp(w, player, event.v[0]);
                    a.states[event.v[0]].active = false;
                    actorPlaceBox(a, event.v[0]);
                }
                break;
            case NetEventType::Back:
                if (!host && match.playing && event.v[0] >= 0 && event.v[0] < w.actors->count)
                {
                    w.actors->states[event.v[0]].active = true;
                    actorPlaceBox(*w.actors, event.v[0]);
                }
                break;
            case NetEventType::Fire:
                if (from != match.me && from < kNetPlayers && match.playing && event.v[0] >= 0 &&
                    event.v[0] < w.recordCount)
                {
                    // As they did it, by someone standing where they stand;
                    // and nothing of it told back.
                    PlayerState ghost = netGhost(match, w, from);
                    const bool sharing = w.sharing;
                    w.sharing = false;
                    if (event.a == 0)
                        logicUse(w, ghost, event.v[0], -1, kUseSet);
                    else
                        logicStrike(w, ghost, event.v[0], event.v[1]);
                    w.sharing = sharing;
                }
                break;
            case NetEventType::End:
                match.over = true;
                [[fallthrough]];
            case NetEventType::Start:
            case NetEventType::Bye:
                if (match.passedCount < NetMatch::kMostNotices)
                    match.passed[match.passedCount++] = event;
                break;
            default:
                break;
            }
        }
    } // namespace detail

    // ---------------------------------------------------------- the frames

    /// This console's frame for the air; 0 if there is none to send.
    inline int netWrite(NetMatch &match, std::uint8_t *bytes)
    {
        if (match.role == NetMatch::Role::Host)
            return netHostWrite(match.host, match.mine, bytes);
        if (match.role == NetMatch::Role::Client)
            return netClientWrite(match.client, match.mine, bytes);
        return 0;
    }
    /// A frame from the air: from client `who`, if this is the host (the
    /// radio says which).
    inline void netRead(NetMatch &match, const std::uint8_t *bytes, int size, int who = 0)
    {
        if (match.role == NetMatch::Role::Host)
            netHostRead(match.host, who, bytes, size);
        else if (match.role == NetMatch::Role::Client)
            netClientRead(match.client, bytes, size);
    }

    // ------------------------------------------------------------ the match

    /// Become the host, or a client. A client is told by the host which
    /// player it is; `token` is what it calls itself until then (anything
    /// but 0, and not what another client is likely to choose).
    inline void netBegin(NetMatch &match, NetMatch::Role role, std::uint16_t token, const char *name)
    {
        match = NetMatch();
        match.role = role;
        match.me = 0;
        match.client.token = token != 0 ? token : 1;
        match.mine.flags = kNetHere;
        std::strncpy(match.myName, name, kNetTextBytes - 1);
        std::strncpy(match.names[0], role == NetMatch::Role::Host ? name : "", kNetTextBytes - 1);
        if (role == NetMatch::Role::Client)
        {
            NetEvent hello;
            hello.type = NetEventType::Hello;
            std::strncpy(hello.text, name, kNetTextBytes - 1);
            netSay(match, hello);
        }
    }

    /// The local player is put into the map, at one of its places to come
    /// in at, as they are when the game starts them.
    inline void netSpawn(NetMatch &match, LogicWorld &w, PlayerState &player)
    {
        // Any of the places nobody is near, by chance (as the game does);
        // if there is someone near every one, the one they are farthest
        // from.
        int best = -1, clear = 0;
        std::int64_t bestNearest = -1;
        for (int s = 0; s < match.spawnCount; ++s)
        {
            std::int64_t nearest = INT64_MAX / 2;
            for (int p = 0; p < kNetPlayers; ++p)
            {
                if (p == match.me || !(netPlayer(match, p).flags & kNetAlive))
                    continue;
                std::int64_t far = 0;
                for (int i = 0; i < 3; ++i)
                {
                    const std::int64_t d = fxToInt(match.spawnPoints[s].pos[i]) - netPlayer(match, p).pos[i] / 4;
                    far += d * d;
                }
                nearest = far < nearest ? far : nearest;
            }
            if (nearest >= 384 * 384)
            {
                // (Each clear place in turn has one chance in as many as
                // there have been of being the one: any of them, evenly.)
                match.seed = match.seed * 1664525u + 1013904223u;
                if (((match.seed >> 16) % static_cast<std::uint32_t>(++clear)) == 0)
                    best = s;
            }
            else if (clear == 0 && nearest > bestNearest)
            {
                bestNearest = nearest;
                best = s;
            }
        }
        {
            // (A new life carries nothing of the last: the later weapons
            // neither -- but they are still the game's to keep.)
            Arsenal *keep = player.arsenal;
            const Cheats *cheats = player.cheats;
            player = PlayerState();
            player.arsenal = keep;
            player.cheats = cheats;
            if (keep != nullptr)
                *keep = Arsenal();
        }
        if (best >= 0)
        {
            for (int i = 0; i < 3; ++i)
                player.origin[i] = match.spawnPoints[best].pos[i];
            fx sine, cosine;
            fxSinCos(match.spawnPoints[best].yaw, sine, cosine);
            player.facingXY[0] = cosine;
            player.facingXY[1] = sine;
        }
        player.inv.have = static_cast<std::uint8_t>(1u | (1u << kWeaponCrowbar) | (1u << kWeaponGlock));
        player.inv.current = kWeaponGlock;
        player.inv.clipGlock = 17;
        player.inv.ammo9mm = 68;
        ++match.spawns;
        match.wasAlive = true;
        match.deadMs = 0;
        match.blamed = kNetPlayers;
        match.blameMs = 0;
        match.respawned = true;
        if (w.actors != nullptr)
            w.actors->wanted = -1;
        netSay(match, netEvent(NetEventType::Spawned));
    }

    /// The map is loaded: the match begins on this console. `firstActor` is
    /// the first of kNetPlayers actor slots that are the map's to spare,
    /// `model` the players' model in its ModelRefs (-1: it has none, and
    /// the others are not seen).
    inline void netStart(NetMatch &match, LogicWorld &w, PlayerState &player, int firstActor, int model,
                         const NetSpawn *spawns, int spawnCount, std::uint32_t seed)
    {
        ActorWorld &a = *w.actors;
        match.firstActor = firstActor;
        match.model = model;
        match.spawnPoints = spawns;
        match.spawnCount = spawnCount;
        match.seed = seed | 1;
        match.playing = true;
        player.suit.value.silent = true;
        match.over = false;
        match.playedMs = 0;
        for (int p = 0; p < kNetPlayers; ++p)
        {
            match.frags[p] = match.deaths[p] = 0;
            match.glide[p] = NetGlide();
            match.pose[p] = 0;
        }
        for (NetMatch::Taken &slot : match.taken)
            slot = NetMatch::Taken();
        a.shared = true;
        a.wanted = -1;
        w.sharing = true;
        w.struckCount = 0;
        // Nobody of the map's own is in a match: no monsters, no people.
        // What lies about to be taken stays, and the furniture.
        for (int i = 0; i < a.count; ++i)
        {
            ActorState &actor = a.states[i];
            const bool stays = actor.kind == static_cast<std::uint8_t>(ActorKind::Item) ||
                               actor.kind == static_cast<std::uint8_t>(ActorKind::Furniture);
            if (actor.active && !stays)
            {
                actor.active = false;
                actorPlaceBox(a, i);
            }
        }
        for (int s = 0; s < kMaxProjectiles; ++s)
            match.shotWas[s] = false;
        netSpawn(match, w, player);
    }

    /// The local player used the map's record `index` (a button, a door, a
    /// charger): the others are told.
    inline void netUsed(NetMatch &match, int index)
    {
        if (match.playing)
            netSay(match, netEvent(NetEventType::Fire, 0, index));
    }

    namespace detail
    {
        /// A client: which player it is, once the host has said.
        inline void netPlace(NetMatch &match)
        {
            if (match.role == NetMatch::Role::Client && match.me == 0 && match.client.me != 0)
            {
                match.me = match.client.me;
                std::memcpy(match.names[match.me], match.myName, kNetTextBytes);
            }
        }

        /// The events that have come, taken; and on the host, passed on.
        inline void netEvents(NetMatch &match, LogicWorld &w, PlayerState &player)
        {
            const bool host = match.role == NetMatch::Role::Host;
            NetEvent event;
            if (host)
            {
                for (int c = 1; c < kNetPlayers; ++c)
                {
                    while (match.host.clients[c].here && match.host.clients[c].in.take(event))
                    {
                        detail::netApply(match, w, player, event);
                        // What the other clients need of it, passed on.
                        switch (event.type)
                        {
                        case NetEventType::Shot:
                        case NetEventType::Hit:
                        case NetEventType::Burst:
                        case NetEventType::Died:
                        case NetEventType::Spawned:
                        case NetEventType::Fire:
                        case NetEventType::Bye:
                            match.host.out.add(event);
                            break;
                        default:
                            break;
                        }
                    }
                }
            }
            else
            {
                while (match.client.in.take(event))
                    detail::netApply(match, w, player, event);
            }
        }
    } // namespace detail

    /// Once a frame while there is no map loaded (the lobby): who is here,
    /// what they are called, and whatever else is said. (`none` and `nobody`
    /// are never looked at: nothing that needs a map is done.)
    inline void netLobby(NetMatch &match, LogicWorld &none, PlayerState &nobody)
    {
        if (match.role == NetMatch::Role::Off)
            return;
        detail::netPlace(match);
        match.mine = NetPlayer();
        match.mine.flags = kNetHere;
        detail::netEvents(match, none, nobody);
    }

    /// Once a frame, after the game's own tick. `yaw` and `pitch` are the
    /// view's (65536 to the circle), `did` what weaponsTick said it did
    /// (kWeapon*), `lamp` whether the suit's lamp is on.
    inline void netTick(NetMatch &match, LogicWorld &w, PlayerState &player, std::uint16_t yaw, std::int32_t pitch,
                        std::uint32_t did, bool lamp, int ms)
    {
        if (match.role == NetMatch::Role::Off)
            return;
        if (!match.playing)
        {
            // (No map: only what is said in the lobby.)
            static LogicWorld none;
            static PlayerState nobody;
            netLobby(match, none, nobody);
            return;
        }
        ActorWorld &a = *w.actors;
        const bool host = match.role == NetMatch::Role::Host;
        detail::netPlace(match);

        // ---- what this console's player did
        if (match.playing)
        {
            // What their shots and blows took off the others: a claim each.
            for (int p = 0; p < kNetPlayers && match.firstActor >= 0; ++p)
            {
                ActorState &actor = a.states[match.firstActor + p];
                if (p == match.me || !actor.active || actor.kind != static_cast<std::uint8_t>(ActorKind::Player))
                    continue;
                const int hurt = kNetActorHealth - actor.health;
                actor.health = kNetActorHealth;
                // (Which of their lives it was: a hit on one that is over
                // by the time it arrives is not taken by the next.)
                if (hurt > 0 && player.health > 0)
                    netSay(match, netEvent(NetEventType::Hit, p, hurt > 30000 ? 30000 : hurt, netPlayer(match, p).spawns));
            }
            // What they struck of the map's.
            for (int n = 0; n < w.struckCount; ++n)
                netSay(match, netEvent(NetEventType::Fire, 1, w.struck[n * 2], w.struck[n * 2 + 1]));
            w.struckCount = 0;
            // What they would pick up.
            if (a.wanted >= 0)
            {
                if (host)
                    detail::netGrant(match, w, player, a.wanted, match.me);
                else
                    netSay(match, netEvent(NetEventType::Take, 0, a.wanted));
                // (Asked once: it is asked again if they are still on it a
                // second from now and nobody has it.)
                a.wanted = -2;
            }
            if (did & kWeaponFired)
                netSay(match, netEvent(NetEventType::Shot, player.inv.current, yaw, pitch,
                                       (did & kWeaponSecondary) ? 1 : 0));
            // A grenade that was in flight and is not: it went off there.
            for (int s = 0; s < kMaxProjectiles; ++s)
            {
                const Projectile &shot = a.shots[s];
                const bool is = shot.active && (shot.kind == kShotGrenade || shot.kind == kShotContact ||
                                                  shot.kind == kShotRocket || shot.kind == kShotSatchel);
                if (match.shotWas[s] && !is)
                    netSay(match, netEvent(NetEventType::Burst, 0, match.shotAt[s][0] >> 10, match.shotAt[s][1] >> 10,
                                           match.shotAt[s][2] >> 10));
                match.shotWas[s] = is;
                for (int i = 0; is && i < 3; ++i)
                    match.shotAt[s][i] = shot.pos[i];
            }
            // Their death; and after a while, their return.
            if (match.blameMs > 0)
                match.blameMs -= ms;
            if (player.health <= 0)
            {
                if (match.wasAlive)
                {
                    match.wasAlive = false;
                    match.deadMs = 0;
                    const int killer = match.blameMs > 0 ? match.blamed : kNetPlayers;
                    netSay(match, netEvent(NetEventType::Died, killer));
                    detail::netNotice(match, NetEventType::Died, match.me, killer);
                    if (host)
                        detail::netScore(match, match.me, killer);
                }
                match.deadMs += ms;
                if (match.deadMs >= kNetRespawnMs && !match.over)
                    netSpawn(match, w, player);
            }
        }
        // (The pickup asked for a second ago and not given: ask again.)
        if (a.wanted == -2 && ((match.playedMs / 1000) != ((match.playedMs + ms) / 1000)))
            a.wanted = -1;

        // ---- where they are, for the next frame
        NetPlayer &mine = match.mine;
        for (int i = 0; i < 3; ++i)
        {
            const std::int32_t quarter = player.origin[i] >> 10;
            mine.pos[i] = static_cast<std::int16_t>(quarter < -32768 ? -32768 : (quarter > 32767 ? 32767 : quarter));
            const std::int32_t speed = fxToInt(player.velocity[i]) / 8;
            mine.velocity[i] = static_cast<std::int8_t>(speed < -127 ? -127 : (speed > 127 ? 127 : speed));
        }
        mine.yaw = yaw;
        mine.pitch = static_cast<std::int8_t>(pitch >> 8);
        mine.weapon = player.inv.current;
        mine.health = static_cast<std::uint8_t>(player.health < 0 ? 0 : (player.health > 255 ? 255 : player.health));
        mine.spawns = match.spawns;
        mine.flags = static_cast<std::uint8_t>(kNetHere | (match.playing ? kNetInGame : 0) |
                                               (match.playing && player.health > 0 ? kNetAlive : 0) |
                                               (player.ducked ? kNetCrouched : 0) | (player.onGround ? kNetOnGround : 0) |
                                               ((did & kWeaponFired) ? kNetFiring : 0) | (lamp ? kNetLamp : 0));

        // ---- what the others did
        detail::netEvents(match, w, player);

        // ---- the others, here
        for (int p = 0; p < kNetPlayers && match.firstActor >= 0; ++p)
        {
            if (p == match.me)
                continue;
            ActorState &actor = a.states[match.firstActor + p];
            const NetPlayer &heard = netPlayer(match, p);
            const bool here = (heard.flags & kNetHere) && (heard.flags & kNetInGame) && match.model >= 0;
            if (!here)
            {
                if (actor.active)
                {
                    actor.active = false;
                    actorPlaceBox(a, match.firstActor + p);
                }
                match.glide[p].placed = false;
                match.pose[p] = 0;
                continue;
            }
            netGlide(match.glide[p], heard, ms);
            const bool alive = (heard.flags & kNetAlive) != 0;
            const bool crouched = (heard.flags & kNetCrouched) != 0;
            if (!actor.active || actor.kind != static_cast<std::uint8_t>(ActorKind::Player))
            {
                actor = ActorState();
                actor.active = true;
                actor.kind = static_cast<std::uint8_t>(ActorKind::Player);
                actor.model = static_cast<std::uint16_t>(match.model);
                actor.health = kNetActorHealth;
                match.pose[p] = 0;
            }
            // (An actor is where its feet are; a player, where their middle
            // is.)
            actor.pos[0] = match.glide[p].pos[0];
            actor.pos[1] = match.glide[p].pos[1];
            actor.pos[2] = match.glide[p].pos[2] - fxFromInt(crouched ? 18 : 36);
            actor.yaw = heard.yaw;
            actor.mood = crouched ? 1 : 0;
            actor.flags = alive ? kActorSolid : 0;
            // (Dead, they are no longer shot at: what is dead is passed by.)
            actor.ai = alive ? 0 : kAiDead;
            // What they are doing, as a body does it.
            const int speed = (heard.velocity[0] < 0 ? -heard.velocity[0] : heard.velocity[0]) +
                              (heard.velocity[1] < 0 ? -heard.velocity[1] : heard.velocity[1]);
            const std::uint8_t pose = !alive ? kPoseDead
                                      : !(heard.flags & kNetOnGround) ? kPoseJump
                                      : crouched ? (speed > 2 ? kPoseCrawl : kPoseCrouch)
                                      : speed > 22 ? kPoseRun
                                      : speed > 2 ? kPoseWalk
                                                  : kPoseIdle;
            if (pose != match.pose[p])
            {
                static constexpr std::uint32_t kClips[] = {0,
                                                           nameHash("look_idle"),
                                                           nameHash("walk"),
                                                           nameHash("run"),
                                                           nameHash("crouch_idle"),
                                                           nameHash("crawl"),
                                                           nameHash("jump"),
                                                           nameHash("die_simple")};
                actorPlay(a, actor, kClips[pose], pose == kPoseDead || pose == kPoseJump);
                match.pose[p] = pose;
            }
            actorPlaceBox(a, match.firstActor + p);
            // ... and walking into the map's things here, as they do there.
            if (alive)
            {
                PlayerState ghost = detail::netGhost(match, w, p);
                logicGhostTouch(w, ghost);
            }
        }

        // ---- the host's own business
        match.playedMs += ms;
        if (!host)
            return;
        for (NetMatch::Taken &slot : match.taken)
        {
            if (slot.actor < 0 || (slot.ms -= ms) > 0)
                continue;
            a.states[slot.actor].active = true;
            actorPlaceBox(a, slot.actor);
            match.host.out.add(netEvent(NetEventType::Back, 0, slot.actor));
            slot.actor = -1;
        }
        if (!match.over)
        {
            bool done = match.minuteLimit > 0 && match.playedMs >= match.minuteLimit * 60000;
            for (int p = 0; p < kNetPlayers; ++p)
                done = done || (match.fragLimit > 0 && match.frags[p] >= match.fragLimit);
            if (done)
            {
                match.over = true;
                match.host.out.add(netEvent(NetEventType::End));
                detail::netNotice(match, NetEventType::End, 0);
            }
        }
    }
} // namespace hldata
