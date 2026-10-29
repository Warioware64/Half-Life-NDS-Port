#pragma once

// The radio: the DS's local wireless, as one console the host and up to
// three its clients (dswifi's local multiplayer mode, of the fork that also
// does Download Play). After Skyjo-DS's NetLink, which found out the hard
// way most of what is written in the .cpp.
//
// Only frames go through here: what is in them is shared/HlNet.hpp's. Every
// frame the host sends one that all the clients hear (send), and each
// client's answer goes back by itself; what has come is read with take.
// Frames arrive in an interrupt and are copied into a ring there; nothing
// else happens outside the caller's own time.
//
// All of dswifi is behind this header.

#include <cstddef>
#include <cstdint>

namespace hlrt::netlink
{
    /// The channel a host is on: fixed, so that a console that knows which
    /// host it wants can go straight to it.
    inline constexpr int kChannel = 6;
    /// What a beacon of this game says it is.
    inline constexpr std::uint32_t kGameId = 0x484C4453; // "HLDS"

    /// Leave whatever was going on and be silent. Safe at any time.
    void shutdown();
    /// ... and give the radio and the library back altogether (on leaving
    /// multiplayer for the title).
    void powerDown();
    /// Why the radio could not be started, as the library numbers it.
    int failStage();

    // ------------------------------------------------------------- the host
    /// Be a host, seen by consoles that look, under `name`, for `most`
    /// players at most (itself among them). False if the radio could not be
    /// started.
    bool host(const char *name, int most);
    /// Which clients are there: bit n for client n (1..7).
    unsigned clients();
    /// What the beacon says of the match (HlNet.hpp's NetRoomInfo, as
    /// netRoomWrite has it): seen by consoles looking from the next beacon.
    void hostRoom(const std::uint8_t *bytes, int size);
    /// ... and, on a console looking, what host `index` says of its match
    /// (`size` 0 for a host that says nothing), and how strong its signal is.
    bool hostRoomAt(int index, std::uint8_t *bytes, int &size, int &rssi);
    /// On a client: how strong the host's signal is (0..255).
    int strength();
    /// Let more consoles in, or not (a match under way lets nobody in).
    void admit(bool allow);

    // ----------------------------------------------------------- a client
    /// Look for hosts.
    bool scan();
    /// How many things the scan has found, and whether number `index` is a
    /// host of this game that lets people in: its name, and how many are in
    /// it of how many.
    int found();
    bool hostAt(int index, char *name, int room, int *players, int *most);
    /// Join the host that is number `index` of what was found.
    bool join(int index);
    /// Join the host with this address, without having looked for it (a
    /// console started by Download Play knows who started it). `again`: on
    /// a radio already set to do so, ask once more.
    bool joinDirect(const std::uint8_t address[6], bool again);
    bool joined();     ///< it is this host's client now
    bool refused();    ///< ... or the host would not have it

    // --------------------------------------------------------------- frames
    /// Send a frame: the host's to all its clients, a client's to the host.
    void send(const void *bytes, int size);
    /// The oldest frame not yet read: its bytes into `into` (which holds a
    /// frame of any length), and who it is from (a client's number; 0 for
    /// the host's). Returns its length; 0 if there is none.
    int take(std::uint8_t *into, int &from);
    /// How many frames were dropped because nobody read them in time.
    unsigned dropped();

    /// This console's own address, and the name its owner gave it.
    void address(std::uint8_t out[6]);
    void ownerName(char *out, int room);

    // ------------------------------------------------------- Download Play
    /// Offer `rom` (a whole program, which must stay where it is until
    /// dlStop) to consoles looking at their Download Play menu. `param` is
    /// 32 bytes handed to the program when it starts.
    bool dlStart(const void *rom, std::size_t size, const char *description, const void *param);
    void dlUpdate();      ///< once a frame while offering
    void dlStop();
    bool dlActive();
    unsigned dlGuests();  ///< bit n: guest n is there
    /// How far guest `n` has got with the program: 0..100; -1 if it is not
    /// there.
    int dlProgress(int n);
    /// Whether every guest there has the whole program and is waiting.
    bool dlAllReady();
    void dlLock(bool locked); ///< no more guests
    bool dlBootAll();         ///< start the program on all of them
} // namespace hlrt::netlink
