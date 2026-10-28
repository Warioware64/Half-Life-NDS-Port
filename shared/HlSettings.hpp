#pragma once

// The player's settings: <card>/_nds/hl_nds/data/settings.dat
//
// Written by the game, never by the converter. A yas archive behind the same
// kind of fixed header as the manifest (HlManifest.hpp), for the same reason:
// yas reports a damaged archive by throwing, the ARM9 has no exceptions, so
// everything that can be checked is checked before yas runs. A file that does
// not check out is not an error -- it is a first boot, and the defaults apply.

#include <cstdint>
#include <cstring>
#include <vector>

#include "HlYas.hpp"

#include "HlLevel.hpp"

#include "HlControls.hpp"
#include "HlFog.hpp"

namespace hldata
{
    struct SettingsData
    {
        /// Deprecated, can be reused for something else later
        std::uint8_t renderMode = 0;
        /// Before version 13: the DSi's DSP on (now roomEffects' Enhanced).
        /// Still written, for a build before to read.
        std::uint8_t dspAudio = 0;
        /// How loud each layer of the sound is, 0 (off) to 10: the music,
        /// the effects (the player's and the world's), the voices, and the
        /// ambience of a place.
        std::uint8_t volume[4] = {8, 10, 10, 8};
        /// The debug readout along the top of the touch screen.
        std::uint8_t debugReadout = 0;
        /// The stylus is in the left hand: the touch screen's buttons are
        /// on the other side (HlTouchLayout.hpp).
        std::uint8_t leftHanded = 0;
        /// The view bobs with a walk and leans into a sidestep, as the
        /// game's does. Off unless asked for: on a screen this small, held
        /// this close, it is more than some can look at.
        std::uint8_t viewBob = 0;
        /// The fog, and how much detail is given up for speed (HlFog.hpp).
        GraphicsSettings graphics;
        /// The layout of the buttons (kControls*), and which of the pad and
        /// the stylus turns and which moves (HlControls.hpp).
        std::uint8_t controlScheme = 0;
        std::uint8_t padTurns = 0;
        std::uint8_t padLooks = 0;
        /// How far the stylus turns the view, across and up and down (10:
        /// a pixel is 1/512 of a turn, 1 to 20), and whether up is down.
        std::uint8_t aimSpeed = kAimSpeedDefault;
        std::uint8_t pitchSpeed = kAimSpeedDefault;
        std::uint8_t invertPitch = 0;
        /// The crouch button toggles (else it is held).
        std::uint8_t duckToggle = 0;
        /// How strict the stylus is about light touches (0..10), and what a
        /// firm press on the aim pad does (kFirm*): HlControls.hpp. Only
        /// where the console can say how hard it is pressed.
        std::uint8_t firmness = 4;
        std::uint8_t firmPress = kFirmOff;
        /// The CUSTOM layout: each action's buttons (kButton*).
        std::uint16_t keys[kActionCount] = {};
        /// The guns bend their aim towards a monster near it, on EASY and
        /// MEDIUM (the game's sv_aim: HlAutoaim.hpp). On unless asked off.
        std::uint8_t autoaim = 1;
        /// An achievement earned is shown on the top screen as it is
        /// (Achievements.cpp). On unless asked off: off, it is still earned.
        std::uint8_t achievementPopups = 1;
        /// The rooms heard (kRoom*: off, the game's own, or -- on a DSi --
        /// the DSP's richer one); how the sound is laid out (kOutput*: the
        /// console's speakers, headphones, one channel); and, on a DSi, how
        /// fine it is (kQuality*).
        std::uint8_t roomEffects = 1;
        std::uint8_t outputMode = 0;
        std::uint8_t soundQuality = 0;

        /// What a file of version 8 held: all but the controls.
        template <class Ar>
        void serializeEight(Ar &ar)
        {
            ar &renderMode &dspAudio &volume[0] &volume[1] &volume[2] &volume[3] &debugReadout &leftHanded &viewBob;
            ar &graphics.fog &graphics.fogStrength &graphics.fogDistance &graphics.fogColour &graphics.modelDetail &
                graphics.worldDetail &graphics.modelLimit &graphics.drawDistance;
        }
        /// ... and of version 9: the layouts, and no more.
        template <class Ar>
        void serializeNine(Ar &ar)
        {
            serializeEight(ar);
            ar &controlScheme &padTurns &padLooks;
        }
        /// ... and of version 10: the controls, and no autoaim.
        template <class Ar>
        void serializeTen(Ar &ar)
        {
            serializeNine(ar);
            ar &aimSpeed &pitchSpeed &invertPitch &duckToggle &firmness &firmPress;
            for (std::uint16_t &key : keys)
                ar &key;
        }
        /// ... and of version 11: no achievements' pop-ups to turn off.
        template <class Ar>
        void serializeEleven(Ar &ar)
        {
            serializeTen(ar);
            ar &autoaim;
        }
        /// ... and of version 12: no sound settings but the DSP and the
        /// volumes.
        template <class Ar>
        void serializeTwelve(Ar &ar)
        {
            serializeEleven(ar);
            ar &achievementPopups;
        }
        template <class Ar>
        void serialize(Ar &ar)
        {
            serializeTwelve(ar);
            ar &roomEffects &outputMode &soundQuality;
        }
    };

    inline constexpr std::uint8_t kRoomOff = 0, kRoomClassic = 1, kRoomEnhanced = 2, kRoomChoices = 3;
    inline constexpr std::uint8_t kOutputSpeakers = 0, kOutputHeadphones = 1, kOutputMono = 2, kOutputChoices = 3;
    inline constexpr std::uint8_t kQualityStandard = 0, kQualityHigh = 1, kQualityChoices = 2;

