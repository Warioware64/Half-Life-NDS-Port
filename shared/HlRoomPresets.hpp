#pragma once

// The game's 29 rooms (env_sound's "roomtype", 0..28; the FGD names them):
// what each does to the sound, as the engine's room DSP has it -- its preset
// table's values (room_lp, room_mod, room_size, room_refl, room_rvblp,
// room_delay, room_feedback, room_dlylp, room_left), in whole milliseconds and
// thousandths. The values only: the code that uses them is ours
// (HlRoomDsp.hpp), written from what each stage is described as doing.
//
//   0 off   1 generic   2-4 metal S/M/L   5-7 tunnel   8-10 chamber
//   11-13 bright   14-16 water   17-19 concrete   20-22 outside (big)
//   23-25 cavern   26-28 weirdo
//
// Underwater, the room is 14 whatever the env_sound says (the engine's
// waterroom_type).

#include <cstdint>

namespace hldata
{
    inline constexpr int kRoomCount = 29;
    inline constexpr int kRoomUnderwater = 14;

    struct RoomPreset
    {
        std::uint8_t lowpass;    ///< the sound through a lowpass (water)
        std::uint8_t modulate;   ///< its loudness wandering (weirdo 1)
        std::uint8_t reverbMs;   ///< the reverb's size (0: none), at most 100
        std::uint16_t reverbFb;  ///< ... how much of it goes round again, in 1000ths
        std::uint8_t reverbLow;  ///< ... dulled each time round
        std::uint16_t delayMs;   ///< an echo's distance (0: none), at most 400
        std::uint16_t delayFb;   ///< ... how much of it comes back again, in 1000ths
        std::uint8_t delayLow;   ///< ... dulled each time round
        std::uint8_t leftMs;     ///< the left side late by this (a wider sound), at most 100
    };

    inline constexpr RoomPreset kRoomPresets[kRoomCount] = {
        // lp mod rvb  fb  rlp dly   fb  dlp left
        {0, 0, 0, 0, 1, 0, 0, 1, 0},          // 0 normal (off)
        {0, 0, 0, 0, 1, 65, 100, 0, 10},      // 1 generic
        {0, 0, 0, 0, 1, 20, 750, 0, 10},      // 2 metal small
        {0, 0, 0, 0, 1, 30, 780, 0, 20},      // 3 metal medium
        {0, 0, 0, 0, 1, 60, 770, 0, 30},      // 4 metal large
        {0, 0, 50, 850, 1, 8, 960, 1, 10},    // 5 tunnel small
        {0, 0, 50, 880, 1, 10, 980, 1, 20},   // 6 tunnel medium
        {0, 0, 50, 920, 1, 15, 995, 1, 40},   // 7 tunnel large
        {0, 0, 50, 840, 1, 0, 0, 1, 12},      // 8 chamber small
        {0, 0, 50, 900, 1, 0, 0, 1, 8},       // 9 chamber medium
        {0, 0, 50, 950, 1, 0, 0, 1, 4},       // 10 chamber large
        {0, 0, 50, 700, 0, 0, 0, 1, 12},      // 11 bright small
        {0, 0, 55, 780, 0, 0, 0, 1, 8},       // 12 bright medium
        {0, 0, 50, 860, 0, 0, 0, 1, 2},       // 13 bright large
        {1, 0, 0, 0, 1, 0, 0, 1, 10},         // 14 water 1
        {1, 0, 0, 0, 1, 60, 850, 1, 20},      // 15 water 2
        {1, 0, 0, 0, 1, 200, 600, 1, 50},     // 16 water 3
        {0, 0, 50, 800, 1, 0, 480, 1, 16},    // 17 concrete small
        {0, 0, 60, 900, 1, 0, 520, 1, 10},    // 18 concrete medium
        {0, 0, 70, 940, 1, 300, 600, 1, 8},   // 19 concrete large
        {0, 0, 0, 0, 1, 300, 420, 1, 0},      // 20 big 1 (outside)
        {0, 0, 0, 0, 1, 350, 480, 1, 0},      // 21 big 2
        {0, 0, 0, 0, 1, 380, 600, 1, 0},      // 22 big 3
        {0, 0, 50, 900, 1, 200, 280, 0, 0},   // 23 cavern small
        {0, 0, 70, 900, 1, 300, 400, 0, 0},   // 24 cavern medium
        {0, 0, 90, 900, 1, 350, 500, 0, 0},   // 25 cavern large
        {0, 1, 10, 900, 0, 0, 0, 1, 50},      // 26 weirdo 1
        {0, 0, 0, 0, 1, 9, 999, 1, 40},       // 27 weirdo 2
        {0, 0, 1, 999, 0, 200, 800, 1, 50},   // 28 weirdo 3
    };
} // namespace hldata
