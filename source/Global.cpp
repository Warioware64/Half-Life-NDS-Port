#include "Global.hpp"
#include <cstdio>
#include <nds/arm9/dldi.h>

#include "HlManifest.hpp"
#include "data/VramWriter.hpp"
#include "render/HudDraw.hpp"
#include "render/Camera.hpp"
#include "game/Think.hpp"
#include "render/GpuStats.hpp"

Global global;

namespace
{
    volatile std::uint32_t vblankCount = 0;

    /// NEA's own vblank work, plus the tick the game's clock runs on.
    void onVblank()
    {
        vblankCount = vblankCount + 1;
        // First: what is written to video memory has only until line 206.
        hlrt::vramWriter.onVblank();
        hlrt::GpuStats::onVblank();
        // The display's sprites, as the frame before made them.
        hlrt::hudDraw.onVblank();
        NEA_VBLFunc();
    }
} // namespace

std::uint32_t Global::vblanks()
{
    return vblankCount;
}

namespace
{
    std::uint32_t drawClockAt = 0;
}

std::uint32_t Global::drawClock()
{
    return drawClockAt;
}

void Global::latchDrawClock()
{
    drawClockAt = vblankCount;
}

void Global::process(void (*draw)())
{
    NEA_Process(draw);
}

void Global::Init(const char *launchedFrom)
{
    // First, before anything is timed against the CPU: every later measurement
    // and every budget chosen from isDSi assumes the clock is already what it
    // is going to be. setCpuClock is a no-op outside DSi mode.
    isDSi = isDSiMode();
    if (isDSi)
    {
        setCpuClock(true);
        fastClock = true;
    }

    irqEnable(IRQ_HBLANK);
    irqSet(IRQ_VBLANK, onVblank);
    irqSet(IRQ_HBLANK, NEA_HBLFunc);

    // SD / flashcard FAT access -- every byte of game content comes from here.
    const bool fatOk = fatInitDefault();
    fatDevice = fatGetDefaultDrive();
    hasFat = fatOk && (fatDevice != nullptr);
    if (hasFat)
    {
        fatDeviceCPP = fatDevice;
    }
    else
    {
        // In DSi mode the card is the console's own SD, reached only when
        // the loader has left it switched on (SCFG_EXT bit 18; the register
        // reads 0 if the loader locked SCFG). In DS mode it is whatever the
        // loader's DLDI driver reaches -- none, if it put none in.
        char text[160];
        const DLDI_INTERFACE *dldi = io_dldi_data;
        std::snprintf(text, sizeof(text), "%s mode, SCFG %08lX, DLDI \"%.24s\", from %.60s", isDSi ? "DSi" : "DS",
                      isDSi ? static_cast<unsigned long>(REG_SCFG_EXT) : 0ul,
                      dldi != nullptr ? dldi->friendlyName : "none",
                      launchedFrom != nullptr && launchedFrom[0] != '\0' ? launchedFrom : "(no path)");
        cardWhy = text;
    }

}

void Global::InitVideo()
{
    videoReady_ = true;
    NEA_Init3D();
    // NEA puts the 3D screen on top by default; said explicitly because the
    // whole control scheme depends on the touch screen NOT being the 3D one.
    NEA_MainScreenSetOnTop();
    // The 3D picture and the sprites at priority 1, so that a fade's colour
    // can be put over both, at 0 (render/ScreenFade.cpp). (The sprites are
    // still over the picture: of the same priority, they are drawn first.)
    REG_BG0CNT = static_cast<std::uint16_t>((REG_BG0CNT & ~3) | BG_PRIORITY(1));
    NEA_SetDepthBufferMode(NEA_WBUFFER);
    // A DSi has a second set of DMA channels, which NEA uses for display
    // lists when it is told it may: they can feed the geometry engine in the
    // modes where the old ones are busy with the screen. (The world's own
    // list is sent the same way: WorldDraw::emitList.)
    NEA_DisplayListEnableNDMA(isDSiMode());
    swiWaitForVBlank();
    swiWaitForVBlank();

    // Room for a map's worth of textures: NEA's defaults (128 materials, 64
    // palettes) are below what one retail map uses, and every 4x4 compressed
    // texture has a palette of its own. The banks must be defined explicitly:
    // "all of them" would hand Bank D to the texture allocator, and Bank D
    // is held back. Every map fits without it. (Compressed textures live in
    // Banks A and C with their indexes in Bank B.)
    NEA_TextureSystemReset(512, 1024, NEA_VRAM_ABC);
    vramSetBankD(VRAM_D_LCD);

    // The projection the world is drawn through. NEA rebuilds it every frame
    // from these, so they are set once.
    NEA_SetFov(hlrt::Camera::kFovDegrees);
    // The worker that thinks for the actors while a frame waits.
    hlrt::think.restart();
    NEA_ClippingPlanesSetI(hlrt::Camera::kNearClip, hlrt::Camera::kFarClip);
    NEA_ClearColorSet(RGB15(0, 0, 0), 31, 63);

    // (No text console: what there is to read is drawn on the touch screen,
    // in the game's own letters -- ui/TouchCanvas.)

    // Bringing the engine up again closed its archives.
    mountCommon();
    if (!mapMounted_.empty() && !NEA_NpacIsMounted("map"))
        NEA_NpacMount("map", mapArchive(mapMounted_).c_str());
}

