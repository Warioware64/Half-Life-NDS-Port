#pragma once

// Who a player may be seen as (NetProfile::model, Settings::playerModel). The
// first is the game's player in the suit, which every multiplayer map has;
// the rest are in the common archive (chr/<name>.hmd, chr/<name>.htx), for a
// console with the game. (Not the game's ivan, bbbbarney and skeleton: their
// skins are in several pieces, and a player's colours would not go over them.)

namespace hldata
{
    inline constexpr const char *kCharacterFiles[] = {"gordon", "barney", "gina", "gman",      "helmet", "hgrunt",
                                                      "recon",  "robo",   "scientist", "tmcm", "zombie"};
    inline constexpr const char *kCharacterNames[] = {"HEV suit", "Barney", "Gina", "G-Man",     "Helmet", "Soldier",
                                                      "Recon",    "Robo",   "Scientist", "Tmcm", "Zombie"};
    inline constexpr int kCharacterCount = sizeof(kCharacterFiles) / sizeof(kCharacterFiles[0]);
    /// How many besides the suit a match has in memory at once.
    inline constexpr int kCharacterSlots = 4;
} // namespace hldata
