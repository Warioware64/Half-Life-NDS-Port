#include "global_header.hpp"

#include "Settings.hpp"

#include "Global.hpp"

Settings settings;

void Settings::load()
{
    if (!global.hasFat)
        return;
    std::FILE *file = std::fopen(global.dataPath("settings.dat").c_str(), "rb");
    if (file == nullptr)
        return;
    std::uint8_t bytes[hldata::kSettingsMostBytes];
    const std::size_t size = std::fread(bytes, 1, sizeof(bytes), file);
    std::fclose(file);

    hldata::SettingsData data;
    if (hldata::readSettings(bytes, size, data))
    {
        dspAudio = data.dspAudio != 0;
        for (int i = 0; i < 4; ++i)
            volume[i] = data.volume[i];
        debugReadout = data.debugReadout != 0;
        leftHanded = data.leftHanded != 0;
        viewBob = data.viewBob != 0;
        graphics = data.graphics;
        controlScheme = data.controlScheme;
        padTurns = data.padTurns != 0;
        padLooks = data.padLooks != 0;
        aimSpeed = data.aimSpeed;
        pitchSpeed = data.pitchSpeed;
        invertPitch = data.invertPitch != 0;
        duckToggle = data.duckToggle != 0;
        firmness = data.firmness;
        firmPress = data.firmPress;
        for (int a = 0; a < hldata::kActionCount; ++a)
            keys[a] = data.keys[a];
        autoaim = data.autoaim != 0;
        achievementPopups = data.achievementPopups != 0;
    }
}

bool Settings::save() const
{
    if (!global.hasFat)
        return false;
    hldata::SettingsData data;
    data.dspAudio = dspAudio ? 1 : 0;
    for (int i = 0; i < 4; ++i)
        data.volume[i] = volume[i];
    data.debugReadout = debugReadout ? 1 : 0;
    data.leftHanded = leftHanded ? 1 : 0;
    data.viewBob = viewBob ? 1 : 0;
    data.graphics = graphics;
    data.controlScheme = controlScheme;
    data.padTurns = padTurns ? 1 : 0;
    data.padLooks = padLooks ? 1 : 0;
    data.aimSpeed = aimSpeed;
    data.pitchSpeed = pitchSpeed;
    data.invertPitch = invertPitch ? 1 : 0;
    data.duckToggle = duckToggle ? 1 : 0;
    data.firmness = firmness;
    data.firmPress = firmPress;
    for (int a = 0; a < hldata::kActionCount; ++a)
        data.keys[a] = keys[a];
    data.autoaim = autoaim ? 1 : 0;
    data.achievementPopups = achievementPopups ? 1 : 0;
    const std::vector<std::uint8_t> bytes = hldata::writeSettings(data);

    std::FILE *file = std::fopen(global.dataPath("settings.dat").c_str(), "wb");
    if (file == nullptr)
        return false;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    return std::fclose(file) == 0 && ok;
}