bool Global::mountMap(const std::string &name)
{
    unmountMap();
    if (!hasFat || NEA_NpacMount("map", mapArchive(name).c_str()) != 0)
        return false;
    mapMounted_ = name;
    return true;
}

void Global::unmountMap()
{
    if (NEA_NpacIsMounted("map"))
        NEA_NpacUnmount("map");
    mapMounted_.clear();
}

void Global::mountCommon()
{
    // (A card with no common archive is a game without its own letters and
    // pictures, which is said where each is missed, not here.)
    if (hasFat && !NEA_NpacIsMounted("common"))
        NEA_NpacMount("common", (dataDir() + "common.npac").c_str());
}

std::size_t Global::heapHeadroom() const
{
    return static_cast<std::size_t>(getHeapLimit() - getHeapEnd());
}

Global::DataStatus Global::ReadManifest(std::string &startMap, std::vector<std::string> *multiplayer,
                                        std::string *why) const
{
    if (!hasFat)
        return DataStatus::NoCard;

    std::FILE *file = std::fopen(dataPath("manifest.bin").c_str(), "rb");
    if (file == nullptr)
        return DataStatus::NoData;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    // A manifest is a few kilobytes. The size is the card's word, so it is
    // bounded before it is used.
    if (size <= 0 || size > 256 * 1024)
    {
        std::fclose(file);
        return DataStatus::Damaged;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    const bool got = std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    std::fclose(file);
    if (!got)
        return DataStatus::Damaged;

    // readManifest checks magic, versions, length and checksum BEFORE yas
    // touches the payload: yas reports a bad archive by throwing, and this
    // build has no exceptions.
    hldata::Manifest manifest;
    switch (hldata::readManifest(bytes.data(), bytes.size(), manifest))
    {
    case hldata::ManifestError::Ok:
        break;
    case hldata::ManifestError::OtherVersion:
        if (why != nullptr)
            *why = hldata::manifestRefusal(bytes.data(), bytes.size());
        return DataStatus::OtherVersion;
    default:
        return DataStatus::Damaged;
    }
    if (manifest.startMap.empty() || manifest.find(manifest.startMap) == nullptr)
        return DataStatus::Damaged;
    startMap = manifest.startMap;
    for (const hldata::ManifestMap &map : manifest.maps)
    {
        if (multiplayer != nullptr && map.spawns != 0)
            multiplayer->push_back(map.name);
    }
    return DataStatus::Ok;
}

const char *Global::DataStatusTitle(DataStatus s)
{
    switch (s)
    {
    case DataStatus::NoCard:       return "No SD card found.";
    case DataStatus::NoData:       return "No Half-Life data on the card.";
    case DataStatus::OtherVersion: return "The data is from another version.";
    case DataStatus::Damaged:      return "The data on the card is damaged.";
    default:                       return "";
    }
}

const char *Global::DataStatusHint(DataStatus s)
{
    switch (s)
    {
    case DataStatus::NoCard:       return "Insert a card the console can read.";
    case DataStatus::NoData:       return "Run the converter first.";
    case DataStatus::OtherVersion: return "Run this version's converter again.";
    case DataStatus::Damaged:      return "Run the converter again.";
    default:                       return "";
    }
}
