#pragma once

// What the game tells the achievements about, as it happens: a short list
// the logic adds to during a frame and the engine empties after it
// (HlAchievements.hpp reads it). Kept apart from the achievements
// themselves so that the logic needs no more than this.

#include <cstdint>

namespace hldata
{
    enum class AchEventType : std::uint8_t
    {
        Fired,  ///< a watched name was used, and something live answered to it
        Killed, ///< a monster died: `kind`, `hash` its name, kAchByPlayer if the player did it
        Broke,  ///< something breakable broke
        Soda,   ///< a soda can gave the player health
        Picked, ///< the player took an item: `kind` its ItemKind
    };

    inline constexpr std::uint8_t kAchByPlayer = 1;

    struct AchEvent
    {
        AchEventType type = AchEventType::Fired;
        std::uint8_t kind = 0;
        std::uint8_t flags = 0;
        std::uint32_t hash = 0;
    };

    struct AchEvents
    {
        static constexpr int kMost = 16;
        static constexpr int kWatchMost = 8;
        AchEvent list[kMost] = {};
        int count = 0;
        /// The names the current map's achievements care about (the hash
        /// of each): a name not here is not reported when it is used.
        std::uint32_t watched[kWatchMost] = {};
        int watchedCount = 0;
        /// While above 0 nothing is reported (the COMMAND line's killall).
        int muted = 0;

        bool watches(std::uint32_t hash) const
        {
            for (int i = 0; i < watchedCount; ++i)
            {
                if (watched[i] == hash)
                    return true;
            }
            return false;
        }
        /// (A frame that has more to say than there is room for loses the
        /// rest: nothing that matters comes sixteen at a time.)
        void push(AchEventType type, std::uint32_t hash, std::uint8_t kind = 0, std::uint8_t flags = 0)
        {
            if (muted > 0 || count >= kMost)
                return;
            list[count++] = AchEvent{type, kind, flags, hash};
        }
    };
} // namespace hldata
