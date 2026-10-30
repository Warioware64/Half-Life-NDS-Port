#include "global_header.hpp"

#include "render/BottomScreen.hpp"

#include "HlPlayerMove.hpp"
#include "Settings.hpp"
#include "Global.hpp"
#include "HlTouchLayout.hpp"
#include "HlWeaponList.hpp"
#include "HlUi.hpp"
#include "net/NetGame.hpp"
#include "ui/TouchCanvas.hpp"

#include <cstring>

namespace hlrt
{
    BottomScreen bottomScreen;

    namespace
    {
        using namespace hldata;

        // The weapons' pictures (ui/weapons.hic), if the card has them.
        std::uint32_t iconWords[(sizeof(IconsHeader) + kWeaponIcons * kIconMaxHeight * iconRowWords(kIconMaxWidth) * 4) / 4];
        IconsView icons;

        /// The touch screen's tiles, as the screen's, for the pad drawn off
        /// it while the weapon list slides (setWeaponList, present).
        std::uint32_t padTiles[768 * 8];

        /// The pad's tiles (it is whole tiles: 208 by 132 from 0 or 48, 24)
        /// copied from `from` to `to`, both laid out as the screen's.
        void copyPad(std::uint32_t *to, const std::uint32_t *from, bool mirrored)
        {
            const TouchRect r = touchLayout(mirrored, netGame.playing()).pad;
            const int left = r.x / 8, right = (r.x + r.w + 7) / 8, top = r.y / 8, bottom = (r.y + r.h + 7) / 8;
            for (int ty = top; ty < bottom; ++ty)
            {
                const int at = (ty * 32 + left) * 8;
                std::memcpy(to + at, from + at, static_cast<std::size_t>(right - left) * 32);
            }
        }

        // What a control is drawn in: at rest a dim amber line on the
        // screen's own dark, held (or in hand) the full amber on a lit
        // plate.
        void plate(const TouchRect &r, bool lit, bool faint = false)
        {
            touchCanvas.canvas().plate(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 4, lit ? kUiPanelDown : kUiBack,
                                       lit ? kUiOrange + 2 : (faint ? kUiPanel : kUiOrange));
        }

        void label(const TouchRect &r, const char *text, bool lit)
        {
            Canvas &canvas = touchCanvas.canvas();
            const FontView &font = touchCanvas.font(kFontSmall);
            plate(r, lit);
            drawTextCentred(canvas, font, r.x + r.w / 2, r.y + (r.h - font.height()) / 2, text,
                            inkFor(font, lit ? kUiWhite : kUiOrange));
        }

        /// A chevron, its point up or down, five lines thick.
        void chevron(int cx, int cy, bool up, int colour)
        {
            Canvas &canvas = touchCanvas.canvas();
            for (int t = 0; t < 5; ++t)
            {
                const int tip = up ? cy - 7 + t : cy + 7 - t, base = up ? cy + 3 + t : cy - 3 - t;
                canvas.line(cx - 11, base, cx, tip, colour);
                canvas.line(cx + 11, base, cx, tip, colour);
            }
        }

        /// A ring, `thick` pixels of it inwards from `radius`.
        void ring(int cx, int cy, int radius, int thick, int colour)
        {
            Canvas &canvas = touchCanvas.canvas();
            const int outer = radius * radius, inner = (radius - thick) * (radius - thick);
            for (int y = -radius; y <= radius; ++y)
            {
                for (int x = -radius; x <= radius; ++x)
                {
                    const int d = x * x + y * y;
                    if (d <= outer && d > inner)
                        canvas.pixel(cx + x, cy + y, colour);
                }
            }
        }
    } // namespace

    void BottomScreen::create()
    {
        touchCanvas.create();
    }

    void BottomScreen::reset()
    {
        touchCanvas.reset();
        shown_ = Shown::Nothing;
    }

    void BottomScreen::release()
    {
        shown_ = Shown::Nothing;
    }

    void BottomScreen::loadIcons()
    {
        if (iconsTried_)
            return;
        iconsTried_ = true;
        if (!global.hasData)
            return;
        std::FILE *file = std::fopen(global.commonPath(kWeaponIconsFile).c_str(), "rb");
        if (file == nullptr)
            return;
        const std::size_t size = std::fread(iconWords, 1, sizeof(iconWords), file);
        std::fclose(file);
        icons.open(reinterpret_cast<const std::uint8_t *>(iconWords), size);
    }

