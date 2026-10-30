#pragma once

// The host gives the game to consoles without it, by DS Download Play: the
// program sent (child/, carried in this one's own files as dlplay/child.nds)
// to every console that comes to it from its Download Play menu, and started
// on all of them at once. Then the host opens its game again, and they join
// it of themselves (HlNetAir.hpp's parameter says where it is), to be sent
// the rest as they need it (NetGame: AirStream).
//
// The radio does one thing at a time: while this goes on, the host's game is
// closed. So it is done before anyone has joined. A console comes when its
// owner chooses the game in its menu; once the first has, more are waited
// for a few seconds (and no longer: those already there are waiting), then
// all who are there are sent it together -- one that comes while it is being
// sent would not be let in.
//
// Download Play starts only a program signed by Nintendo, unless the console
// has FlashMe (a DS, a DS Lite), or a Download Play that does not check.

#include <string>

namespace hlrt::dlsend
{
    enum class State
    {
        Off,
        Waiting,   ///< offered: no console has come yet
        Gathering, ///< some have: waiting a little for more
        Sending,
        Starting,  ///< all have it: told to start, the game about to open again
        Done,      ///< (let go: the host's game is to be opened)
        Failed,
    };

    /// Whether this program carries one to send.
    bool available();
    /// Offer it, `description` under its name in the consoles' menus, for
    /// up to `most` players (this one among them). False, and why, if it
    /// cannot be (the radio, the memory).
    bool begin(const char *description, int most, std::string &why);
    /// Once a frame while it is going on.
    void frame();
    State state();
    /// The consoles that have come: bit n is console n (1..15).
    unsigned consoles();
    /// How console `n` stands, in a word or a number ("checking", "42%",
    /// "ready", "failed"), and its owner's name.
    void describe(int n, char *status, int room, const char *&name);
    /// How far the sending is (0..100), over all of them.
    int percent();
    /// Stopped, whatever it was doing, and its memory let go.
    void end();
} // namespace hlrt::dlsend
