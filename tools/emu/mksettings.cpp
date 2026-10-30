// A settings file for an emulator test (tools/emu/README.md): the defaults,
// consoles without the game let in, and the map hosted (argv[2], if given).
// Pressing keys to change a setting is not to be relied on with two consoles
// running: one press is seen twice, or not at all.
//
//   g++ -std=gnu++23 -D_LITTLE_ENDIAN -include yas/detail/config/endian.hpp \
//       -Ishared -Ithirdparty -o build/emu/mksettings tools/emu/mksettings.cpp
//   build/emu/mksettings sdroot/_nds/hl_nds/data/settings.dat stalkyard
#include <cstdio>
#include <cstring>
#include "HlSettings.hpp"
int main(int argc, char **argv)
{
    hldata::SettingsData data;
    data.allowGuests = 1;
    if (argc > 2)
        std::strncpy(data.hostMap, argv[2], sizeof(data.hostMap) - 1);
    const std::vector<std::uint8_t> bytes = hldata::writeSettings(data);
    std::FILE *f = std::fopen(argv[1], "wb");
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    hldata::SettingsData back;
    const bool read = hldata::readSettings(bytes.data(), bytes.size(), back);
    std::printf("%zu bytes, reads back: %d, guests %d, map %s\n", bytes.size(), read, back.allowGuests, back.hostMap);
}