    void BottomScreen::drawBar()
    {
        Canvas &canvas = touchCanvas.canvas();
        // (The bar has other buttons in a match: what was there before is
        // cleared, not drawn over.)
        matchBar_ = netGame.playing();
        const TouchLayout at = touchLayout(mirrored_, matchBar_);
        canvas.fill(0, 0, kTouchWidth, kTouchBarHeight, kUiBack);
        label(at.menu, "MENU", held_ == kTouchMenu);
        label(at.use, "USE", held_ == kTouchUse);
        if (at.spray.w > 0)
            label(at.spray, "SPRAY", held_ == kTouchSpray);
        // RELOAD is faint while there is nothing to reload: no gun in
        // hand, a full one, or nothing carried for it.
        if (canReload_ || held_ == kTouchReload)
        {
            label(at.reload, "RELOAD", held_ == kTouchReload);
        }
        else
        {
            const FontView &font = touchCanvas.font(kFontSmall);
            plate(at.reload, false, true);
            drawTextCentred(canvas, font, at.reload.x + at.reload.w / 2, at.reload.y + (at.reload.h - font.height()) / 2,
                            "RELOAD", inkFor(font, kUiGrey));
        }
    }

    void BottomScreen::drawPad()
    {
        Canvas &canvas = touchCanvas.canvas();
        const TouchRect r = touchLayout(mirrored_, netGame.playing()).pad;
        canvas.plate(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 6, kUiBack, kUiOrange);
        // The mark of the place in its middle, as faint as a line can be:
        // it is where the stylus rests, not something to read.
        const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        ring(cx, cy, 30, 4, kUiPanel);
        for (int t = 0; t < 5; ++t)
        {
            canvas.line(cx - 7 + t, cy - 17, cx + 9 + t, cy + 17, kUiPanel);
            canvas.line(cx - 1 + t, cy - 3, cx - 13 + t, cy + 17, kUiPanel);
        }
        canvas.fill(cx - 12, cy - 17, 10, 3, kUiPanel);
        // ... and a tick at the middle of each side.
        canvas.fill(cx - 8, r.y + 1, 16, 2, kUiOrange);
        canvas.fill(cx - 8, r.y + r.h - 3, 16, 2, kUiOrange);
        canvas.fill(r.x + 1, cy - 8, 2, 16, kUiOrange);
        canvas.fill(r.x + r.w - 3, cy - 8, 2, 16, kUiOrange);
    }

    HL_ARM void BottomScreen::drawColumn()
    {
        Canvas &canvas = touchCanvas.canvas();
        const TouchLayout at = touchLayout(mirrored_, netGame.playing());
        const FontView &font = touchCanvas.font(kFontSmall);
        const struct
        {
            const TouchRect &r;
            const char *name;
            bool up, lit;
        } buttons[] = {{at.jump, "JUMP", true, held_ == kTouchJump}, {at.duck, "DUCK", false, held_ == kTouchDuck}};
        for (const auto &b : buttons)
        {
            plate(b.r, b.lit);
            const int cx = b.r.x + b.r.w / 2;
            chevron(cx, b.r.y + (b.up ? 15 : b.r.h - 15), b.up, b.lit ? kUiWhite + 2 : kUiOrange + 1);
            drawTextCentred(canvas, font, cx, b.up ? b.r.y + b.r.h - font.height() - 3 : b.r.y + 3, b.name,
                            inkFor(font, b.lit ? kUiWhite : kUiOrange));
        }
        // Between them the lamp: a bulb with its rays, lit while it is
        // alight, and under it what is left in it. Faint with no suit to
        // have one.
        {
            const TouchRect &r = at.lamp;
            const bool lit = lampOn_ || held_ == kTouchLamp;
            plate(r, lit, !lampSuit_);
            const int colour = !lampSuit_ ? kUiGrey : (lit ? kUiWhite + 2 : kUiOrange + 1);
            const int cx = r.x + 13, cy = r.y + 12;
            ring(cx, cy, 5, lampOn_ ? 5 : 2, colour);
            for (int k = 0; k < 3; ++k)
            {
                // (Three rays, out to the side the gauge's numbers are.)
                const int dy = (k - 1) * 5;
                canvas.line(cx + 8, cy + dy, cx + 13, cy + dy + (k - 1) * 2, lampOn_ ? colour : (lampSuit_ ? kUiOrange : kUiGrey));
            }
            if (lampSuit_)
            {
                char text[8];
                std::snprintf(text, sizeof(text), "%d", lampBar_ < 0 ? 0 : lampBar_ * 100 / (r.w - 10));
                drawText(canvas, font, r.x + r.w - 5 - textWidth(font, text), r.y + 12 - font.height() / 2, text,
                         inkFor(font, lit ? kUiWhite : kUiOrange));
                // The gauge.
                const int wide = r.w - 10, left = r.x + 5, top = r.y + r.h - 9;
                canvas.fill(left, top, wide, 4, lit ? kUiBack : kUiTrack);
                canvas.fill(left, top, lampBar_ < 0 ? 0 : lampBar_, 4, lampBar_ * 5 < wide ? kUiRed : kUiOrange + 2);
            }
        }
    }

