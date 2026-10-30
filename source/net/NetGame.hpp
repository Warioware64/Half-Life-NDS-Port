#pragma once

#include "EngineObject.hpp"

// A game between consoles, on this one: the radio (NetLink) joined to the
// match (shared/HlNetGame.hpp) and to the game.
//
// frame() is called once a frame whatever is on the screen: it reads what
// has come, keeps account of who is there, and -- while no map is loaded --
// says this console's part of the lobby. With a map loaded, the game calls
// tick() after its own tick instead: that is where what the player did goes
// out and what the others did comes in.

#include <cstdio>
#include <string>
#include <vector>

#include "HlNetAir.hpp"
#include "HlNetGame.hpp"
#include "HlNetMaps.hpp"

namespace hlrt
{
    class NetGame
    {
    public:
        enum class Phase
        {
            Off,
            Hosting,  ///< a host, in its lobby
            Serving,  ///< ... sending the match's map to the consoles it gave the game to, before it begins
            Scanning, ///< looking for hosts
            Joining,  ///< asking one to be let in
            Lobby,    ///< a client, in the host's lobby
            Playing,  ///< a map is loaded and the match is on
        };

        Phase phase() const { return phase_; }
        bool on() const { return phase_ != Phase::Off; }
        bool playing() const { return phase_ == Phase::Playing; }
        bool isHost() const { return match_.role == hldata::NetMatch::Role::Host; }
        const hldata::NetMatch &match() const { return match_; }
        hldata::NetMatch &match() { return match_; }

        /// The maps a match can be played on (the card's, by the manifest),
        /// with what is known of each.
        void setMaps(std::vector<hldata::NetMapInfo> maps)
        {
            maps_.clear();
            for (const hldata::NetMapInfo &map : maps)
                maps_.push_back(map.name);
            mapInfo_ = std::move(maps);
        }
        const std::vector<std::string> &maps() const { return maps_; }
        const std::vector<hldata::NetMapInfo> &mapInfo() const { return mapInfo_; }

        // ------------------------------------------------------------ lobby
        bool host();  ///< be a host; false if the radio would not start
        bool scan();  ///< look for hosts
        /// The hosts found so far: up to `most` of them, each a line to
        /// show ("name 2/4"), what its beacon says of the match, how strong
        /// its signal is, and what to give join().
        struct Found
        {
            char line[32];
            char name[16];
            int index;
            int players, most;
            /// What the host says of its match; `room.version` 0 if it says
            /// nothing (an older build), and another than kNetVersion if it
            /// is a build this one cannot play with.
            hldata::NetRoomInfo room;
            const char *map; ///< its map, if this card has it; else null
            int bars;        ///< 0..4
        };
        int found(Found *out, int most);
        bool join(int index);
        /// A console started by Download Play: the host is whoever started
        /// it.
        bool joinStarter();
        /// Out of it altogether: the others are told, the radio is silent.
        void leave();
        /// The host: the settings' match (its mode, its rules, its map) has
        /// changed -- to the others in the lobby, and to the beacon.
        void hostChanged();
        /// The profile was changed (PROFILE, from the lobby): said to the
        /// others again.
        void sayProfile();
        /// The team this console's player would rather be on (1, 2; 0:
        /// either). The host says which they are on.
        void askTeam(int team);
        /// How strong the link to player `p` is, in bars (0..4); -1 if it is
        /// not known (a client knows only its host's).
        int bars(int p) const;
        /// The map of the match being waited for: the host's own, or (a
        /// client) the one its beacon said, if this card has it; "" if not.
        const char *lobbyMap() const;

        /// The host: begin the match on `map`. (It then loads the map
        /// itself, as the clients do when they hear.) With consoles it gave
        /// the game to among them, it first sends them the map (Serving).
        void start(const std::string &map, int fragLimit, int minuteLimit, int skill);
        /// The host: what it is sending the consoles it gave the game to, if
        /// it is -- which stream ("the game": the common one; a group's
        /// base; a map), to how many, and how far the slowest has got.
        bool sending(const char *&what, int &consoles, int &percent) const;
        /// How many of its clients are consoles it gave the game to.
        int guests() const;
#ifdef HLDS_CHILD
        /// A console that was sent the game: a stream the host has begun
        /// sending it, to be taken now (GuestLoad's receiveAir, or a map
        /// loaded). True once per stream.
        bool airAsked(hldata::AirHead &head);
#endif
        /// The host: the match about to begin is not to (CANCEL): everyone
        /// back to the lobby.
        void cancelStart();
        /// The host, on its STARTING page: how player `p` stands, in a word
        /// into `line`. 0: nobody there; 1: on the way; 2: ready.
        int startStatus(int p, char *line, int room) const;
        /// Every console: its match's map is loaded (a client says it is
        /// Ready), and it waits for the word to begin: goAsked, true once --
        /// the host's when everyone has said it (or long enough has gone by).
        void loadedMap();
        bool goAsked();
        /// The host gave up the match being started (true once): the map
        /// loaded for it is let go, and it is the lobby again.
        bool cancelled();
        /// A match is being begun (from START to the word to go).
        bool starting() const { return starting_; }
        /// Whether a match has been started and its map is to be loaded:
        /// which map, and how hard the host made it (kSkill*). True once per
        /// start, on every console.
        bool startAsked(std::string &map, int &skill);
        /// The map is loaded: the match begins here.
        void mapLoaded();
        /// The match is over and its scores have been shown: the map is to
        /// be left for the host's lobby, where the host may start another.
        /// True once per match.
        bool lobbyAsked();
        /// ... and it has been left (the map is gone): the lobby again, with
        /// whoever is still there.
        void toLobby();

