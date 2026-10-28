#pragma once

// The pipeline (Pipeline.arm.cpp): the console's own mix, captured, and
// played again with the music added -- the room's effect is added here too.

#include <cstdint>

#include "HlAudioLink.hpp"

namespace hlaudio
{
    /// The capture units and channels 1 and 3 set going, and the block
    /// timer with them (so a block is a known stretch of the rings).
    void pipelineStart(hldata::AudioLink &link);
    /// Back to the plain mixer.
    void pipelineStop();
    bool pipelineOn();
    /// `blocks` more blocks, as the timer has counted them.
    void pipelineRun(hldata::AudioLink &link, std::uint32_t blocks);

    /// The room's effect (kAudioEffects*) and how the sound is laid out
    /// (kAudioOutput*).
    void pipelineMode(std::uint8_t effects, std::uint8_t output);
    /// The room the player is in (0..28).
    void pipelineRoom(int room);

    void musicOpen(hldata::AudioLink &link, const hldata::AudioCommand &command);
    void musicStop(hldata::AudioLink &link);
    void musicVolume(int volume);
} // namespace hlaudio