    void BottomScreen::setLamp(bool suit, bool on, int percent)
    {
        const int bar = suit ? percent * (touchLayout(false).lamp.w - 10) / 100 : -1;
        if (suit == lampSuit_ && on == lampOn_ && bar == lampBar_)
            return;
        lampSuit_ = suit;
        lampOn_ = on;
        lampBar_ = bar;
        if (shown_ == Shown::Pad)
            drawColumn();
    }

    HL_ARM void BottomScreen::drawSlots()
    {
        // (What a slot says with no picture to show: by weapon.)
        static const char *const kNames[kWeaponCount] = {"",     "BAR",  "9MM",   "SHOT", "GREN", "MP5",
                                                         ".357", "XBOW", "SATCH", "MINE", "SNARK",
                                                         "RPG",  "TAU",  "EGON",  "HIVE"};
        Canvas &canvas = touchCanvas.canvas();
        const TouchLayout at = touchLayout(mirrored_, netGame.playing());
        const FontView &font = touchCanvas.font(kFontSmall);
        loadIcons();
        for (int n = 0; n < kTouchSlots; ++n)
        {
            // The game's slot n + 1, a kind of weapon: shown as the one of
            // them in hand, or else the first of them carried.
            const TouchRect &r = at.slot[n];
            int count = 0;
            const std::uint8_t *list = weaponsOfCategory(n + 1, count);
            int weapon = kWeaponNone;
            for (int k = 0; k < count && weapon == kWeaponNone; ++k)
            {
                if (list[k] == weapon_ && (have_ & (1u << list[k])))
                    weapon = list[k];
            }
            for (int k = 0; k < count && weapon == kWeaponNone; ++k)
            {
                if (have_ & (1u << list[k]))
                    weapon = list[k];
            }
            const bool carried = weapon != kWeaponNone, inHand = carried && weapon == weapon_;
            plate(r, inHand, !carried);
            if (!carried)
                continue;
            const int cx = r.x + r.w / 2;
            const int top = r.y + (inHand && weapon != kWeaponCrowbar ? 3 : (r.h - icons.height()) / 2);
            const int icon = weapon - 1;
            if (icon < icons.count())
            {
                // The game's picture: dim while it waits, bright in hand.
                const int inks[4] = {0, inHand ? kUiOrange + 1 : kUiOrange, inHand ? kUiOrange + 2 : kUiOrange + 1,
                                     inHand ? kUiWhite + 2 : kUiOrange + 1};
                const int left = cx - icons.width() / 2;
                for (int y = 0; y < icons.height(); ++y)
                {
                    for (int x = 0; x < icons.width(); ++x)
                    {
                        if (const int level = icons.level(icon, x, y))
                            canvas.pixel(left + x, top + y, inks[level]);
                    }
                }
            }
            else
            {
                drawTextCentred(canvas, font, cx, r.y + (inHand && weapon != kWeaponCrowbar ? 3 : (r.h - font.height()) / 2),
                                kNames[weapon], inkFor(font, inHand ? kUiWhite : kUiOrange));
            }
            if (!inHand || weapon == kWeaponCrowbar)
                continue;
            // Under it, what is in it and what is carried for it -- or as
            // much of that as the slot is wide. (What is thrown or put down
            // is a count of itself.)
            char text[16];
            std::snprintf(text, sizeof(text), "%d/%d", clip_, spare_);
            const bool counted = weapon == kWeaponGrenade || weapon == kWeaponSatchel || weapon == kWeaponTripmine ||
                                 weapon == kWeaponSnark || weapon == kWeaponGauss || weapon == kWeaponEgon ||
                                 weapon == kWeaponHornetgun;
            if (counted || textWidth(font, text) > r.w - 6)
                std::snprintf(text, sizeof(text), "%d", clip_);
            drawTextCentred(canvas, font, cx, r.y + r.h - font.height() - 1, text, inkFor(font, kUiWhite));
        }
    }