        // ---------------------------------------------------------- frames
        void frame();
        /// After the game's tick (see HlNetGame.hpp's netTick).
        void tick(std::uint16_t yaw, std::int32_t pitch, std::uint32_t did, bool lamp, int ms);
        /// The local player used the map's record `index`.
        void used(int index);
        /// The local player sprays (the game's impulse 201): true if it went
        /// on a wall.
        bool spray();

        /// Something to say on the screen, if there is: a line of it into
        /// `line`. False when there is nothing more.
        bool notice(char *line, int room);
        /// Why this console is out of the game, if it is and did not ask to
        /// be: the host left, the link was lost. Empty otherwise; cleared
        /// by reading.
        const char *ended();
        /// The radio's own account of itself, for the debug readout.
        void status(char *line, int room) const;

    private:
        Phase phase_ = Phase::Off;
        hldata::NetMatch match_;
        std::vector<std::string> maps_;
        std::vector<hldata::NetMapInfo> mapInfo_;
        std::string lobbyMap_; ///< (a client: the host's map, as its beacon said when it was joined)
        std::vector<hldata::NetSpawn> spawns_;
        unsigned clientsWere_ = 0;
        int lostFrames_ = 0;
        const char *ended_ = "";
        std::string pendingMap_;
        int pendingSkill_ = 2; ///< (kSkillMedium)
        bool startPending_ = false;
        std::uint32_t sent_ = 0, heard_ = 0;
        std::uint32_t profilesSeen_ = 0; ///< the match's profileStamp, when the colours were last set
        std::uint32_t ticks_ = 0;
        int overMs_ = 0;      ///< how long the match has been over
        bool lobbyAsked_ = false;
        // Beginning a match: who has said Ready (the host's: bit n for
        // client n), and the words heard (a client's).
        bool starting_ = false, goAsked_ = false, cancelled_ = false;
        // A client's: the picture of the host's map, asked for in the lobby
        // by a console that has none of it (a stream of its own, whole in
        // one window), and the map it is of.
        hldata::AirReceiver preview_;
        std::uint8_t *previewRing_ = nullptr;
        std::uint32_t previewHash_ = 0, previewHad_ = 0, previewSince_ = 0;
        int previewLinger_ = 0;
        std::string previewName_;
        void previewTick();
        void previewFrame(const hldata::AirHead &head);
        int previewReply(std::uint8_t *bytes);
        void previewEnd();
        unsigned ready_ = 0;
        std::uint32_t loadedAt_ = 0;
        /// How long the host waits for those not ready once it is.
        static constexpr std::uint32_t kReadyVblanks = 20 * 60;
#ifndef HLDS_CHILD
        // (The host's.) What it sends the consoles it gave the game to.
        hldata::AirSender air_;
        unsigned guests_ = 0;        ///< bit n: client n has asked for a stream
        std::FILE *airFile_ = nullptr;
        std::uint16_t serves_ = 0;   ///< sendings made, for each one's number
        int serveStage_ = 0;         ///< (Serving: 0 waiting, 1 the base, 2 the map, 3 sent)
        int serveSkill_ = 2;
        bool airBegin(hldata::AirStream stream, const std::string &name, const std::string &path, unsigned mask,
                      int skill = 0);
        void airEnd();
        int airSend(int frames);
        void airAsk(int from, const std::uint8_t *bytes, int size);
        void serveNext();
#else
        hldata::AirHead airAsked_;
        bool airPending_ = false;
        std::uint8_t airReply_[hldata::kNetReplyBytes];
        int airReplySize_ = 0;
#endif

        void send();
        void sayRules();
        void lost(const char *why);
    };

    HL_ENGINE_OBJECT(NetGame, netGame);
} // namespace hlrt