    inline constexpr char kSettingsMagic[4] = {'H', 'L', 'S', 'T'};
    inline constexpr std::uint16_t kSettingsVersion = 13;
    /// The versions before, which are still read: 12 had no room effects,
    /// output or quality (its DSP switch is the room effects' Enhanced);
    /// 11 had no achievements' pop-ups to turn off; 10 had no autoaim; 9 had
    /// the layouts but no aim speed, custom buttons or stylus pressure; 8 no
    /// controls to choose.
    inline constexpr std::uint16_t kSettingsVersionTwelve = 12;
    inline constexpr std::uint16_t kSettingsVersionEleven = 11;
    inline constexpr std::uint16_t kSettingsVersionTen = 10;
    inline constexpr std::uint16_t kSettingsVersionNine = 9;
    inline constexpr std::uint16_t kSettingsVersionBefore = 8;
    /// The most a settings file can be (what the console reads it into).
    inline constexpr std::size_t kSettingsMostBytes = 128;
    inline constexpr int kVolumeMusic = 0;
    inline constexpr int kVolumeEffects = 1;
    inline constexpr int kVolumeVoices = 2;
    inline constexpr int kVolumeAmbience = 3;
    inline constexpr int kVolumeMost = 10;

    struct SettingsHeader
    {
        char magic[4];
        std::uint16_t version;
        std::uint16_t payloadBytes;
        std::uint32_t crc32;
    };
    static_assert(sizeof(SettingsHeader) == 12);

    inline std::vector<std::uint8_t> writeSettings(const SettingsData &data)
    {
        constexpr std::size_t flags = yas::mem | yas::binary | yas::no_header;
        yas::mem_ostream os;
        yas::binary_oarchive<yas::mem_ostream, flags> oa(os);
        oa &data;
        const auto buf = os.get_intrusive_buffer();

        SettingsHeader header{};
        std::memcpy(header.magic, kSettingsMagic, sizeof(header.magic));
        header.version = kSettingsVersion;
        header.payloadBytes = static_cast<std::uint16_t>(buf.size);
        header.crc32 = crc32(reinterpret_cast<const std::uint8_t *>(buf.data),
                             static_cast<std::uint32_t>(buf.size));
        std::vector<std::uint8_t> out(sizeof(header) + buf.size);
        std::memcpy(out.data(), &header, sizeof(header));
        std::memcpy(out.data() + sizeof(header), buf.data, buf.size);
        return out;
    }

    /// False -- leaving `out` at its defaults -- for anything that is not a
    /// settings file of this version, and for values out of range.
    inline bool readSettings(const std::uint8_t *bytes, std::size_t size, SettingsData &out)
    {
        constexpr std::size_t flags = yas::mem | yas::binary | yas::no_header;
        SettingsHeader header;
        if (size < sizeof(header))
            return false;
        std::memcpy(&header, bytes, sizeof(header));
        if (std::memcmp(header.magic, kSettingsMagic, sizeof(header.magic)) != 0 ||
            (header.version != kSettingsVersion && header.version != kSettingsVersionTwelve &&
             header.version != kSettingsVersionEleven &&
             header.version != kSettingsVersionTen &&
             header.version != kSettingsVersionNine && header.version != kSettingsVersionBefore) ||
            header.payloadBytes != size - sizeof(header) ||
            crc32(bytes + sizeof(header), header.payloadBytes) != header.crc32)
            return false;

        SettingsData read;
        yas::mem_istream is(bytes + sizeof(header), header.payloadBytes);
        yas::binary_iarchive<yas::mem_istream, flags> ia(is);
        if (header.version == kSettingsVersionBefore)
            read.serializeEight(ia);
        else if (header.version == kSettingsVersionNine)
            read.serializeNine(ia);
        else if (header.version == kSettingsVersionTen)
            read.serializeTen(ia);
        else if (header.version == kSettingsVersionEleven)
            read.serializeEleven(ia);
        else if (header.version == kSettingsVersionTwelve)
            read.serializeTwelve(ia);
        else
            ia &read;
        // (Before 13: the DSP switched on is the DSP's rooms; else the
        // game's own.)
        if (header.version != kSettingsVersion)
            read.roomEffects = read.dspAudio != 0 ? kRoomEnhanced : kRoomClassic;
        if (read.roomEffects >= kRoomChoices || read.outputMode >= kOutputChoices ||
            read.soundQuality >= kQualityChoices)
            return false;
        if (read.controlScheme >= kControlsSchemes || read.padTurns > 1 || read.padLooks > 1)
            return false;
        if (read.aimSpeed < 1 || read.aimSpeed > kAimSpeedMost || read.pitchSpeed < 1 ||
            read.pitchSpeed > kAimSpeedMost || read.invertPitch > 1 || read.duckToggle > 1 ||
            read.firmness > kFirmnessMost || read.firmPress >= kFirmActions)
            return false;
        for (std::uint16_t key : read.keys)
        {
            if (key & ~kAssignable)
                return false;
        }
        if (read.dspAudio > 1 || read.autoaim > 1 || read.achievementPopups > 1 ||
            read.debugReadout > 1 || read.leftHanded > 1 || read.viewBob > 1 || !read.graphics.valid())
            return false;
        for (std::uint8_t level : read.volume)
        {
            if (level > kVolumeMost)
                return false;
        }
        out = read;
        return true;
    }
} // namespace hldata