    void BottomScreen::setWeaponList(const WeaponList &list)
    {
        const int hover = list.owned ? list.hover : -1;
        if (list.category == list_.category && list.slide == list_.slide && hover == listHover_ &&
            list.count == list_.count)
            return;
        list_ = list;
        listHover_ = hover;
        if (shown_ != Shown::Pad)
            return;
        // What was under it (it is all inside the pad's frame), then it where
        // it is now -- off the screen, from the pad as it is on it: put on
        // it whole, in the next vertical blank (present).
        copyPad(padTiles, touchCanvas.screenTiles(), mirrored_);
        touchCanvas.offScreen(padTiles);
        drawPad();
        if (list_.open())
            drawWeaponList();
        touchCanvas.onScreen();
        padPending_ = true;
    }

    void BottomScreen::present()
    {
        if (!padPending_)
            return;
        padPending_ = false;
        if (shown_ == Shown::Pad)
            copyPad(touchCanvas.screenTiles(), padTiles, mirrored_);
    }

    /// The list of a slot's weapons: each its picture and its name, the one
    /// in hand lit, the one under the stylus pressed, one with nothing to
    /// fire grey; risen out of the pad's bottom edge as far as it has slid.
    HL_ARM void BottomScreen::drawWeaponList()
    {
        static const char *const kFull[kWeaponCount] = {"",         "CROWBAR",  "9MM PISTOL", "SHOTGUN",  "GRENADES",
                                                        "MP5",      ".357",     "CROSSBOW",   "SATCHELS", "TRIPMINES",
                                                        "SNARKS",   "RPG",      "TAU CANNON", "GLUON GUN", "HIVEHAND"};
        const TouchLayout at = touchLayout(mirrored_, netGame.playing());
        const TouchRect r = weaponListRect(list_, at);
        const int drop = (256 - list_.slide) * (r.h + 4) / 256;
        // (Drawn into the screen's rows down to the pad's bottom edge, and no
        // further -- what is still below it is not seen, and nothing outside
        // the pad is ever touched: the pad drawn again puts all of it right.)
        Canvas band = touchCanvas.canvas();
        band.height = weaponListFloor(at) / 8 * 8;
        const FontView &font = touchCanvas.font(kFontSmall);
        loadIcons();
        const int top = r.y + drop;
        band.plate(r.x, top, r.w, r.h, 4, kUiBack, kUiOrange + 2);
        for (int n = 0; n < list_.count; ++n)
        {
            const int weapon = list_.weapons[n];
            const int y = top + 2 + n * kWeaponListRow;
            const bool inHand = weapon == weapon_, under = n == listHover_;
            const bool dry = (list_.empty & (1u << weapon)) != 0;
            band.button(r.x + 3, y + 1, r.w - 6, kWeaponListRow - 2, under);
            if (inHand)
                band.frame(r.x + 3, y + 1, r.w - 6, kWeaponListRow - 2, kUiOrange + 2);
            const int icon = weapon - 1;
            if (icon < icons.count())
            {
                const int base = dry ? kUiGrey : kUiOrange;
                const int inks[4] = {0, base, base + 1, dry ? kUiGrey + 2 : kUiWhite + 2};
                const int left = r.x + 6, iconTop = y + (kWeaponListRow - icons.height()) / 2;
                for (int py = 0; py < icons.height(); ++py)
                {
                    for (int px = 0; px < icons.width(); ++px)
                    {
                        if (const int level = icons.level(icon, px, py))
                            band.pixel(left + px, iconTop + py, inks[level]);
                    }
                }
            }
            drawText(band, font, r.x + 50, y + (kWeaponListRow - font.height()) / 2, kFull[weapon],
                     inkFor(font, dry ? kUiGrey : (inHand || under ? kUiWhite : kUiOrange)));
        }
    }

