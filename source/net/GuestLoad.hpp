#pragma once

// What a console that was sent the game (HLDS_CHILD) is sent, taken as it
// comes (HlGuest.hpp's streams):
//
//  - the common stream, at the start: its files put on the "air:" drive whole
//    for the game to read in its own order as it starts up (the letters, the
//    display's pictures, the menu's sounds), and let go of once it has
//    (dropCommon);
//  - a group's base: each model's block and sound's clip kept as it came,
//    by its name, from one map to the next; the map's loaders borrow them
//    (borrow) rather than read them;
//  - a map's own stream: given to the "air:" drive (beginMap), whose files
//    the map's loaders read as they come, each straight into its own block.
//
// Where the bytes come from is a GuestSupply: the air (later), or, for a test
// with no host, what the build was given (MemorySupply, in pieces the size
// of what the air brings).

#include <cstdint>
#include <cstdio>
#include <string>

#include "HlGuest.hpp"

namespace hlrt::guest
{
    /// A stream already in memory, given out a piece at a time.
    struct MemorySupply : hldata::GuestSupply
    {
        const std::uint8_t *data = nullptr;
        std::uint32_t size = 0;
        std::uint32_t at = 0;
        std::uint32_t piece = 600;
        bool more(const std::uint8_t *&out, std::size_t &length) override;
    };

    bool receiveCommon(hldata::GuestSupply &supply);
    void dropCommon();
    bool receiveBase(hldata::GuestSupply &supply);
    /// A model's block or a sound's clip (`name` as the stream has it:
    /// "mdl/v_357.hmd", "snd/weapons_357_shot1.11k.hsn") the base holds.
    bool borrow(const char *name, const std::uint8_t *&data, std::uint32_t &size);

    /// A stream on a card, read a packet's worth at a time: for a test that
    /// gives the child its data as the air would, with none of it in memory
    /// (HLDS_CHILD_FROMCARD).
    struct FileSupply : hldata::GuestSupply
    {
        std::FILE *file = nullptr;
        std::uint8_t piece[600];
        bool more(const std::uint8_t *&out, std::size_t &length) override;
    };

    /// A map's stream, for a test: where it is (MemorySupply), or the file
    /// on the card it is in (FileSupply).
    void offerMap(const std::string &map, const std::uint8_t *data, std::uint32_t size);
    void offerMapFile(const std::string &map, const std::string &path);
    bool offered(const std::string &map);
    /// The maps offered, in order.
    int offeredCount();
    std::string offeredName(int index);
    /// Its stream to the "air:" drive, as the map is loaded; false if there
    /// is none, or it is not a stream of this version.
    bool beginMap(const std::string &map);
    /// The map is loaded: what is left of its stream passed over, and the
    /// drive's stream let go.
    void endMap();

    /// What the guest holds of what it was sent, by kind (the readout's,
    /// and the converter's estimate's check): the base's bytes, and what the
    /// last map's stream gave its loaders.
    struct Held
    {
        std::uint32_t baseBytes = 0;
        std::uint32_t mapBytes[static_cast<int>(hldata::GuestKind::Count)] = {};
    };
    const Held &held();
    /// `bytes` of a map's `kind` taken by its loader (for held()).
    void count(hldata::GuestKind kind, std::uint32_t bytes);
} // namespace hlrt::guest
