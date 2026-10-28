// The game's own ARM7 program: what BlocksDS's default (with the wireless
// library the Download Play build needs) does -- the touch screen and the
// buttons, the wifi, the card on a DSi, the clock -- and, instead of maxmod,
// the game's own sound (Audio.cpp), which plays what the ARM9 asks through
// one block of shared memory (shared/HlAudioLink.hpp).

#include <cstdint>

#include <dswifi7.h>
#include <nds.h>

#include "Audio.hpp"

namespace
{
    volatile bool exitLoop = false;

    void powerButton()
    {
        exitLoop = true;
    }

    void vblankHandler()
    {
        inputGetAndSend();
        Wifi_Update();
    }
} // namespace

int main()
{
    enableSound();
    readUserSettings();
    ledBlink(LED_ALWAYS_ON);
    touchInit();
    irqInit();
    fifoInit();

    installWifiFIFO();
    // (libnds's own sound messages stay: the DSi's output rate and mix
    // ratio, and the sound capture the DSi's DSP takes its echo from, are
    // asked for that way.)
    installSoundFIFO();
    installSystemFIFO(); // sleep, the card, the firmware...
    if (isDSiMode())
        installCameraFIFO();

    hlaudio::install();

    setPowerButtonCB(powerButton);
    // (Timer 3: the clock. The sound keeps time by timers 0, 1 and 2.)
    initClockIRQTimer(3);
    irqSet(IRQ_VBLANK, vblankHandler);
    irqEnable(IRQ_VBLANK);

    // The sound's blocks, between interrupts; and SELECT+START+L+R, or the
    // DSi's power button, to leave.
    unsigned blocks = 0;
    while (!exitLoop)
    {
        hlaudio::waitAndRun();
        if (++blocks % 8 == 0)
        {
            const std::uint16_t mask = KEY_SELECT | KEY_START | KEY_L | KEY_R;
            if ((~REG_KEYINPUT & mask) == mask)
                exitLoop = true;
        }
    }
    return 0;
}
