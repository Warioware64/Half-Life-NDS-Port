#pragma once

// What goes with each weapon, loaded together (ModelCache): what is held, and
// what it fires, throws or spits out -- a bolt, a rocket, a casing. Where the
// map has one of these for another reason (a soldier's casings, a tank's
// rocket, a mine on a wall) it is the map's own and always there; only the
// rest is taken up with the weapon (kModelRefLazy).
//
// What lies about to be picked up (w_shotgun and so on) is not here: it has
// to be seen before it is anyone's.

#include <cstdint>
#include <cstring>

#include "HlModel.hpp"
#include "HlPlayerMove.hpp"

namespace hldata
{
    /// What is not held, of a bundle, is small on the screen: a smaller skin.
    inline constexpr int kLazyPieceSkinTexels = 64 * 64;
    /// How many of a map's lazy models are in memory at once: the most any
    /// one weapon has. (Each has a palette in video memory: the converter
    /// keeps room for this many.)
    inline constexpr int kLazyResident = 3;

    struct WeaponBundle
    {
        const char *models[3];
    };

    /// By kWeapon*.
    inline constexpr WeaponBundle kWeaponBundles[kWeaponCount] = {
        {{nullptr, nullptr, nullptr}},
        {{"v_crowbar", nullptr, nullptr}},
        {{"v_9mmhandgun", "shell", nullptr}},
        {{"v_shotgun", "shotgunshell", nullptr}},
        {{"v_grenade", "w_grenade", nullptr}},
        {{"v_9mmar", "shell", "grenade"}},
        {{"v_357", nullptr, nullptr}},
        {{"v_crossbow", "crossbow_bolt", nullptr}},
        {{"v_satchel", "v_satchel_radio", "w_satchel"}},
        {{"v_tripmine", nullptr, nullptr}},
        {{"v_squeak", "w_squeak", nullptr}},
        {{"v_rpg", "rpgrocket", nullptr}},
        {{"v_gauss", nullptr, nullptr}},
        {{"v_egon", nullptr, nullptr}},
        {{"v_hgun", "hornet", nullptr}},
    };

    /// The weapons (a bit each, by kWeapon*) whose bundles have the model.
    inline std::uint16_t bundlesOf(const char *model)
    {
        std::uint16_t mask = 0;
        for (int weapon = 0; weapon < kWeaponCount; ++weapon)
        {
            for (const char *name : kWeaponBundles[weapon].models)
            {
                if (name != nullptr && std::strcmp(name, model) == 0)
                    mask = static_cast<std::uint16_t>(mask | 1u << weapon);
            }
        }
        return mask;
    }
} // namespace hldata
