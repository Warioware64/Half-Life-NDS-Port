#pragma once

// The game's sounds, made into what the DS plays.
//
// A sound is read from its WAV (8 or 16 bits, one channel or two mixed to
// one, with the loop point Half-Life marks by a "cue" chunk), brought to the
// rate it will be played at, and packed as IMA-ADPCM in the DS hardware's own
// layout: four bits a sample, which the console's sound chip unpacks by
// itself. See shared/HlSound.hpp for the file.
//
// Spoken lines are lists of such sounds: sentences.txt names each line and
// the words it is made of.

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "HlSound.hpp"

namespace hlds
{
    struct WavData
    {
        int rate = 0;
        std::vector<std::int16_t> samples; ///< one channel
        int loopStart = -1;                ///< the sample it loops from; -1: it is not marked to loop
    };

    bool parseWav(const std::vector<std::uint8_t> &bytes, WavData &out, std::string &error);
    bool readWav(const std::filesystem::path &path, WavData &out, std::string &error);

    /// `samples` at `from` a second, as they would be at `to` a second.
    std::vector<std::int16_t> resample(const std::vector<std::int16_t> &samples, int from, int to);

    struct CookedSound
    {
        int rate = hldata::kSoundRate;
        std::vector<std::uint8_t> data; ///< the ADPCM: header, then nibbles
        std::uint32_t loopWord = hldata::kSoundNoLoop;
        std::uint32_t samples = 0;
    };

    /// `wav` at `rate`, packed. `loops`: it goes round, from the WAV's own
    /// loop point if it has one and from its start if not.
    CookedSound encodeSound(const WavData &wav, int rate, bool loops);
    /// What the DS will make of it: for the tests.
    std::vector<std::int16_t> decodeSound(const CookedSound &sound);

    std::vector<std::uint8_t> packSound(const CookedSound &sound, std::uint32_t sourceHash);
    bool unpackSound(const std::vector<std::uint8_t> &bytes, CookedSound &out, hldata::SoundHeader &header);

    /// The name a sound goes by on the card (no folder, no extension): the
    /// game's path with its slashes made underscores, the rate, and whether
    /// it loops -- "ambience/labdrone2.wav" at 11025 looping is
    /// "ambience_labdrone2.11l".
    std::string soundCardName(const std::string &gamePath, int rate, bool loops);

    /// One spoken line: the sounds it is made of, as the game's paths
    /// ("scientist/alright.wav"), lower-case.
    /// How one word of a line is said: the game's own marks on it. `pitch`
    /// and `volume` in percent; `end`: where it is cut off, in percent of
    /// its length (100: said to its end).
    struct WordSaid
    {
        int pitch = 100;
        int volume = 100;
        int end = 100;
    };
    struct Sentence
    {
        /// The sounds, in order -- the pauses of its commas and full stops
        /// among them, which are sounds too ("_comma", "_period").
        std::vector<std::string> words;
        std::vector<WordSaid> said; ///< parallel to `words`
        int volume = 127;
        int pitch = 0; ///< (as it was: each word has its own now)
    };
    /// sentences.txt: every line by its name, upper-case, without the "!".
    struct SentenceBook
    {
        std::map<std::string, Sentence> lines;
        /// The line called `name` ("!C1A0_SCI_GM" or a group's name, any
        /// case); null if there is none.
        const Sentence *find(const std::string &name) const;
    };
    SentenceBook parseSentences(const std::string &text);

    /// An MP3 as one channel of samples (`out`), and -- if asked for -- as
    /// its two (a one-channel MP3 gives the same in both). False if it is
    /// not one.
    bool decodeMp3(const std::vector<std::uint8_t> &bytes, WavData &out, std::string &error,
                   std::vector<std::int16_t> *left = nullptr,
                   std::vector<std::int16_t> *right = nullptr);

    /// Two channels at `rate`, as a .hsd file (HlSound.hpp): what the ARM7
    /// plays the music from (HlStereoMusic.hpp).
    std::vector<std::uint8_t> encodeMusicStereo(const std::vector<std::int16_t> &left,
                                                const std::vector<std::int16_t> &right, int rate);
    /// What the ARM7 will make of a .hsd file, both channels: for the tests.
    /// Starts at block `fromBlock`, as a track taken up part way does.
    bool decodeMusicStereo(const std::vector<std::uint8_t> &file, std::size_t fromBlock,
                           std::vector<std::int16_t> &left, std::vector<std::int16_t> &right);
} // namespace hlds
