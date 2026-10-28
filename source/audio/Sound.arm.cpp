#include "audio/Sound.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

#include <nds.h>

#include <NEAMain.h>
#include <maxmod9.h>
#include <mm_mas.h>

#include "Global.hpp"
#include "Settings.hpp"
#include "audio/Arm7Audio.hpp"
#include "audio/DspAudio.hpp"
#include "game/Player.hpp"
#include "data/LevelFile.hpp"
#include "game/Game.hpp"
#include "render/Camera.hpp"

namespace hlrt
{
    Sound sound;

    namespace
    {
        using namespace hldata;

        /// The music's ring, which the ARM7 plays round and round and
        /// musicTick keeps filled ahead of it (half a second).
        alignas(32) std::int8_t musicRing[8192];
    } // namespace

    void Sound::init()
    {
        soundEnable();
        // A DSi's extras: the output at 47.6 kHz instead of 32.7 (asked of
        // NEA, after the sound is switched on -- which puts it back to 32.7
        // -- and before anything plays), and the DSP. Neither exists on a
        // DS, and then nothing here does anything.
        // The DSP is used only if the player has asked for it (the pause
        // menu): until then a DSi sounds as a DS does, at the higher rate.
        const bool dsi = NEA_SoundDSiOutputAvailable();
        if (dsi)
        {
            // (All of the speakers to the ordinary channels, until the DSP
            // has something for them: DspAudio.)
            NEA_SoundSetDSiMixRatio(8);
            NEA_SoundEnableDSiOutput(NEA_DSI_SOUND_FREQ_47KHZ);
        }
        // The game's own sound on the ARM7 (arm7/source/Audio.cpp), which
        // plays the clips where they are in memory: nothing is "loaded".
        // (Channel 1 is not its: its clock is what a DSi's sound capture
        // keeps time by -- DspAudio.)
        static_assert(sizeof(musicRing) == kStreamSamples);
        up_ = arm7Audio.start();
        applyVolumes();
        // The menu's own sounds, for as long as the game runs.
        for (int n = 0; n < kUiSoundCount; ++n)
        {
            if (global.hasFat && readClip(kUiSoundFiles[n], ui_[n]))
                describe(ui_[n]);
        }
        if (dsi)
            extended_ = settings.dspAudio && dspAudio.start();
        lastVblank_ = Global::vblanks();
    }

    const void *Sound::sample(std::uint32_t id) const
    {
        if (id >= static_cast<std::uint32_t>(kSpeechSample) && id < static_cast<std::uint32_t>(kSamples))
        {
            const Clip &clip = speech_[(id - kSpeechSample) / 2].clip[(id - kSpeechSample) & 1];
            return clip.data != nullptr ? &clip.header : nullptr;
        }
        if (id >= static_cast<std::uint32_t>(kUiSample) && id < static_cast<std::uint32_t>(kUiSample + kUiSoundCount))
            return ui_[id - kUiSample].data != nullptr ? &ui_[id - kUiSample].header : nullptr;
        if (id < static_cast<std::uint32_t>(count_) && clips_ != nullptr && clips_[id].data != nullptr)
            return &clips_[id].header;
        return nullptr;
    }

    void Sound::applyVolumes()
    {
        const auto effects = settings.volume[kVolumeEffects];
        mixer_.volume[static_cast<int>(SoundLayer::Player)] = effects;
        mixer_.volume[static_cast<int>(SoundLayer::World)] = effects;
        mixer_.volume[static_cast<int>(SoundLayer::Ui)] = effects;
        mixer_.volume[static_cast<int>(SoundLayer::Voice)] = settings.volume[kVolumeVoices];
        mixer_.volume[static_cast<int>(SoundLayer::Ambience)] = settings.volume[kVolumeAmbience];
        // (The music's is taken as it is fed: fillMusic. What is playing in
        // the other layers is told when it is next placed.)
        loopsDue_ = 0;
    }

    bool Sound::readClip(const char *name, Clip &clip)
    {
        clip = Clip();
        // The map's own, out of its archive; the menu's, out of the card's.
        const std::string inArchive = std::string("snd/") + name + ".hsn";
        std::FILE *file = std::fopen(global.mapPath(inArchive).c_str(), "rb");
        if (file == nullptr)
            file = std::fopen(global.commonPath(inArchive).c_str(), "rb");
        if (file == nullptr)
            return false;
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        // The header is checked against the file's real size before anything
        // in it sizes an allocation or is handed to the hardware.
        SoundHeader header;
        bool ok = size > static_cast<long>(sizeof(header)) && size < 4 * 1024 * 1024 &&
                  std::fread(&header, sizeof(header), 1, file) == 1 &&
                  soundHeaderValid(header, static_cast<std::uint32_t>(size));
        std::uint8_t *data = nullptr;
        if (ok)
        {
            data = static_cast<std::uint8_t *>(std::malloc(header.dataBytes));
            ok = data != nullptr && std::fread(data, 1, header.dataBytes, file) == header.dataBytes;
        }
        std::fclose(file);
        if (!ok)
        {
            std::free(data);
            return false;
        }
        // The ARM7 plays it out of memory itself: it must be there, not in
        // this processor's cache.
        DC_FlushRange(data, header.dataBytes);
        clip.data = data;
        clip.bytes = header.dataBytes;
        clip.loopWord = header.loopWord;
        clip.rate = header.rate;
        clip.ms = static_cast<std::uint32_t>((static_cast<std::uint64_t>(header.samples) * 1000) / header.rate);
        return true;
    }

