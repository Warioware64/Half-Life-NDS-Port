#include "ui/Keyboard.hpp"

#include <cstdio>
#include <nds.h>

#include "HlFont.hpp"
#include "HlUi.hpp"
#include "audio/Sound.hpp"
#include "ui/TouchCanvas.hpp"

namespace hlrt
{
    extern TouchCanvas touchCanvas;

    namespace
    {
        using namespace hldata;

        const char *const kKeys[4] = {"1234567890", "qwertyuiop", "asdfghjkl_", "zxcvbnm-.<"};
        constexpr int kPromptHigh = 16;
        constexpr int kKey = 24, kLeft = 8;
        constexpr int kBottom = 180, kWideHigh = 12;
    } // namespace

    Keyboard::Keyboard(std::string &text, std::size_t most, int promptTop)
        : text_(text), most_(most), promptTop_(promptTop), keysTop_(promptTop + 18)
    {
    }

    void Keyboard::drawPrompt()
    {
        Canvas &canvas = touchCanvas.canvas();
        const FontView &plain = touchCanvas.plain();
        canvas.fill(0, promptTop_, 256, kPromptHigh, kUiBack);
        canvas.frame(6, promptTop_, 244, kPromptHigh, kUiEdge);
        // (A long line: its end, which is where the typing is.)
        char line[48];
        const std::size_t shown = 27;
        const char *from = text_.size() > shown ? text_.c_str() + (text_.size() - shown) : text_.c_str();
        std::snprintf(line, sizeof(line), "%s%s_", text_.size() > shown ? "<" : "] ", from);
        drawText(canvas, plain, 12, promptTop_ + (kPromptHigh - plain.height()) / 2, line, inkFor(plain, kUiWhite));
    }

    void Keyboard::key(int row, int col, bool down)
    {
        Canvas &canvas = touchCanvas.canvas();
        const FontView &font = touchCanvas.font(kFontMedium);
        const char text[2] = {kKeys[row][col], '\0'};
        if (text[0] == ' ')
            return;
        canvas.button(kLeft + col * kKey, keysTop_ + row * kKey, kKey - 1, kKey - 1, down);
        drawTextCentred(canvas, font, kLeft + col * kKey + kKey / 2, keysTop_ + row * kKey + (kKey - font.height()) / 2,
                        text, inkFor(font, down ? kUiWhite : kUiOrange));
    }

    void Keyboard::wide(int n, bool down)
    {
        static const char *const kNames[3] = {"SPACE", "ENTER", "CANCEL"};
        Canvas &canvas = touchCanvas.canvas();
        const FontView &small = touchCanvas.font(kFontSmall);
        canvas.button(kLeft + n * 80, kBottom, 79, kWideHigh, down);
        drawTextCentred(canvas, small, kLeft + n * 80 + 40, kBottom + (kWideHigh - small.height()) / 2, kNames[n],
                        inkFor(small, down ? kUiWhite : kUiOrange));
    }

    void Keyboard::draw()
    {
        drawPrompt();
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 10; ++col)
                key(row, col, false);
        }
        for (int n = 0; n < 3; ++n)
            wide(n, false);
    }

    Keyboard::Event Keyboard::poll()
    {
        const std::uint32_t pressed = keysDown();
        if (pressed & (KEY_B | KEY_START))
            return Event::Cancel;
        if (pressed & KEY_A)
            return Event::Enter;
        if (pressed & KEY_UP)
            return Event::Up;
        if (pressed & KEY_DOWN)
            return Event::Down;
        if (pressed & KEY_L)
            return Event::PageBack;
        if (pressed & KEY_R)
            return Event::PageOn;
        Event event = Event::None;
        const bool down = (keysHeld() & KEY_TOUCH) != 0;
        if (down && !touching_)
        {
            touchPosition touch;
            touchRead(&touch);
            const int col = (touch.px - kLeft) / kKey, row = (touch.py - keysTop_) / kKey;
            if (touch.py >= kBottom && touch.py < kBottom + kWideHigh && touch.px >= kLeft && touch.px < kLeft + 240)
            {
                const int n = (touch.px - kLeft) / 80;
                sound.ui(kUiSoundPress);
                heldWide_ = n;
                wide(n, true);
                if (n == 1)
                    event = Event::Enter;
                else if (n == 2)
                    event = Event::Cancel;
                else if (text_.size() < most_)
                {
                    text_.push_back(' ');
                    event = Event::Changed;
                }
            }
            else if (touch.px >= kLeft && touch.py >= keysTop_ && col < 10 && row < 4 && kKeys[row][col] != ' ')
            {
                const char c = kKeys[row][col];
                sound.ui(kUiSoundTick);
                if (c == '<')
                {
                    if (!text_.empty())
                        text_.pop_back();
                }
                else if (text_.size() < most_)
                {
                    text_.push_back(c);
                }
                heldRow_ = row;
                heldCol_ = col;
                key(row, col, true);
                event = Event::Changed;
            }
            if (event == Event::Changed)
                drawPrompt();
        }
        else if (!down && heldRow_ >= 0)
        {
            key(heldRow_, heldCol_, false);
            heldRow_ = heldCol_ = -1;
        }
        else if (!down && heldWide_ >= 0)
        {
            wide(heldWide_, false);
            heldWide_ = -1;
        }
        touching_ = down;
        return event;
    }
} // namespace hlrt
