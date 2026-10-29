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

#include <string>
#include <vector>

#include "HlNetGame.hpp"

namespace hlrt
{
    class NetGame
    {
    public:
        enum class Phase
        {
            Off,
            Hosting,  ///< a host, in its lobby
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

        /// The maps a match can be played on (the card's, by the manifest).
        void setMaps(std::vector<std::string> maps) { maps_ = std::move(maps); }
        const std::vector<std::string> &maps() const { return maps_; }

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
        /// The team this console's player would rather be on (1, 2; 0:
        /// either). The host says which they are on.
        void askTeam(int team);
        /// How strong the link to player `p` is, in bars (0..4); -1 if it is
        /// not known (a client knows only its host's).
        int bars(int p) const;

        /// The host: begin the match on `map`. (It then loads the map
        /// itself, as the clients do when they hear.)
        void start(const std::string &map, int fragLimit, int minuteLimit, int skill);
        /// Whether a match has been started and its map is to be loaded:
        /// which map, and how hard the host made it (kSkill*). True once per
        /// start, on every console.
        bool startAsked(std::string &map, int &skill);
        /// The map is loaded: the match begins here.
        void mapLoaded();

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

        void send();
        void sayProfile();
        void sayRules();
        void lost(const char *why);
    };

    HL_ENGINE_OBJECT(NetGame, netGame);
} // namespace hlrt