    /// The header maxmod reads a clip by. The hardware plays the ADPCM as
    /// it is: all of it once, or round and round from where it loops.
    void Sound::describe(Clip &clip)
    {
        const std::uint32_t words = clip.bytes / 4;
        const bool loops = clip.loopWord != kSoundNoLoop && clip.loopWord < words && clip.loopWord < 0xFFFF;
        Header &h = clip.header;
        h = Header{};
        h.size = sizeof(Header) - 8;
        h.type = 2; // (a DS sample)
        h.loopStart = loops ? clip.loopWord : 0;
        h.length = loops ? words - clip.loopWord : words;
        h.format = MM_SFORMAT_ADPCM;
        h.repeat = loops ? MM_SREPEAT_FORWARD : MM_SREPEAT_OFF;
        // (Maxmod's unit: 1024ths of 32768 Hz.)
        h.frequency = static_cast<std::uint16_t>((static_cast<std::uint32_t>(clip.rate) * 1024 + 16384) / 32768);
        h.point = reinterpret_cast<std::uint32_t>(clip.data);
        DC_FlushRange(&h, sizeof(h));
    }

    void Sound::load()
    {
        const LevelView &view = level.view;
        // (The numbers at the top are the menu's and the spoken word's.)
        const int count = static_cast<int>(view.soundCount) < kUiSample ? static_cast<int>(view.soundCount) : kUiSample;
        clips_ = static_cast<Clip *>(std::calloc(count + 1, sizeof(Clip)));
        bool *kept = static_cast<bool *>(std::calloc(view.soundCount + 1, sizeof(bool)));
        if (clips_ == nullptr || kept == nullptr)
        {
            std::free(clips_);
            std::free(kept);
            clips_ = nullptr;
            return;
        }
        count_ = count;
        played_ = static_cast<std::uint32_t *>(std::calloc(view.logicCount / 32 + 1, sizeof(std::uint32_t)));
        soundsKept(view.logic, view.logicCount, view.logicSounds, view.logicSoundCount,
                   view.soundCount, kept, view.modelSounds, view.modelSoundCount, view.sounds);
        lazy_ = static_cast<std::uint8_t *>(std::calloc(count + 1, 1));
        for (int i = 0; lazy_ != nullptr && i < count; ++i)
            lazy_[i] = soundLazy(view.sounds[i]) && view.sounds[i].name[0] != '\0' ? 1 : 0;
        for (int k = 0; k < kLazyClips; ++k)
            lazyClip_[k] = -1;
        // In two goes: everything but the later weapons' sounds, then those.
        // Left with too little to play in (a DS, in a map with every weapon),
        // the map does without the rest rather than without its memory --
        // and the rest is a weapon's noise, not the map's own.
        const int laterFrom = static_cast<int>(kCommonSoundsBefore30);
        const int laterTo = static_cast<int>(commonsOf(view.header->version));
        for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < count; ++i)
        {
            const bool later = i >= laterFrom && i < laterTo;
            if (later != (pass == 1) || !kept[i] || view.sounds[i].name[0] == '\0')
                continue;
            if (global.heapHeadroom() < 300 * 1024)
                break;
            // (A DSi has the memory for the better copy, where there is one.)
            char twin[sizeof(SoundRef)];
            if ((global.isDSi && soundTwinName(view.sounds[i], twin, sizeof(twin)) && readClip(twin, clips_[i])) ||
                readClip(view.sounds[i].name, clips_[i]))
            {
                describe(clips_[i]);
                ++loaded_;
                bytes_ += static_cast<int>(clips_[i].bytes);
            }
        }
        std::free(kept);
        lastVblank_ = Global::vblanks();
        resumeMusic();
    }

    void Sound::holdMusic()
    {
        // Put down where it is, to be taken up again -- by whichever of the
        // two ways of playing it there then is, so where it is is kept in
        // milliseconds.
        if (dspAudio.musicPlaying())
        {
            musicHeld_ = true;
            musicTrack_ = dspAudio.musicTrack();
            musicHeldMs_ = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(dspAudio.musicPair()) * 1000) / kStereoRate);
            dspAudio.stopMusic();
        }
        if (music_ != nullptr)
        {
            // (What has been handed over, less what the stream still has
            // in hand, less any silence after the end.)
            const std::uint32_t played = musicPlayed();
            const int track = musicTrack_;
            const bool over = musicLeft_ == 0;
            const std::uint32_t before = musicSkipped_;
            stopMusic();
            if (!over)
            {
                musicHeld_ = true;
                musicTrack_ = track;
                musicHeldMs_ = static_cast<std::uint32_t>(
                    (static_cast<std::uint64_t>(before + played) * 1000) / kMusicRate);
            }
        }
    }

    void Sound::resumeMusic()
    {
        // (With the game standing still it stays put down: setPaused takes
        // it up again.)
        if (!musicHeld_ || paused_)
            return;
        musicHeld_ = false;
        const int track = musicTrack_;
        musicTrack_ = -1;
        // The DSP's way, taken up at the block that holds the moment...
        if (dspAudio.up() &&
            dspAudio.startMusic(track, static_cast<std::uint32_t>(
                                           (static_cast<std::uint64_t>(musicHeldMs_) * kStereoRate) / 1000)))
            return;
        // ... or the DS's.
        musicSkipped_ = musicHeldMs_ * (kMusicRate / 1000);
        startMusic(track);
    }

    bool Sound::setDsp(bool on)
    {
        if (on == extended_)
            return extended_;
        holdMusic();
        if (on)
        {
            extended_ = dspAudio.start();
        }
        else
        {
            dspAudio.stop();
            extended_ = false;
        }
        resumeMusic();
        return extended_;
    }

    void Sound::unload()
    {
        if (up_)
            arm7Audio.stopAll();
        fading_[0] = fading_[1] = Fading();
        for (int v = 0; v < Mixer::kVoices; ++v)
        {
            mixer_.stop(v);
            voices_[v] = Voice();
        }
        // The music is put down where it is, to be taken up again: a map
        // takes seconds to load, and nothing would feed it.
        holdMusic();
        // (Two blanks, so that the ARM7 has let go of everything before it
        // is freed.)
        swiWaitForVBlank();
        swiWaitForVBlank();
        for (int i = 0; i < count_; ++i)
        {
            std::free(clips_[i].data);
        }
        for (Speech &speech : speech_)
        {
            releaseWord(speech, 0);
            releaseWord(speech, 1);
            speech = Speech();
        }
        std::free(played_);
        played_ = nullptr;
        std::free(lazy_);
        lazy_ = nullptr;
        for (int k = 0; k < kLazyClips; ++k)
            lazyClip_[k] = -1;
        std::free(clips_);
        clips_ = nullptr;
        count_ = 0;
        loaded_ = 0;
        bytes_ = 0;
    }

    void Sound::place(const fx *pos, int volume, int radius, int &outVolume, int &outPan) const
    {
        const fx d[3] = {pos[0] - camera.pos[0], pos[1] - camera.pos[1], pos[2] - camera.pos[2]};
        const fx distance = fxLength(d);
        outVolume = soundGain(volume, fxToInt(distance), radius);
        // To one side by how far round it is: straight ahead and straight
        // behind are the middle.
        outPan = 64;
        if (distance > fxFromInt(8))
        {
            const std::int64_t side = static_cast<std::int64_t>(d[0]) * camera.right[0] +
                                      static_cast<std::int64_t>(d[1]) * camera.right[1] +
                                      static_cast<std::int64_t>(d[2]) * camera.right[2];
            outPan = 64 + static_cast<int>((side * 56) / (static_cast<std::int64_t>(distance) << kFxShift));
            outPan = outPan < 0 ? 0 : (outPan > 127 ? 127 : outPan);
        }
    }

    /// Play sample `sample` (which is `clip`) in `layer`, `volume` loud
    /// (0..127, before the layer's own volume) and `pan` to one side
    /// (0..127). The voice it got, or -1 if it is not to be heard.
    int Sound::start(SoundLayer layer, int sample, const Clip &clip, int volume, int pan, bool loop, int tag,
                     int pitch)
    {
        if (!up_ || (paused_ && layer != SoundLayer::Ui))
            return -1;
        const int gain = mixer_.scaled(layer, volume);
        if (gain < 3)
            return -1;
        const int v = mixer_.take(layer, gain);
        if (v < 0)
            return -1;
        if (mixer_.voices[v].active)
            retire(v);
        // (The unit of speed: 1024 is as recorded.)
        int rate = (1024 * pitch) / 100;
        rate = rate < 64 ? 64 : (rate > 4095 ? 4095 : rate);
        (void)sample;
        auto playIt = [&]()
        {
            return arm7Audio.play(clip.data, clip.bytes / 4, clip.loopWord, clip.rate, gain * 2, pan * 2, rate,
                                  layer == SoundLayer::Ui);
        };
        std::uint16_t handle = playIt();
        if (handle == 0 && (fading_[0].handle != 0 || fading_[1].handle != 0))
        {
            // No channel: what was on its way out is out now.
            killFading();
            handle = playIt();
        }
        if (handle == 0)
        {
            // The hardware has no channel for it, whatever the count says
            // (something of its own is on one): then what matters least of
            // all that is playing gives its channel up, and it is asked
            // again. A sound is never taken as played that was not.
            const int least = mixer_.weakest();
            if (least < 0 || (layer != SoundLayer::Ui && layer != SoundLayer::Voice &&
                              Mixer::rank(mixer_.voices[least].layer) > Mixer::rank(layer)))
                return -1;
            stop(least);
            handle = playIt();
            if (handle == 0)
                return -1;
        }
        voices_[v] = Voice();
        voices_[v].handle = handle;
        voices_[v].shown = static_cast<std::uint8_t>(gain);
        voices_[v].pan = static_cast<std::uint8_t>(pan);
        voices_[v].rate = static_cast<std::uint16_t>(rate);
        voices_[v].hz = clip.rate;
        // (Played faster, it is over sooner.)
        mixer_.start(v, layer, gain, loop ? -1 : static_cast<std::int32_t>(clip.ms * 100 / pitch) + 34, tag);
        return v;
    }

    void Sound::stop(int v)
    {
        if (voices_[v].handle != 0)
            arm7Audio.stop(voices_[v].handle);
        voices_[v] = Voice();
        mixer_.stop(v);
    }

    void Sound::retire(int v)
    {
        const std::uint16_t handle = voices_[v].handle;
        const int level = voices_[v].shown;
        voices_[v] = Voice();
        mixer_.stop(v);
        if (handle == 0)
            return;
        // (Too quiet to click, or the game standing still: simply stopped.)
        if (level < 8 || paused_)
        {
            arm7Audio.stop(handle);
            return;
        }
        // A place on the way out; if both are taken, the older has had its
        // time.
        int slot = fading_[0].handle == 0 ? 0 : (fading_[1].handle == 0 ? 1 : -1);
        if (slot < 0)
        {
            arm7Audio.stop(fading_[0].handle);
            fading_[0] = fading_[1];
            slot = 1;
        }
        fading_[slot].handle = handle;
        fading_[slot].level = static_cast<std::uint8_t>(level / 2);
        arm7Audio.setVolume(handle, static_cast<int>(level));
    }

    /// Every frame: what is on its way out is half as loud again, and gone
    /// when there is nothing left of it (three or four frames).
    void Sound::fadeTick()
    {
        for (Fading &fading : fading_)
        {
            if (fading.handle == 0)
                continue;
            fading.level = static_cast<std::uint8_t>(fading.level / 2);
            if (fading.level < 4)
            {
                arm7Audio.stop(fading.handle);
                fading = Fading();
            }
            else
            {
                arm7Audio.setVolume(fading.handle, static_cast<int>(fading.level * 2));
            }
        }
    }

    void Sound::killFading()
    {
        for (Fading &fading : fading_)
        {
            if (fading.handle != 0)
                arm7Audio.stop(fading.handle);
            fading = Fading();
        }
    }

    /// A weapon's sound the map has not, about to be played: read now if it
    /// is not in memory (a frame's wait, once), and said to be played.
    void Sound::lazyTouch(int sound)
    {
        if (lazy_ == nullptr || sound < 0 || sound >= count_ || lazy_[sound] == 0)
            return;
        const std::uint32_t now = Global::vblanks();
        for (int k = 0; k < kLazyClips; ++k)
        {
            if (lazyClip_[k] == sound)
            {
                lazyPlayed_[k] = now;
                return;
            }
        }
        // A place: an empty one, or the one played longest ago -- as long as
        // it is over, at the slowest it may be played (twice as long) and a
        // half second more.
        int place = -1;
        for (int k = 0; k < kLazyClips && place < 0; ++k)
            place = lazyClip_[k] < 0 ? k : -1;
        if (place < 0)
        {
            std::uint32_t oldest = 0;
            for (int k = 0; k < kLazyClips; ++k)
            {
                const std::uint32_t ago = now - lazyPlayed_[k];
                const std::uint32_t lasts = clips_[lazyClip_[k]].ms * 2 * 60 / 1000 + 30;
                if (ago > lasts && ago >= oldest)
                {
                    oldest = ago;
                    place = k;
                }
            }
        }
        if (place < 0)
            return;
        if (lazyClip_[place] >= 0)
        {
            const int gone = lazyClip_[place];
            std::free(clips_[gone].data);
            clips_[gone] = Clip();
            lazyClip_[place] = -1;
        }
        if (global.heapHeadroom() < 200 * 1024 || !readClip(level.view.sounds[sound].name, clips_[sound]))
            return;
        describe(clips_[sound]);
        lazyClip_[place] = sound;
        lazyPlayed_[place] = now;
    }

    void Sound::playOnce(const SoundEvent &event)
    {
        lazyTouch(event.sound);
        if (event.sound >= count_ || clips_[event.sound].data == nullptr)
            return;
        const Clip &clip = clips_[event.sound];
        int volume = event.volume, pan = 64;
        const bool placed = !(event.flags & kSoundHere);
        if (placed)
            place(event.pos, event.volume, event.radius, volume, pan);
        const int v = start(event.layer, event.sound, clip, volume, pan, false, -1, event.pitch);
        if (v < 0 || !placed)
            return;
        Voice &voice = voices_[v];
        voice.placed = true;
        voice.volume = event.volume;
        voice.radius = event.radius;
        for (int k = 0; k < 3; ++k)
            voice.pos[k] = event.pos[k];
    }

    /// What something makes for as long as it moves (a lift, a train, a
    /// door that takes its time): round and round from where it is, until
    /// stopLoop. A sound that does not go round is simply played.
    void Sound::playLoop(const SoundEvent &event)
    {
        lazyTouch(event.sound);
        if (event.sound >= count_ || clips_[event.sound].data == nullptr)
            return;
        const Clip &clip = clips_[event.sound];
        if (clip.header.repeat != MM_SREPEAT_FORWARD)
        {
            playOnce(event);
            return;
        }
        // Already going round: it is where it is now (what moves as it makes
        // it -- an osprey), and it does not start twice.
        for (int v = 0; v < Mixer::kVoices; ++v)
        {
            const MixVoice &mix = mixer_.voices[v];
            if (mix.active && mix.layer == event.layer && mix.leftMs < 0 && mix.tag == event.tag)
            {
                for (int k = 0; k < 3; ++k)
                    voices_[v].pos[k] = event.pos[k];
                return;
            }
        }
        int volume = event.volume, pan = 64;
        const bool placed = !(event.flags & kSoundHere);
        if (placed)
            place(event.pos, event.volume, event.radius, volume, pan);
        const int v = start(event.layer, event.sound, clip, volume, pan, true, event.tag, event.pitch);
        if (v < 0)
            return;
        Voice &voice = voices_[v];
        voice.placed = placed;
        voice.volume = event.volume;
        voice.radius = event.radius;
        for (int k = 0; k < 3; ++k)
            voice.pos[k] = event.pos[k];
    }

    void Sound::stopLoop(int tag)
    {
        for (int v = 0; v < Mixer::kVoices; ++v)
        {
            const MixVoice &mix = mixer_.voices[v];
            if (mix.active && mix.layer != SoundLayer::Ambience && mix.layer != SoundLayer::Voice &&
                mix.leftMs < 0 && mix.tag == tag)
                retire(v);
        }
    }

    /// What has a place is put there again as the camera moves and turns:
    /// louder and quieter, and from one side to the other.
    void Sound::follow()
    {
        for (int v = 0; v < Mixer::kVoices; ++v)
        {
            Voice &voice = voices_[v];
            MixVoice &mix = mixer_.voices[v];
            if (!mix.active || !voice.placed || mix.layer == SoundLayer::Ambience)
                continue;
            int volume, pan;
            place(voice.pos, voice.volume, voice.radius, volume, pan);
            const int gain = mixer_.scaled(mix.layer, volume);
            const int dv = gain - voice.shown, dp = pan - voice.pan;
            if (dv > 2 || dv < -2)
            {
                voice.shown = static_cast<std::uint8_t>(gain);
                mix.gain = voice.shown;
                arm7Audio.setVolume(voice.handle, static_cast<int>(gain * 2));
            }
            if (dp > 3 || dp < -3)
            {
                voice.pan = static_cast<std::uint8_t>(pan);
                arm7Audio.setPanning(voice.handle, static_cast<int>(pan * 2));
            }
        }
    }

    void Sound::releaseWord(Speech &speech, int which)
    {
        speech.loaded[which] = false;
        std::free(speech.clip[which].data);
        speech.clip[which] = Clip();
    }

    void Sound::say(const SoundEvent &event)
    {
        const LevelView &view = level.view;
        const int sentence = event.sound;
        if (sentence < 0 || sentence >= static_cast<int>(view.sentenceCount))
            return;
        // Whose voice: the one that is already this speaker's (a line cuts
        // in on the same speaker's last, as on the game's own voice
        // channel); else one that is silent; else the one that has been
        // speaking longest. The same speaker is the same place -- or no
        // place at all, which is the voice that is heard everywhere.
        const bool placed = !(event.flags & kSoundHere);
        Speech *speech = nullptr;
        for (Speech &other : speech_)
        {
            if (!other.busy() || other.placed != placed)
                continue;
            const fx d[3] = {other.pos[0] - event.pos[0], other.pos[1] - event.pos[1], other.pos[2] - event.pos[2]};
            if (!placed || fxLength(d) < fxFromInt(96))
                speech = &other;
        }
        for (Speech &other : speech_)
        {
            if (speech == nullptr && !other.busy())
                speech = &other;
        }
        if (speech == nullptr)
            speech = speech_[0].born < speech_[1].born ? &speech_[0] : &speech_[1];

        const SentenceRecord &record = view.sentences[sentence];
        speech->count = 0;
        speech->at = 0;
        for (int w = 0; w < record.wordCount && speech->count < kMaxSpoken; ++w)
            speech->words[speech->count++] = static_cast<std::uint16_t>(record.firstWord + w);
        // (As loud as the line is said and as whoever says it is far off:
        // a line from somewhere comes from there.)
        speech->volume = (record.volume * event.volume) / 127;
        speech->pitch = event.pitch; // (each word has its own on top: SentenceWord)
        speech->placed = placed;
        speech->radius = event.radius;
        for (int k = 0; k < 3; ++k)
            speech->pos[k] = event.pos[k];
        speech->born = ++speechClock_;
        speech->leftMs = 1; // (the word in the air is cut short at the next tick)
        // (What was read ahead was the old line's next word.)
        releaseWord(*speech, speech->cur ^ 1);
        speech->ahead = -1;
    }

    /// The word after the one in the air, into the buffer that one is not
    /// in: read while there is time, so that the line does not stop for the
    /// card between two words.
    void Sound::readAhead(Speech &speech, int index)
    {
        const int other = speech.cur ^ 1;
        releaseWord(speech, other);
        speech.ahead = -1;
        if (index >= static_cast<int>(level.view.soundCount) || level.view.sounds[index].name[0] == '\0' ||
            (index < count_ && clips_[index].data != nullptr) || global.heapHeadroom() < 200 * 1024)
            return;
        if (!readClip(level.view.sounds[index].name, speech.clip[other]))
            return;
        describe(speech.clip[other]);
        speech.loaded[other] = up_;
        if (speech.loaded[other])
            speech.ahead = index;
        else
            releaseWord(speech, other);
    }

    void Sound::speechTick(int ms)
    {
        for (int s = 0; s < kSpeakers; ++s)
        {
            Speech &speech = speech_[s];
            if (!speech.busy() && speech.voice < 0)
                continue;
            if (speech.leftMs > 0)
            {
                speech.leftMs -= ms;
                if (speech.leftMs > 0)
                    continue;
            }
            // The word in the air is over: its voice and its memory can
            // go, and the next is said.
            if (speech.voice >= 0 && mixer_.voices[speech.voice].active &&
                mixer_.voices[speech.voice].layer == SoundLayer::Voice && mixer_.voices[speech.voice].tag == s)
            {
                // (A word read off the card is played out of memory that is
                // about to be given back: that one is stopped dead. One of
                // the map's own sounds may fade.)
                if (speech.clip[speech.cur].data != nullptr)
                    stop(speech.voice);
                else
                    retire(speech.voice);
            }
            speech.voice = -1;
            releaseWord(speech, speech.cur);
            bool said = false;
            while (speech.at < speech.count && !said)
            {
                // How this word is said: the game's marks on it.
                const SentenceWord &word = level.view.sentenceWords[speech.words[speech.at++]];
                const std::uint16_t index = word.sound;
                if (index >= level.view.soundCount || level.view.sounds[index].name[0] == '\0')
                    continue;
                const int pitch = (speech.pitch * (word.pitch != 0 ? word.pitch : 100)) / 100;
                const int loud = (speech.volume * word.volume) / 100;
                int volume = loud, pan = 64;
                if (speech.placed)
                    place(speech.pos, loud, speech.radius, volume, pan);
                // (In memory already, if something else of the map plays it.)
                const bool borrowed = index < count_ && clips_[index].data != nullptr;
                int v = -1;
                std::uint32_t lasts = 0;
                if (borrowed)
                {
                    lasts = clips_[index].ms;
                    v = start(SoundLayer::Voice, index, clips_[index], volume, pan, false, s, pitch);
                }
                else
                {
                    // Read ahead while the last word was said; or read now.
                    if (speech.ahead != index)
                        readAhead(speech, index);
                    if (speech.ahead != index)
                        continue; // (not on the card, or no memory for it)
                    speech.cur ^= 1;
                    speech.ahead = -1;
                    Clip &clip = speech.clip[speech.cur];
                    lasts = clip.ms;
                    v = start(SoundLayer::Voice, kSpeechSample + s * 2 + speech.cur, clip, volume, pan, false, s,
                              pitch);
                }
                if (v >= 0 && speech.placed)
                {
                    Voice &voice = voices_[v];
                    voice.placed = true;
                    voice.volume = static_cast<std::uint8_t>(loud);
                    voice.radius = speech.radius;
                    for (int k = 0; k < 3; ++k)
                        voice.pos[k] = speech.pos[k];
                }
                // (Too far off to be heard, it still takes its time.)
                speech.voice = v;
                // (Said faster it is over sooner; and a word may be cut off
                // before its end, which is how the game clips its speech.)
                speech.leftMs = static_cast<std::int32_t>(lasts * 100 / (pitch > 0 ? pitch : 100)) *
                                    (word.end != 0 ? word.end : 100) / 100 +
                                (word.end != 0 && word.end < 100 ? 0 : 20);
                said = true;
            }
            if (!said)
            {
                speech.count = speech.at = 0;
                releaseWord(speech, speech.cur ^ 1);
                speech.ahead = -1;
                continue;
            }
            // ... and the word after it is fetched while this one is said.
            if (speech.at < speech.count)
                readAhead(speech, level.view.sentenceWords[speech.words[speech.at]].sound);
        }
    }

    namespace
    {
        /// A track train's engine's pitch, in percent (plats.cpp's
        /// UpdateSound: 60 standing, 200 at a thousand units a second).
        int trainPitch(fx speed)
        {
            const int units = fxToInt(speed < 0 ? -speed : speed);
            const int pitch = 60 + units * (200 - 60) / 1000;
            return pitch > 200 ? 200 : pitch;
        }
    } // namespace

    void Sound::loopsTick()
    {
        const LevelView &view = level.view;
        // The loudest of the loops that are on, where the camera is: up to
        // kLoops of them while there are voices for it, and never fewer than
        // the layer's own share (the mixer sees to both).
        int want[kLoops], wantVolume[kLoops], wantPan[kLoops], wantWeight[kLoops];
        int wanted = 0;
        for (std::uint32_t i = 0; i < view.logicCount; ++i)
        {
            const LogicRecord &record = view.logic[i];
            const bool ambient = record.kind == static_cast<std::uint8_t>(LogicKind::Ambient) &&
                                 (record.flags & kLogicToggle);
            // (Something that turns hums for as long as it does; a track
            // train's engine runs for as long as it goes.)
            const bool train = record.kind == static_cast<std::uint8_t>(LogicKind::TrackTrain);
            const bool turning = (record.kind == static_cast<std::uint8_t>(LogicKind::Rotating) || train) &&
                                 view.logicSoundCount > i && view.logicSounds[i].move != kNoSound;
            if (!ambient && !turning)
                continue;
            const std::uint32_t sample = ambient ? record.extra[0] : view.logicSounds[i].move;
            const bool on = (train ? game.logic.states[i].speed != 0 : game.logic.states[i].running) &&
                            !game.logic.states[i].dead;
            if (!on && played_ != nullptr)
                played_[i >> 5] &= ~(1u << (i & 31));
            if (!on || sample >= static_cast<std::uint32_t>(count_) || clips_[sample].data == nullptr)
                continue;
            // Something that does not go round is played once when it is
            // switched on, as the game plays it, and that is all.
            if (clips_[sample].header.repeat != MM_SREPEAT_FORWARD && played_ != nullptr &&
                (played_[i >> 5] & (1u << (i & 31))))
                continue;
            int volume = ambient ? static_cast<int>(record.extra[2]) : 100, pan = 64;
            if (turning && record.brush != kNoBrush)
            {
                const MoverHull &hull = game.logic.movers[record.brush];
                const fx at[3] = {(hull.mins[0] + hull.maxs[0]) / 2, (hull.mins[1] + hull.maxs[1]) / 2,
                                  (hull.mins[2] + hull.maxs[2]) / 2};
                place(at, volume, kRadiusLarge, volume, pan);
            }
            else if (ambient && record.extra[1] != 0)
            {
                place(record.move, volume, static_cast<int>(record.extra[1]), volume, pan);
            }
            if (mixer_.scaled(SoundLayer::Ambience, volume) < 3)
                continue;
            // What is playing already is not put out by something only a
            // little louder: two of a loudness would change places ten
            // times a second.
            int weight = volume;
            for (int v = 0; v < Mixer::kVoices; ++v)
            {
                if (mixer_.voices[v].active && mixer_.voices[v].layer == SoundLayer::Ambience &&
                    mixer_.voices[v].tag == static_cast<int>(i))
                    weight += 12;
            }
            int at = wanted < kLoops ? wanted : kLoops - 1;
            if (wanted >= kLoops && weight <= wantWeight[at])
                continue;
            while (at > 0 && wantWeight[at - 1] < weight)
            {
                want[at] = want[at - 1];
                wantVolume[at] = wantVolume[at - 1];
                wantPan[at] = wantPan[at - 1];
                wantWeight[at] = wantWeight[at - 1];
                --at;
            }
            want[at] = static_cast<int>(i);
            wantVolume[at] = volume;
            wantPan[at] = pan;
            wantWeight[at] = weight;
            if (wanted < kLoops)
                ++wanted;
        }
        // Those already playing stay on their voices (and are only told of
        // a change worth telling); the rest stop; the new ones start.
        bool placed[kLoops] = {};
        for (int v = 0; v < Mixer::kVoices; ++v)
        {
            MixVoice &mix = mixer_.voices[v];
            if (!mix.active || mix.layer != SoundLayer::Ambience)
                continue;
            int found = -1;
            for (int n = 0; n < wanted; ++n)
            {
                if (want[n] == mix.tag)
                    found = n;
            }
            if (found < 0)
            {
                // (What does not go round is left to finish.)
                if (mix.leftMs < 0)
                    retire(v);
                continue;
            }
            placed[found] = true;
            Voice &voice = voices_[v];
            const int gain = mixer_.scaled(SoundLayer::Ambience, wantVolume[found]);
            const int dv = gain - voice.shown, dp = wantPan[found] - voice.pan;
            if (dv > 2 || dv < -2)
            {
                voice.shown = static_cast<std::uint8_t>(gain);
                mix.gain = voice.shown;
                arm7Audio.setVolume(voice.handle, static_cast<int>(gain * 2));
            }
            if (dp > 3 || dp < -3)
            {
                voice.pan = static_cast<std::uint8_t>(wantPan[found]);
                arm7Audio.setPanning(voice.handle, static_cast<int>(voice.pan * 2));
            }
            // (A train's engine is higher the faster it goes.)
            if (view.logic[want[found]].kind == static_cast<std::uint8_t>(LogicKind::TrackTrain) && !paused_)
            {
                const int rate = (1024 * trainPitch(game.logic.states[want[found]].speed)) / 100;
                if (rate - voice.rate > 16 || voice.rate - rate > 16)
                {
                    voice.rate = static_cast<std::uint16_t>(rate);
                    arm7Audio.setRate(voice.handle, voice.rate);
                }
            }
        }
        for (int n = 0; n < wanted; ++n)
        {
            if (placed[n])
                continue;
            const LogicRecord &record = view.logic[want[n]];
            const bool ambient = record.kind == static_cast<std::uint8_t>(LogicKind::Ambient);
            const int sample = static_cast<int>(ambient ? record.extra[0] : view.logicSounds[want[n]].move);
            const Clip &clip = clips_[sample];
            const bool round = clip.header.repeat == MM_SREPEAT_FORWARD;
            const bool train = record.kind == static_cast<std::uint8_t>(LogicKind::TrackTrain);
            const int v = start(SoundLayer::Ambience, sample, clip, wantVolume[n], wantPan[n], round, want[n],
                                train ? trainPitch(game.logic.states[want[n]].speed)
                                      : (ambient && record.pad != 0 ? record.pad : 100));
            if (v >= 0 && !round && played_ != nullptr)
                played_[want[n] >> 5] |= 1u << (want[n] & 31);
        }
        // What something made for as long as it moved, and is no longer
        // there to say it has stopped.
        for (int v = 0; v < Mixer::kVoices; ++v)
        {
            const MixVoice &mix = mixer_.voices[v];
            if (mix.active && mix.layer == SoundLayer::World && mix.leftMs < 0 && mix.tag >= 0 &&
                mix.tag < static_cast<int>(view.logicCount) && game.logic.states[mix.tag].dead)
                stop(v);
        }
    }

    void Sound::roomTick()
    {
        // What the room sounds like: under water, that; otherwise the
        // nearest of the map's marked places that the player is within.
        int type = 0;
        if (player.state.waterLevel == 3)
        {
            type = 15;
        }
        else
        {
            fx nearest = 0;
            const LevelView &view = level.view;
            for (std::uint32_t i = 0; i < view.roomCount; ++i)
            {
                const RoomRecord &room = view.rooms[i];
                const fx d[3] = {room.origin[0] - camera.pos[0], room.origin[1] - camera.pos[1],
                                 room.origin[2] - camera.pos[2]};
                const fx distance = fxLength(d);
                if (distance > fxFromInt(room.radius) || (type != 0 && distance >= nearest))
                    continue;
                // (A place marked "normal" counts: walking into one is how a
                // map ends an echo.)
                type = room.type == 0 ? -1 : room.type;
                nearest = distance;
            }
        }
        if (type != 0)
            dspAudio.room(roomEcho(type < 0 ? 0 : type));
    }

    void Sound::ui(int which, int volume)
    {
        if (which < 0 || which >= kUiSoundCount || ui_[which].data == nullptr)
            return;
        start(SoundLayer::Ui, kUiSample + which, ui_[which], volume, 64, false);
    }

    void Sound::setPaused(bool paused)
    {
        if (paused == paused_)
            return;
        killFading();
        lastVblank_ = Global::vblanks();
        // Everything of the game's own stops where it is, and goes on from
        // there: slowed to the slowest a channel goes, and silent, for as
        // long as the game stands still (Sound.hpp).
        for (int v = 0; v < Mixer::kVoices; ++v)
        {
            const Voice &voice = voices_[v];
            if (!mixer_.voices[v].active || mixer_.voices[v].layer == SoundLayer::Ui || voice.handle == 0)
                continue;
            if (paused)
            {
                // (A channel's clock is sixteen bits of the 16.7 MHz bus:
                // 256 Hz is as slow as it goes. A little above that.)
                const int slow = (300 * 1024) / (voice.hz > 0 ? voice.hz : 11025) + 1;
                arm7Audio.setVolume(voice.handle, 0);
                arm7Audio.setRate(voice.handle, static_cast<int>(slow));
            }
            else
            {
                arm7Audio.setRate(voice.handle, voice.rate);
                arm7Audio.setVolume(voice.handle, static_cast<int>(voice.shown * 2));
            }
        }
        if (paused)
        {
            // (The music is put down as it is across a load, and taken up
            // from the same moment.)
            holdMusic();
            paused_ = true;
        }
        else
        {
            paused_ = false;
            resumeMusic();
        }
    }

    int Sound::musicTrack() const
    {
        return dspAudio.musicPlaying() ? dspAudio.musicTrack() : musicTrack_;
    }

    std::uint32_t Sound::musicMs() const
    {
        if (musicHeld_)
            return musicHeldMs_;
        if (dspAudio.musicPlaying())
            return static_cast<std::uint32_t>((static_cast<std::uint64_t>(dspAudio.musicPair()) * 1000) / kStereoRate);
        if (music_ == nullptr)
            return 0;
        const std::uint32_t played = musicPlayed();
        return static_cast<std::uint32_t>((static_cast<std::uint64_t>(musicSkipped_ + played) * 1000) / kMusicRate);
    }

    void Sound::setMusic(int track, std::uint32_t ms)
    {
        stopMusic();
        dspAudio.stopMusic();
        musicHeld_ = false;
        musicTrack_ = -1;
        if (track < 0)
            return;
        // Put down at that moment, and taken up from it -- at once, or when
        // the game next goes on, as any music that was put down is.
        musicHeld_ = true;
        musicTrack_ = track;
        musicHeldMs_ = ms;
        resumeMusic();
    }

    void Sound::startMusic(int track)
    {
        if (dspAudio.musicPlaying() && dspAudio.musicTrack() == track)
            return;
        if (track == musicTrack_ && music_ != nullptr)
            return;
        const std::uint32_t skip = musicSkipped_;
        musicSkipped_ = 0;
        stopMusic();
        dspAudio.stopMusic();
        // A DSi's DSP plays it in stereo, if it is there and so is the file.
        if (dspAudio.startMusic(track, 0))
            return;
        if (!up_)
            return;
        char name[16];
        musicFileName(track, name, sizeof(name));
        const std::string path = global.dataPath("music/") + name;
        std::FILE *file = std::fopen(path.c_str(), "rb");
        if (file == nullptr)
            return;
        MusicHeader header;
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        if (std::fread(&header, sizeof(header), 1, file) != 1 ||
            std::memcmp(header.magic, kMusicMagic, 4) != 0 || header.version != kSoundVersion ||
            header.rate != kMusicRate || size < static_cast<long>(sizeof(header)) ||
            header.bytes > static_cast<std::uint32_t>(size) - sizeof(header) ||
            global.heapHeadroom() < 300 * 1024)
        {
            std::fclose(file);
            return;
        }
        // (No buffer of the C library's own: the reads are small and
        // regular, and a second copy of each is time and memory for
        // nothing.)
        std::setvbuf(file, nullptr, _IONBF, 0);
        music_ = file;
        musicTrack_ = track;
        musicLeft_ = header.bytes;
        musicFed_ = 0;
        musicPast_ = 0;
        // Taken up part of the way in, if it was put down there.
        if (skip != 0 && skip < musicLeft_)
        {
            std::fseek(music_, static_cast<long>(sizeof(MusicHeader) + skip), SEEK_SET);
            musicLeft_ -= skip;
            musicSkipped_ = skip;
        }
        // A ring the ARM7 plays round and round, which musicTick keeps
        // filled ahead of it: eight bits, one channel, as the file is.
        std::memset(musicRing, 0, sizeof(musicRing));
        DC_FlushRange(musicRing, sizeof(musicRing));
        arm7Audio.musicOpen(musicRing, sizeof(musicRing), kMusicRate);
        musicTick();
    }

    std::uint32_t Sound::musicPlayed() const
    {
        // (What the ARM7 has played of what was handed over, less any
        // silence after the end.)
        const std::uint32_t read = arm7Audio.musicRead(), fed = musicFed_ - musicPast_;
        return read < fed ? read : fed;
    }

    void Sound::fillMusic(std::int8_t *out, std::uint32_t count)
    {
        const std::uint32_t want = music_ == nullptr ? 0 : (musicLeft_ < count ? musicLeft_ : count);
        const std::size_t got = want != 0 ? std::fread(out, 1, want, music_) : 0;
        musicLeft_ -= static_cast<std::uint32_t>(got);
        // As loud as the player has the music (the track itself is played
        // at about four fifths of full).
        const int level = settings.volume[kVolumeMusic] * 205; // of 2560
        for (std::size_t i = 0; i < got; ++i)
            out[i] = static_cast<std::int8_t>((out[i] * level) / 2560);
        // Past the end of the track: silence.
        if (got < count)
            std::memset(out + got, 0, count - got);
        musicFed_ += count;
        musicPast_ += count - static_cast<std::uint32_t>(got);
    }

    void Sound::stopMusic()
    {
        if (music_ == nullptr)
            return;
        arm7Audio.musicStop();
        std::fclose(music_);
        music_ = nullptr;
        musicTrack_ = -1;
    }

    void Sound::musicTick()
    {
        if (music_ == nullptr)
            return;
        // The ring filled up to just short of where the ARM7 is playing (a
        // ring's worth ahead; if it got ahead of what was written -- a long
        // load -- the writing starts again from where it is).
        const std::uint32_t read = arm7Audio.musicRead();
        std::uint32_t written = arm7Audio.musicWritten();
        if (static_cast<std::int32_t>(written - read) < 0)
        {
            arm7Audio.musicWrote(read - written);
            written = read;
        }
        const std::uint32_t target = read + kStreamSamples - 256;
        while (static_cast<std::int32_t>(target - written) > 0)
        {
            const std::uint32_t at = written & (kStreamSamples - 1);
            std::uint32_t n = target - written;
            n = n < kStreamSamples - at ? n : kStreamSamples - at;
            fillMusic(musicRing + at, n);
            DC_FlushRange(musicRing + at, n);
            arm7Audio.musicWrote(n);
            written += n;
        }
        // The track is over once all of it has been played.
        if (musicLeft_ == 0 && read >= musicFed_ - musicPast_)
            stopMusic();
    }

    void Sound::frame()
    {
        const std::uint32_t now = Global::vblanks();
        const int ms = static_cast<int>(((now - lastVblank_) * 1000 + 30) / 60);
        lastVblank_ = now;
        musicTick();
        dspAudio.frame();
        // A DSP that has let the game down is done without from here on:
        // the music goes on the DS's way.
        if (extended_ && dspAudio.failed())
        {
            // (Said in the log, so that it is known to have happened.)
            if (std::FILE *log = std::fopen(global.dataPath("perf.txt").c_str(), "a"))
            {
                std::fprintf(log, "dsp given up: %d faults, ran short %lu times, %s\n", dspAudio.faults(),
                             static_cast<unsigned long>(dspAudio.dry()),
                             dspAudio.up() ? "answering" : "not answering");
                std::fclose(log);
            }
            setDsp(false);
        }
        // What has run its time is over: its voice is maxmod's again.
        fadeTick();
        const std::uint32_t ended = mixer_.tick(ms, paused_);
        for (int v = 0; v < Mixer::kVoices; ++v)
        {
            if (!((ended >> v) & 1))
                continue;
            if (voices_[v].handle != 0)
                arm7Audio.stop(voices_[v].handle);
            voices_[v] = Voice();
        }
        if (clips_ == nullptr)
            return;
        SoundQueue &queue = game.heard;
        if (paused_)
        {
            queue.count = 0;
            return;
        }
        if (dspAudio.up())
        {
            roomDue_ -= ms;
            if (roomDue_ <= 0)
            {
                roomDue_ = 500;
                roomTick();
            }
        }
        for (int i = 0; i < queue.count; ++i)
        {
            const SoundEvent &event = queue.events[i];
            if (event.flags & kSoundSentence)
                say(event);
            else if (event.flags & kSoundLoopOff)
                stopLoop(event.tag);
            else if (event.flags & kSoundLoopOn)
                playLoop(event);
            else
                playOnce(event);
        }
        queue.count = 0;
        speechTick(ms);
        // The loops, and whatever has a place, are looked at a few times a
        // second: each change is a message to the other processor.
        loopsDue_ -= ms;
        if (loopsDue_ <= 0)
        {
            loopsDue_ = 100;
            loopsTick();
            follow();
        }
        if (game.logic.musicRequest >= 0)
        {
            startMusic(game.logic.musicRequest);
            game.logic.musicRequest = -1;
        }
    }
} // namespace hlrt