    void BottomScreen::showPad()
    {
        create();
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        touchCanvas.untintAll(); // (a menu's colours of its own go)
        shown_ = Shown::Pad;
        padPending_ = false; // (drawn on the screen now, all of it)
        mirrored_ = settings.leftHanded;
        drawBar();
        drawPad();
        drawColumn();
        drawSlots();
        if (list_.open())
            drawWeaponList();
    }

    void BottomScreen::setStatus(int weapon, int clip, int spare, unsigned have, unsigned empty, bool canReload)
    {
        const bool bar = canReload != canReload_ || netGame.playing() != matchBar_;
        const bool slots = weapon != weapon_ || clip != clip_ || spare != spare_ || have != have_;
        empty_ = empty;
        canReload_ = canReload;
        weapon_ = weapon;
        clip_ = clip;
        spare_ = spare;
        have_ = have;
        if (shown_ != Shown::Pad)
            return;
        if (bar)
            drawBar();
        if (slots)
            drawSlots();
    }

    void BottomScreen::setPressed(int held)
    {
        if (held == held_)
            return;
        const int was = held_;
        held_ = held;
        if (shown_ != Shown::Pad)
            return;
        // (A slot shows what is in hand, not what is touched.)
        auto onBar = [](int on)
        { return on == kTouchMenu || on == kTouchUse || on == kTouchReload || on == kTouchSpray; };
        if (onBar(was) || onBar(held))
            drawBar();
        auto onColumn = [](int on) { return on == kTouchJump || on == kTouchDuck || on == kTouchLamp; };
        if (onColumn(was) || onColumn(held))
            drawColumn();
    }

    void BottomScreen::showMessage(const char *line1, const char *line2)
    {
        create();
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        shown_ = Shown::Message;
        padPending_ = false;
        const FontView &title = touchCanvas.font(kFontTitle), &font = touchCanvas.font(kFontMedium);
        drawTextCentred(canvas, title, 128, 84, line1, inkFor(title, kUiOrange));
        // (As much of the second line as the screen is wide.)
        char cut[64];
        std::snprintf(cut, sizeof(cut), "%.*s", textFit(font, line2, 248), line2);
        drawTextCentred(canvas, font, 128, 112, cut, inkFor(font, kUiWhite));
    }

    void BottomScreen::showFatal(const char *line1, const char *line2, const char *line3)
    {
        create();
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        shown_ = Shown::Nothing;
        padPending_ = false;
        const FontView &title = touchCanvas.font(kFontTitle), &font = touchCanvas.font(kFontSmall);
        drawTextCentred(canvas, title, 128, 20, "Half-Life DS", inkFor(title, kUiOrange));
        int y = 64;
        for (const char *line : {line1, line2, line3})
        {
            // Each on as many rows as it takes.
            while (line != nullptr && *line != '\0' && y < 180)
            {
                const int fits = textFit(font, line, 244);
                drawText(canvas, font, 6, y, line, inkFor(font, kUiWhite), fits);
                line += fits;
                while (*line == ' ')
                    ++line;
                y += font.height();
            }
            y += 6;
        }
    }

    void BottomScreen::readout(int row, const char *text)
    {
        if (!settings.debugReadout || shown_ == Shown::Nothing || row < 0 || row > 8)
            return;
        Canvas &canvas = touchCanvas.canvas();
        canvas.fill(0, row * 8, 256, 8, kUiBack);
        drawText(canvas, touchCanvas.plain(), 0, row * 8, text, inkPlain(kUiWhite + 2), 32);
    }
} // namespace hlrt
