#include "global_header.hpp"

#include "ui/Menu.hpp"

#include "HlCommands.hpp"
#include "HlSkill.hpp"
#include "Achievements.hpp"
#include "Settings.hpp"
#include "audio/DspAudio.hpp"
#include "game/Input.hpp"
#include "audio/Sound.hpp"
#include "net/NetGame.hpp"
#include "render/BottomScreen.hpp"
#include "ui/TouchCanvas.hpp"

namespace hlrt
{
    Menu menu;

    namespace
    {
        using namespace hldata;

        enum class Kind
        {
            Button,
            Value,  ///< shows a setting; a press changes it
            Slider,
        };
        enum Id
        {
            kResume,
            kSave,
            kSaveGame,
            kLoadGame,
            kContinue,
            kYes,
            kNo,
            kSlot, ///< a line of the list (which, by its row)
            kNewGame,
            kOptions,
            kCommand,
            kDsp,
            kMusic,
            kEffects,
            kVoices,
            kAmbience,
            kReadout,
            kHand,
            kControls,
            kScheme,
            kPadSide,
            kPadUp,
            kMultiplayer,
            kNetHost,
            kNetJoin,
            kNetMap,
            kNetFrags,
            kNetMinutes,
            kNetSkill,
            kNetStart,
            kNetFound, ///< a game found (which, by its row)
            kLeave,
            kBob,
            kBack,
            kGraphics,
            kFog,
            kFogStrength,
            kFogDistance,
            kFogColour,
            kModelDetail,
            kWorldDetail,
            kModelLimit,
            kDrawDistance,
            kDuck,
            kButtons,
            kTouchAim,
            kAimSpeed,
            kPitchSpeed,
            kInvert,
            kFirmness,
            kFirmPress,
            kKey,     ///< an action's button (which, by its row: buttonActions)
            kMore,    ///< the buttons' other screen
            kResetKeys,
            kAutoaim,
            kSkillEasy, ///< a new game's difficulty (kSkillEasy + its level - 1)
            kSkillMedium,
            kSkillHard,
            kAchievementsOpen,
        };
        struct Row
        {
            Kind kind;
            Id id;
            const char *label;
        };
        const Row kMain[] = {{Kind::Button, kResume, "RESUME"},       {Kind::Button, kSave, "QUICK SAVE"},
                             {Kind::Button, kSaveGame, "SAVE GAME"}, {Kind::Button, kLoadGame, "LOAD GAME"},
                             {Kind::Button, kNewGame, "NEW GAME"},   {Kind::Button, kOptions, "OPTIONS"},
                             {Kind::Button, kAchievementsOpen, "ACHIEVEMENTS"},
                             {Kind::Button, kCommand, "COMMAND"}};
        const Row kTitleRows[] = {{Kind::Button, kContinue, "CONTINUE"},     {Kind::Button, kLoadGame, "LOAD GAME"},
                                  {Kind::Button, kNewGame, "NEW GAME"},       {Kind::Button, kMultiplayer, "MULTIPLAYER"},
                                  {Kind::Button, kOptions, "OPTIONS"},
                                  {Kind::Button, kAchievementsOpen, "ACHIEVEMENTS"}};
        // A game between consoles (source/net/NetGame.hpp).
        const Row kMultiRows[] = {{Kind::Button, kNetHost, "HOST GAME"}, {Kind::Button, kNetJoin, "JOIN GAME"},
                                  {Kind::Button, kBack, "BACK"}};
        const Row kHostRows[] = {{Kind::Value, kNetMap, "Map"},
                                 {Kind::Value, kNetFrags, "Frag limit"},
                                 {Kind::Value, kNetMinutes, "Time limit"},
                                 {Kind::Value, kNetSkill, "Difficulty"},
                                 {Kind::Button, kNetStart, "START"},
                                 {Kind::Button, kBack, "BACK"}};
        const Row kLobbyRows[] = {{Kind::Button, kBack, "LEAVE"}};
        const Row kNetPauseRows[] = {{Kind::Button, kResume, "RESUME"}, {Kind::Button, kOptions, "OPTIONS"},
                                     {Kind::Button, kLeave, "LEAVE GAME"}};
        // The games found: made when they are shown, then BACK.
        constexpr int kFoundListed = 4;
        Row foundRows[kFoundListed + 1];
        NetGame::Found foundList[kFoundListed];
        int foundCount = 0;
        const Row kConfirmRows[] = {{Kind::Button, kYes, "YES"}, {Kind::Button, kNo, "NO"}};
        // A new game's difficulty, as the game's NEW GAME asks it.
        const Row kSkillRows[] = {{Kind::Button, kSkillEasy, "EASY"},
                                  {Kind::Button, kSkillMedium, "MEDIUM"},
                                  {Kind::Button, kSkillHard, "DIFFICULT"},
                                  {Kind::Button, kBack, "BACK"}};
        // The list of saves: its lines are made when it is shown (they say
        // what is in each), then BACK.
        Row listRows[kSlotsListed + 1];
        char listText[kSlotsListed][72];
        bool listDead[kSlotsListed];
        char question[40];
        const Row kOptionRows[] = {{Kind::Button, kGraphics, "GRAPHICS"}, {Kind::Value, kDsp, "DSi sound"},
                                   {Kind::Slider, kMusic, "Music"},     {Kind::Slider, kEffects, "Effects"},
                                   {Kind::Slider, kVoices, "Voices"},   {Kind::Slider, kAmbience, "Ambience"},
                                   {Kind::Button, kControls, "CONTROLS"}, {Kind::Value, kBob, "View bob"},
                                   {Kind::Value, kReadout, "Debug readout"},
                                   {Kind::Button, kBack, "BACK"}};

        // How the picture is drawn:
        // the fog, and how much detail is given up for speed (shared/HlFog.hpp).
        const Row kGraphicsRows[] = {{Kind::Value, kFog, "Fog"},
                                     {Kind::Slider, kFogStrength, "Fog strength"},
                                     {Kind::Slider, kFogDistance, "Fog distance"},
                                     {Kind::Value, kFogColour, "Fog colour"},
                                     {Kind::Value, kModelDetail, "Model detail"},
                                     {Kind::Value, kWorldDetail, "World detail"},
                                     {Kind::Value, kModelLimit, "Models at once"},
                                     {Kind::Value, kDrawDistance, "Draw distance"},
                                     {Kind::Button, kBack, "BACK"}};

        // The buttons: which layout, which hand, and which of the pad and
        // the stylus turns and which moves (shared/HlControls.hpp). Under
        // the rows, what each button then does.
        const Row kControlRows[] = {{Kind::Value, kScheme, "Layout"},
                                    {Kind::Value, kHand, "Stylus hand"},
                                    {Kind::Value, kDuck, "Crouch"},
                                    {Kind::Value, kPadSide, "Pad left/right"},
                                    {Kind::Value, kPadUp, "Pad up/down"},
                                    {Kind::Value, kAutoaim, "Autoaim"},
                                    {Kind::Button, kButtons, "BUTTONS"},
                                    {Kind::Button, kTouchAim, "TOUCH AIM"},
                                    {Kind::Button, kBack, "BACK"}};
        // How the stylus turns the view, and how hard it must be pressed
        // (where the console can tell).
        const Row kTouchRows[] = {{Kind::Slider, kAimSpeed, "Aim speed"},
                                  {Kind::Slider, kPitchSpeed, "Up/down"},
                                  {Kind::Value, kInvert, "Invert up/down"},
                                  {Kind::Slider, kFirmness, "Firmness"},
                                  {Kind::Value, kFirmPress, "Firm press"},
                                  {Kind::Button, kBack, "BACK"}};
        // Which button does what: an action a row, on two screens; a row
        // touched asks for its button.
        constexpr int kKeysFirst = 7; ///< actions on the first screen
        Row keyRows[kActionCount + 3];
        int keyActions[kActionCount + 3];
        int keyRowCount = 0;
        const char *const kFirmName[kFirmActions] = {"off", "fire", "use", "jump", "alt fire"};

        // Where the rows are: the first page's are big and few, the
        // others' as many as the screen holds.
        struct Layout
        {
            int x, top, width, height, pitch;
        };
        constexpr Layout kMainLayout = {28, 32, 200, 18, 20};
        constexpr Layout kOptionLayout = {8, 2, 240, 17, 19};
        constexpr int kTrackLeft = 100, kTrackWidth = 132;

        /// What a slider slides, and between what (0..10, but the aim's
        /// speeds: 1..20).
        std::uint8_t &slid(Id id)
        {
            switch (id)
            {
            case kAimSpeed:    return settings.aimSpeed;
            case kPitchSpeed:  return settings.pitchSpeed;
            case kFirmness:    return settings.firmness;
            case kFogStrength: return settings.graphics.fogStrength;
            case kFogDistance: return settings.graphics.fogDistance;
            case kMusic:       return settings.volume[kVolumeMusic];
            case kEffects:     return settings.volume[kVolumeEffects];
            case kVoices:      return settings.volume[kVolumeVoices];
            default:           return settings.volume[kVolumeAmbience];
            }
        }
        int slidLeast(Id id) { return id == kAimSpeed || id == kPitchSpeed ? 1 : 0; }
        int slidMost(Id id) { return id == kAimSpeed || id == kPitchSpeed ? kAimSpeedMost : kVolumeMost; }
        bool isVolume(Id id) { return id == kMusic || id == kEffects || id == kVoices || id == kAmbience; }
        const Row *rowsOf(int page, int &count)
        {
            if (page == 15)
            {
                // (The achievements' page has no rows: Menu::drawAchievements.)
                count = 0;
                return kMain;
            }
            if (page == 14)
            {
                count = static_cast<int>(sizeof(kSkillRows) / sizeof(kSkillRows[0]));
                return kSkillRows;
            }
            if (page == 12)
            {
                count = static_cast<int>(sizeof(kTouchRows) / sizeof(kTouchRows[0]));
                return kTouchRows;
            }
            if (page == 13)
            {
                count = keyRowCount;
                return keyRows;
            }
            if (page == 0)
            {
                count = static_cast<int>(sizeof(kMain) / sizeof(kMain[0]));
                return kMain;
            }
            if (page == 3)
            {
                count = static_cast<int>(sizeof(kTitleRows) / sizeof(kTitleRows[0]));
                return kTitleRows;
            }
            if (page == 4)
            {
                count = kSlotsListed + 1;
                return listRows;
            }
            if (page == 5)
            {
                count = 2;
                return kConfirmRows;
            }
            if (page == 6)
            {
                count = static_cast<int>(sizeof(kControlRows) / sizeof(kControlRows[0]));
                return kControlRows;
            }
            if (page == 7)
            {
                count = static_cast<int>(sizeof(kMultiRows) / sizeof(kMultiRows[0]));
                return kMultiRows;
            }
            if (page == 8)
            {
                count = static_cast<int>(sizeof(kHostRows) / sizeof(kHostRows[0]));
                return kHostRows;
            }
            if (page == 9)
            {
                count = foundCount + 1;
                return foundRows;
            }
            if (page == 10)
            {
                count = static_cast<int>(sizeof(kLobbyRows) / sizeof(kLobbyRows[0]));
                return kLobbyRows;
            }
            if (page == 11)
            {
                count = static_cast<int>(sizeof(kNetPauseRows) / sizeof(kNetPauseRows[0]));
                return kNetPauseRows;
            }
            if (page == 1)
            {
                count = static_cast<int>(sizeof(kOptionRows) / sizeof(kOptionRows[0]));
                return kOptionRows;
            }
            count = static_cast<int>(sizeof(kGraphicsRows) / sizeof(kGraphicsRows[0]));
            return kGraphicsRows;
        }
    } // namespace

    int Menu::rowCount() const
    {
        int count = 0;
        rowsOf(static_cast<int>(page_), count);
        return count;
    }

    void Menu::openTitle()
    {
        open();
        title_ = true;
        page_ = Page::Title;
        draw();
    }

    /// The list, to load from or to save into: a line for each save, saying
    /// what it is and what is in it.
    void Menu::showSaves(bool saving)
    {
        static const char *const kNames[kSlotsListed] = {"AUTO", "QUICK", "1", "2", "3", "4", "5", "6"};
        saving_ = saving;
        const FontView &font = touchCanvas.font(kFontSmall);
        for (int i = 0; i < kSlotsListed; ++i)
        {
            const SaveSlot none;
            const SaveSlot &slot = saves_ != nullptr ? saves_->slots[i] : none;
            // What it is, the chapter (as much of its title as there is
            // room for), and when: the day and the time if the console's
            // clock said, else how long had been played.
            std::string line = std::string(kNames[i]) + "  " + saveLine(slot);
            if (slot.status == kSaveGood)
            {
                const SaveHeading &h = slot.heading;
                char when[24];
                if (h.year != 0)
                    std::snprintf(when, sizeof(when), "  %02u/%02u %02u:%02u", h.day, h.month, h.hour, h.minute);
                else
                    std::snprintf(when, sizeof(when), "  %u:%02u", static_cast<unsigned>(h.playSeconds / 3600),
                                  static_cast<unsigned>(h.playSeconds / 60 % 60));
                std::string what = h.chapter[0] != 0 ? h.chapter : h.map;
                const std::string head = std::string(kNames[i]) + "  ";
                while (what.size() > 3 && textWidth(font, (head + what + when).c_str()) > kOptionLayout.width - 8)
                    what.pop_back();
                line = head + what + when;
            }
            while (line.size() > 4 && textWidth(font, line.c_str()) > kOptionLayout.width - 8)
                line.pop_back();
            std::snprintf(listText[i], sizeof(listText[i]), "%s", line.c_str());
            // To save into: one of the player's six. To load: one that is
            // there and whole.
            listDead[i] = saving ? i < kSlotManual : slot.status != kSaveGood;
            listRows[i] = Row{Kind::Button, kSlot, listText[i]};
        }
        listRows[kSlotsListed] = Row{Kind::Button, kBack, "BACK"};
        asker_ = page_ == Page::Saves ? asker_ : page_;
        page_ = Page::Saves;
        draw();
    }

    void Menu::ask(Action action, const char *text)
    {
        asked_ = action;
        std::snprintf(question, sizeof(question), "%s", text);
        page_ = Page::Confirm;
        draw();
    }

    int netlinkFailStage();

    bool Menu::big() const
    {
        return page_ == Page::Main || page_ == Page::Title || page_ == Page::Confirm || page_ == Page::Multiplayer ||
               page_ == Page::NetPause || page_ == Page::Difficulty;
    }

    /// What of the lobby is on the screen: who is there, what they are
    /// called, how they stand -- to draw it again when it is not so.
    unsigned Menu::netSignature() const
    {
        const NetMatch &match = netGame.match();
        unsigned sum = static_cast<unsigned>(netGame.phase()) * 7919u;
        for (int p = 0; p < kNetPlayers; ++p)
        {
            sum = sum * 31u + (netHere(match, p) ? 1u : 0u);
            for (const char *c = match.names[p]; *c != 0; ++c)
                sum = sum * 31u + static_cast<unsigned char>(*c);
            sum = sum * 31u + static_cast<unsigned>(match.frags[p] + 1000) + static_cast<unsigned>(match.deaths[p]) * 131u;
        }
        return sum;
    }

    /// Who is in the game, from `top` down: their names, and with `scores`
    /// their frags and deaths.
    void Menu::drawPlayers(int top, bool scores)
    {
        Canvas &canvas = touchCanvas.canvas();
        const FontView &small = touchCanvas.font(kFontSmall);
        const NetMatch &match = netGame.match();
        canvas.fill(0, top, 256, 4 * (small.height() + 3) + 2, kUiBack);
        int line = 0;
        for (int p = 0; p < kNetPlayers; ++p)
        {
            if (!netHere(match, p))
                continue;
            char text[48];
            const char *name = match.names[p][0] != 0 ? match.names[p] : "...";
            if (scores)
                std::snprintf(text, sizeof(text), "%s%s   %d frags   %d deaths", name, p == match.me ? " (you)" : "",
                              match.frags[p], match.deaths[p]);
            else
                std::snprintf(text, sizeof(text), "%s%s%s", name, p == 0 ? " (host)" : "", p == match.me ? " (you)" : "");
            drawTextCentred(canvas, small, 128, top + line++ * (small.height() + 3), text,
                            inkFor(small, p == match.me ? kUiWhite : kUiGrey));
        }
        netShown_ = netSignature();
    }

    /// The games found, as rows; then BACK.
    void Menu::showFound()
    {
        foundCount = netGame.found(foundList, kFoundListed);
        for (int i = 0; i < foundCount; ++i)
            foundRows[i] = Row{Kind::Button, kNetFound, foundList[i].line};
        foundRows[foundCount] = Row{Kind::Button, kBack, "BACK"};
        page_ = Page::NetJoin;
        draw();
    }

    /// The buttons page: screen 0 the first actions, then MORE and BACK;
    /// screen 1 the rest, RESET and BACK.
    void Menu::showButtons(int screen)
    {
        buttonsScreen_ = screen;
        capturing_ = -1;
        keyRowCount = 0;
        const int first = screen == 0 ? 0 : kKeysFirst, last = screen == 0 ? kKeysFirst : kActionCount;
        for (int action = first; action < last; ++action)
        {
            keyActions[keyRowCount] = action;
            keyRows[keyRowCount++] = Row{Kind::Value, kKey, actionName(action)};
        }
        keyActions[keyRowCount] = -1;
        keyRows[keyRowCount++] = Row{Kind::Button, screen == 0 ? kMore : kResetKeys,
                                     screen == 0 ? "MORE" : "RESET TO MODERN"};
        if (screen == 1)
        {
            keyActions[keyRowCount] = -1;
            keyRows[keyRowCount++] = Row{Kind::Button, kMore, "BACK TO FIRST"};
        }
        keyActions[keyRowCount] = -1;
        keyRows[keyRowCount++] = Row{Kind::Button, kBack, "DONE"};
        page_ = Page::Buttons;
        draw();
    }

    void Menu::open()
    {
        bottomScreen.release();
        title_ = false;
        page_ = netGame.playing() ? Page::NetPause : Page::Main;
        active_ = -1;
        // (The touch that opened it, if there was one, is not a press.)
        touching_ = (keysHeld() & KEY_TOUCH) != 0;
        draw();
    }

    void Menu::draw()
    {
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        if (big())
        {
            const FontView &title = touchCanvas.font(page_ == Page::Confirm ? kFontMedium : kFontTitle);
            drawTextCentred(canvas, title, 128, page_ == Page::Confirm ? 14 : 8,
                            page_ == Page::Main || page_ == Page::NetPause
                                ? "PAUSED"
                                : (page_ == Page::Title       ? "HALF-LIFE"
                                   : page_ == Page::Multiplayer ? "MULTIPLAYER"
                                   : page_ == Page::Difficulty  ? "NEW GAME"
                                                                : question),
                            inkFor(title, kUiOrange));
        }
        for (int row = 0; row < rowCount(); ++row)
            drawRow(row, false);
        if (page_ == Page::NetHost || page_ == Page::NetLobby || page_ == Page::NetJoin)
        {
            const FontView &small = touchCanvas.font(kFontSmall);
            const int top = kOptionLayout.top + rowCount() * kOptionLayout.pitch + 4;
            const char *said = page_ == Page::NetHost
                                   ? "In the game:"
                                   : (page_ == Page::NetJoin
                                          ? (netGame.phase() == NetGame::Phase::Joining ? "Joining..."
                                                                                        : "Looking for games...")
                                          : "Waiting for the host to start");
            drawTextCentred(canvas, small, 128, top, said, inkFor(small, kUiOrange));
            if (page_ != Page::NetJoin)
                drawPlayers(top + small.height() + 5, false);
        }
        if (page_ == Page::Achievements)
            drawAchievements();
        if (page_ == Page::NetPause)
            drawPlayers(kMainLayout.top + rowCount() * kMainLayout.pitch + 6, true);
        if (page_ == Page::Buttons)
        {
            const FontView &small = touchCanvas.font(kFontSmall);
            const int top = kOptionLayout.top + rowCount() * kOptionLayout.pitch + 6;
            drawTextCentred(canvas, small, 128, top,
                            capturing_ >= 0 ? "Press its button (START: cancel)"
                                            : "Touch an action, then press its button",
                            inkFor(small, kUiGrey));
        }
        if (page_ == Page::TouchAim && !Input::pressureSeen())
        {
            const FontView &small = touchCanvas.font(kFontSmall);
            const int top = kOptionLayout.top + rowCount() * kOptionLayout.pitch + 6;
            drawTextCentred(canvas, small, 128, top, "This console does not say how hard",
                            inkFor(small, kUiGrey));
            drawTextCentred(canvas, small, 128, top + small.height() + 3, "the stylus presses: firmness is unused",
                            inkFor(small, kUiGrey));
        }
        if (page_ == Page::Controls)
        {
            // What the buttons do, as they are now set.
            const FontView &small = touchCanvas.font(kFontSmall);
            const int top = kOptionLayout.top + rowCount() * kOptionLayout.pitch + 3;
            const int pitch = small.height() + 1;
            for (int line = 0; controlsLegend(settings.controls(), line) != nullptr &&
                               top + line * pitch + small.height() <= 192;
                 ++line)
                drawTextCentred(canvas, small, 128, top + line * pitch, controlsLegend(settings.controls(), line),
                                inkFor(small, kUiGrey));
        }
    }

    // ------------------------------------------------------- achievements

    namespace
    {
        /// How much of `text` the next line of a column `width` wide takes:
        /// as many whole words as fit.
        int wrapped(const FontView &font, const char *text, int width)
        {
            int fits = textFit(font, text, width);
            if (text[fits] != '\0')
            {
                int space = fits;
                while (space > 0 && text[space] != ' ')
                    --space;
                fits = space > 0 ? space : fits;
            }
            return fits;
        }
        const char *skipSpaces(const char *text)
        {
            while (*text == ' ')
                ++text;
            return text;
        }

        // Where things are: the chapter between its arrows along the top;
        // the list, a band of whole rows of tiles (so that what is drawn
        // in it, scrolled or slid, is cut at its edges for nothing); the
        // box that reads out the row touched; POP-UPS and BACK at the
        // bottom. Every one of them at least 20 high, for a finger.
        constexpr int kAchHeadHeight = 22;
        constexpr int kAchListTop = 24;
        constexpr int kAchBarTop = 171;
        constexpr int kAchBarHeight = 20;
        constexpr int kAchListWidth = 246;
        constexpr int kAchSlop = 6; ///< how far a touch may wander and still be a touch
        constexpr int kAchSwipe = 48; ///< how far the list has to be slid to turn to another chapter
        // What a touch can be on, besides a row (its number).
        constexpr int kAchNothing = -1, kAchLeft = -2, kAchRight = -3, kAchPopups = -4, kAchBack = -5,
                      kAchList = -6;
        struct AchBox
        {
            int x, y, w, h;
            bool has(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
        };
        constexpr AchBox kAchLeftBox = {2, 1, 30, 20}, kAchRightBox = {224, 1, 30, 20};
        constexpr AchBox kAchPopupsBox = {4, kAchBarTop, 148, kAchBarHeight};
        constexpr AchBox kAchBackBox = {156, kAchBarTop, 96, kAchBarHeight};

        /// What depends on the letters' height.
        struct AchGeometry
        {
            int line, row, listHeight, detailTop, detailHeight;
        };
        AchGeometry achGeometry()
        {
            AchGeometry g;
            g.line = touchCanvas.font(kFontSmall).height();
            g.row = g.line + 5 > 20 ? g.line + 5 : 20;
            g.detailHeight = 3 * g.line + 4;
            g.detailTop = kAchBarTop - 2 - g.detailHeight;
            g.listHeight = (g.detailTop - 2 - kAchListTop) / 8 * 8;
            return g;
        }
        int achCount(int chapter)
        {
            return hldata::kAchChapterStart[chapter + 1] - hldata::kAchChapterStart[chapter];
        }
        int achOf(int chapter, int n)
        {
            return hldata::kAchByChapter[hldata::kAchChapterStart[chapter] + n];
        }
        int achWrap(int chapter)
        {
            return (chapter % hldata::kAchChapterCount + hldata::kAchChapterCount) % hldata::kAchChapterCount;
        }
        int achMostScroll(int chapter, const AchGeometry &g)
        {
            const int over = achCount(chapter) * g.row - g.listHeight;
            return over > 0 ? over : 0;
        }
        /// A hidden one says nothing of itself until it is earned.
        const char *achTitle(const hldata::AchievementTracker &tracker, int id)
        {
            return hldata::kAchievements[id].hidden && !tracker.has(id) ? "Hidden achievement"
                                                                         : hldata::kAchievements[id].title;
        }
        const char *achText(const hldata::AchievementTracker &tracker, int id)
        {
            return hldata::kAchievements[id].hidden && !tracker.has(id) ? "Earned somewhere along the way."
                                                                         : hldata::kAchievements[id].text;
        }

        /// The trophy of the top screen's pop-up, small: lit, or not yet.
        void drawTrophy(Canvas &canvas, int x, int y, bool lit)
        {
            static constexpr const char *kCup[12] = {
                "############", "#.########.#", "#.##o#####.#", ".#.#o####.#.", "...#o####...", "....####....",
                ".....##.....", ".....##.....", "....####....", "...######...", "...######...", "............",
            };
            for (int row = 0; row < 12; ++row)
            {
                for (int col = 0; col < 12; ++col)
                {
                    const char c = kCup[row][col];
                    if (c != '.')
                        canvas.pixel(x + col, y + row,
                                     lit ? (c == 'o' ? kUiWhite + 2 : kUiOrange + 2) : (c == 'o' ? kUiGrey : kUiGrey + 1));
                }
            }
        }

        /// A button of the page, with its word in the middle of it.
        void drawAchButton(const AchBox &box, const char *label, bool down)
        {
            Canvas &canvas = touchCanvas.canvas();
            const FontView &medium = touchCanvas.font(kFontMedium), &small = touchCanvas.font(kFontSmall);
            const FontView &font = textWidth(medium, label) <= box.w - 8 ? medium : small;
            canvas.button(box.x, box.y, box.w, box.h, down);
            drawTextCentred(canvas, font, box.x + box.w / 2, box.y + (box.h - font.height()) / 2, label,
                            inkFor(font, down ? kUiWhite : kUiOrange));
        }
    } // namespace

    void Menu::openAchievements()
    {
        page_ = Page::Achievements;
        achScroll_ = 0;
        achSelected_ = -1;
        achSlide_ = achSlideTo_ = 0;
        achPressed_ = kAchNothing;
        achDrag_ = 0;
        // (The touch that opened it is not one here, nor a button still held.)
        achHeld_ = keysHeld();
        achTouching_ = (achHeld_ & KEY_TOUCH) != 0;
        draw();
    }

    void Menu::drawAchievements()
    {
        drawAchHeader();
        drawAchList();
        drawAchDetail();
        drawAchBar();
    }

    /// The chapter, how many of its achievements are earned, and the arrows
    /// either side.
    void Menu::drawAchHeader()
    {
        Canvas &canvas = touchCanvas.canvas();
        const FontView &small = touchCanvas.font(kFontSmall);
        const hldata::AchievementTracker &tracker = achievements.tracker;
        canvas.fill(0, 0, 256, kAchListTop, kUiBack);
        drawAchButton(kAchLeftBox, "<", achPressed_ == kAchLeft);
        drawAchButton(kAchRightBox, ">", achPressed_ == kAchRight);
        int earned = 0;
        for (int n = 0; n < achCount(achChapter_); ++n)
            earned += tracker.has(achOf(achChapter_, n)) ? 1 : 0;
        char count[16];
        std::snprintf(count, sizeof(count), "  %d/%d", earned, achCount(achChapter_));
        const char *name = hldata::kAchChapterNames[achChapter_];
        const int nameWide = textWidth(small, name), wide = nameWide + textWidth(small, count);
        const int x = 128 - wide / 2, y = (kAchHeadHeight - small.height()) / 2 + 1;
        drawText(canvas, small, x, y, name, inkFor(small, kUiOrange));
        drawText(canvas, small, x + nameWide, y, count, inkFor(small, earned == achCount(achChapter_) ? kUiOrange : kUiWhite));
    }

    /// The list: the chapter's rows where the stylus has put them, and,
    /// while it is slid aside, the next or the last chapter's coming in.
    void Menu::drawAchList()
    {
        const AchGeometry g = achGeometry();
        Canvas &screen = touchCanvas.canvas();
        Canvas band = screen;
        band.tiles = screen.tiles + (kAchListTop / 8) * (screen.width / 8) * 8;
        band.height = g.listHeight;
        band.fill(0, 0, 256, g.listHeight, kUiBack);
        drawAchRows(achChapter_, achSlide_, achScroll_);
        if (achSlide_ < 0)
            drawAchRows(achWrap(achChapter_ + 1), achSlide_ + 256, 0);
        else if (achSlide_ > 0)
            drawAchRows(achWrap(achChapter_ - 1), achSlide_ - 256, 0);
        // How far down, when there is more than shows.
        const int most = achMostScroll(achChapter_, g);
        if (most > 0 && achSlide_ == 0)
        {
            const int whole = achCount(achChapter_) * g.row;
            const int tall = g.listHeight * g.listHeight / whole;
            band.fill(251, achScroll_ * (g.listHeight - tall) / most, 3, tall, kUiGrey + 1);
        }
    }

    /// `chapter`'s rows, `x` across and scrolled `scroll` down.
    void Menu::drawAchRows(int chapter, int x, int scroll)
    {
        const AchGeometry g = achGeometry();
        Canvas &screen = touchCanvas.canvas();
        Canvas band = screen;
        band.tiles = screen.tiles + (kAchListTop / 8) * (screen.width / 8) * 8;
        band.height = g.listHeight;
        const FontView &small = touchCanvas.font(kFontSmall);
        const hldata::AchievementTracker &tracker = achievements.tracker;
        const bool current = chapter == achChapter_ && x == achSlide_;
        for (int n = 0; n < achCount(chapter); ++n)
        {
            const int y = n * g.row - scroll;
            if (y + g.row <= 0 || y >= g.listHeight)
                continue;
            const int id = achOf(chapter, n);
            const bool got = tracker.has(id);
            const bool chosen = current && n == achSelected_;
            band.button(x + 2, y + 1, kAchListWidth, g.row - 2, chosen || (current && n == achPressed_ && achDrag_ == 0));
            if (chosen)
                band.frame(x + 2, y + 1, kAchListWidth, g.row - 2, kUiOrange + 2);
            drawTrophy(band, x + 6, y + (g.row - 12) / 2, got);
            char status[16] = "";
            if (got)
                std::snprintf(status, sizeof(status), "earned");
            else if (hldata::kAchievements[id].goal != 0)
                std::snprintf(status, sizeof(status), "%d/%d", tracker.progress(id), hldata::kAchievements[id].goal);
            const int statusWide = textWidth(small, status);
            const int textY = y + (g.row - g.line) / 2;
            const char *title = achTitle(tracker, id);
            drawText(band, small, x + 22, textY, title, inkFor(small, got ? kUiWhite : kUiGrey),
                     textFit(small, title, kAchListWidth - 26 - statusWide));
            drawText(band, small, x + kAchListWidth - 2 - statusWide, textY, status,
                     inkFor(small, got ? kUiOrange : kUiWhite));
        }
    }

    /// The box under the list: what the row touched asks for, in full; or,
    /// with none touched, how many are earned and how to find the rest.
    void Menu::drawAchDetail()
    {
        const AchGeometry g = achGeometry();
        Canvas &canvas = touchCanvas.canvas();
        const FontView &small = touchCanvas.font(kFontSmall);
        const hldata::AchievementTracker &tracker = achievements.tracker;
        canvas.fill(0, g.detailTop - 1, 256, g.detailHeight + 2, kUiBack);
        canvas.frame(2, g.detailTop, 252, g.detailHeight, kUiEdge);
        const char *rest = nullptr;
        Ink ink = inkFor(small, kUiGrey);
        int line = 0;
        if (achSelected_ >= 0 && achSelected_ < achCount(achChapter_))
        {
            const int id = achOf(achChapter_, achSelected_);
            rest = achText(tracker, id);
            ink = inkFor(small, tracker.has(id) ? kUiWhite : kUiGrey);
        }
        else
        {
            char earned[40];
            std::snprintf(earned, sizeof(earned), "%d of %d earned", tracker.earned(),
                          static_cast<int>(hldata::kAchievementCount));
            drawText(canvas, small, 7, g.detailTop + 2, earned, inkFor(small, kUiOrange));
            line = 1;
            rest = "Touch one to read it. Slide the list, or touch < >, for another chapter.";
        }
        for (; line < 3 && *rest != '\0'; ++line)
        {
            const int fits = wrapped(small, rest, 244);
            drawText(canvas, small, 7, g.detailTop + 2 + line * g.line, rest, ink, fits);
            rest = skipSpaces(rest + fits);
        }
    }

    void Menu::drawAchBar()
    {
        touchCanvas.canvas().fill(0, kAchBarTop, 256, 192 - kAchBarTop, kUiBack);
        drawAchButton(kAchPopupsBox, settings.achievementPopups ? "POP-UPS: ON" : "POP-UPS: OFF",
                      achPressed_ == kAchPopups);
        drawAchButton(kAchBackBox, "BACK", achPressed_ == kAchBack);
    }

    int Menu::achHit(int x, int y) const
    {
        if (kAchLeftBox.has(x, y))
            return kAchLeft;
        if (kAchRightBox.has(x, y))
            return kAchRight;
        if (kAchPopupsBox.has(x, y))
            return kAchPopups;
        if (kAchBackBox.has(x, y))
            return kAchBack;
        const AchGeometry g = achGeometry();
        if (y < kAchListTop || y >= kAchListTop + g.listHeight)
            return kAchNothing;
        const int n = (y - kAchListTop + achScroll_) / g.row;
        return n < achCount(achChapter_) && x >= 2 && x < 2 + kAchListWidth ? n : kAchList;
    }

    /// The next chapter (1) or the last (-1), slid in.
    void Menu::achTurn(int step)
    {
        sound.ui(kUiSoundPress);
        achSlideTo_ = step > 0 ? -256 : 256;
    }

    /// Row `row` read out (-1: none), and kept in sight.
    void Menu::achSelect(int row)
    {
        const AchGeometry g = achGeometry();
        achSelected_ = row;
        if (row >= 0)
        {
            if (row * g.row < achScroll_)
                achScroll_ = row * g.row;
            if ((row + 1) * g.row > achScroll_ + g.listHeight)
                achScroll_ = (row + 1) * g.row - g.listHeight;
        }
        const int most = achMostScroll(achChapter_, g);
        achScroll_ = achScroll_ < 0 ? 0 : (achScroll_ > most ? most : achScroll_);
        drawAchList();
        drawAchDetail();
    }

    Menu::Action Menu::achLeave()
    {
        sound.ui(kUiSoundPress);
        page_ = title_ ? Page::Title : (netGame.playing() ? Page::NetPause : Page::Main);
        settings.save();
        active_ = -1;
        touching_ = (keysHeld() & KEY_TOUCH) != 0;
        draw();
        return Action::None;
    }

    Menu::Action Menu::achievementsFrame()
    {
        const AchGeometry g = achGeometry();
        const std::uint32_t held = keysHeld();
        const std::uint32_t pressed = held & ~achHeld_;
        achHeld_ = held;

        // The buttons: L and R (or left and right) another chapter; up and
        // down the row read out; B back.
        if (achDrag_ == 0 && achSlide_ == 0 && achSlideTo_ == 0 && !achTouching_)
        {
            if (pressed & (KEY_R | KEY_RIGHT))
                achTurn(1);
            else if (pressed & (KEY_L | KEY_LEFT))
                achTurn(-1);
            else if (pressed & (KEY_UP | KEY_DOWN))
            {
                const int last = achCount(achChapter_) - 1;
                int row = achSelected_ < 0 ? 0 : achSelected_ + ((pressed & KEY_DOWN) ? 1 : -1);
                row = row < 0 ? 0 : (row > last ? last : row);
                sound.ui(kUiSoundTick);
                achSelect(row);
            }
            else if (pressed & KEY_B)
            {
                return achLeave();
            }
        }

        // A slide let go of (or asked for) finishes by itself: quickly,
        // then slowing; past the edge, the next chapter is the one shown.
        if (achDrag_ != 2 && achSlide_ != achSlideTo_)
        {
            const int left = achSlideTo_ - achSlide_;
            int step = left / 3;
            if (step > -8 && step < 8)
                step = left > 0 ? (left < 8 ? left : 8) : (left > -8 ? left : -8);
            achSlide_ += step;
            if (achSlide_ == achSlideTo_ && achSlideTo_ != 0)
            {
                achChapter_ = achWrap(achChapter_ + (achSlideTo_ < 0 ? 1 : -1));
                achSlide_ = achSlideTo_ = 0;
                achScroll_ = 0;
                achSelected_ = -1;
                drawAchHeader();
                drawAchDetail();
            }
            drawAchList();
        }

        // The stylus.
        const bool down = (held & KEY_TOUCH) != 0;
        if (down)
        {
            touchPosition touch;
            touchRead(&touch);
            achX_ = touch.px;
            achY_ = touch.py;
        }
        if (down && !achTouching_)
        {
            // (While a slide is going on, a touch only waits for it.)
            achPressed_ = achSlide_ == achSlideTo_ ? achHit(achX_, achY_) : kAchNothing;
            achDownX_ = achX_;
            achDownY_ = achY_;
            achScrollAt_ = achScroll_;
            achDrag_ = 0;
            if (achPressed_ == kAchLeft || achPressed_ == kAchRight)
                drawAchHeader();
            else if (achPressed_ == kAchPopups || achPressed_ == kAchBack)
                drawAchBar();
            else if (achPressed_ >= 0)
                drawAchList();
        }
        else if (down && achTouching_)
        {
            const int dx = achX_ - achDownX_, dy = achY_ - achDownY_;
            const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            if (achDrag_ == 0 && (achPressed_ >= 0 || achPressed_ == kAchList) && (ax > kAchSlop || ay > kAchSlop))
            {
                // Moved: no longer a touch of a row, but the list moving.
                achDrag_ = ax > ay ? 2 : 1;
                drawAchList();
            }
            if (achDrag_ == 1)
            {
                const int most = achMostScroll(achChapter_, g);
                int scroll = achScrollAt_ - dy;
                scroll = scroll < 0 ? 0 : (scroll > most ? most : scroll);
                if (scroll != achScroll_)
                {
                    achScroll_ = scroll;
                    drawAchList();
                }
            }
            else if (achDrag_ == 2)
            {
                const int slide = dx < -256 ? -256 : (dx > 256 ? 256 : dx);
                if (slide != achSlide_)
                {
                    achSlide_ = slide;
                    drawAchList();
                }
            }
        }
        else if (!down && achTouching_)
        {
            const int was = achPressed_;
            const int drag = achDrag_;
            achPressed_ = kAchNothing;
            achDrag_ = 0;
            const bool same = drag == 0 && was != kAchNothing && was != kAchList && achHit(achX_, achY_) == was;
            if (drag == 2)
            {
                // Slid far enough (a fifth of the way, a flick): the rest of
                // the way; else back.
                achSlideTo_ = achSlide_ < -kAchSwipe ? -256 : (achSlide_ > kAchSwipe ? 256 : 0);
                if (achSlideTo_ != 0)
                    sound.ui(kUiSoundTick);
            }
            else if (same && was == kAchLeft)
            {
                achTurn(-1);
                drawAchHeader();
            }
            else if (same && was == kAchRight)
            {
                achTurn(1);
                drawAchHeader();
            }
            else if (same && was == kAchPopups)
            {
                sound.ui(kUiSoundPress);
                settings.achievementPopups = !settings.achievementPopups;
                drawAchBar();
            }
            else if (same && was == kAchBack)
            {
                achTouching_ = false;
                return achLeave();
            }
            else if (same && was >= 0)
            {
                sound.ui(kUiSoundPress);
                achSelect(was);
            }
            else
            {
                // (Let go somewhere else: nothing, and nothing pressed.)
                if (was == kAchLeft || was == kAchRight)
                    drawAchHeader();
                else if (was == kAchPopups || was == kAchBack)
                    drawAchBar();
                else
                    drawAchList();
            }
        }
        achTouching_ = down;
        return Action::None;
    }

    void Menu::drawRow(int row, bool down)
    {
        Canvas &canvas = touchCanvas.canvas();
        const bool main = big();
        int count = 0;
        const Row &item = rowsOf(static_cast<int>(page_), count)[row];
        const Layout &at = main ? kMainLayout : kOptionLayout;
        const int y = at.top + row * at.pitch;
        const FontView &font = touchCanvas.font(main ? kFontMedium : kFontSmall);
        const int textY = y + (at.height - font.height()) / 2;
        // (A DS has no DSP to switch: the row is there, and says so. A save
        // that cannot be loaded, or is not the player's to write, likewise;
        // and CONTINUE with nothing to continue.)
        const bool dead = (item.id == kDsp && !DspAudio::available()) || (item.id == kSlot && listDead[row]) ||
                          (item.id == kContinue && (saves_ == nullptr || saveMostRecent(*saves_) < 0)) ||
                          (item.id == kCommand && title_) || (item.id == kMultiplayer && netGame.maps().empty());
        if (item.kind == Kind::Slider)
        {
            canvas.fill(at.x, y, at.width, at.height, kUiBack);
            drawText(canvas, font, at.x + 4, textY, item.label, inkFor(font, kUiWhite));
            const int least = slidLeast(item.id), most = slidMost(item.id);
            const int level = slid(item.id) - least;
            const int knob = kTrackLeft + (kTrackWidth * level) / (most - least);
            canvas.fill(kTrackLeft, y + at.height / 2 - 3, kTrackWidth, 6, kUiTrack);
            canvas.frame(kTrackLeft - 1, y + at.height / 2 - 4, kTrackWidth + 2, 8, kUiEdge);
            canvas.fill(kTrackLeft, y + at.height / 2 - 3, knob - kTrackLeft, 6, kUiOrange + (down ? 2 : 1));
            canvas.fill(knob - 3, y + 2, 7, at.height - 4, down ? kUiWhite + 2 : kUiWhite + 1);
            canvas.frame(knob - 3, y + 2, 7, at.height - 4, kUiShadow);
            return;
        }
        char text[48];
        switch (item.id)
        {
        case kDsp:
            std::snprintf(text, sizeof(text), "%s: %s", item.label,
                          dead ? "not on a DS" : (sound.extended() ? "on" : "off"));
            break;
        case kReadout:
            std::snprintf(text, sizeof(text), "%s: %s", item.label, settings.debugReadout ? "on" : "off");
            break;
        case kBob:
            std::snprintf(text, sizeof(text), "%s: %s", item.label, settings.viewBob ? "on" : "off");
            break;
        case kAutoaim:
            std::snprintf(text, sizeof(text), "%s: %s", item.label, settings.autoaim ? "on" : "off");
            break;
        case kFog:
            std::snprintf(text, sizeof(text), "%s: %s", item.label, kFogTypeName[settings.graphics.fog]);
            break;
        case kFogColour:
            std::snprintf(text, sizeof(text), "%s: %s", item.label, kFogColourName[settings.graphics.fogColour]);
            break;
        case kModelDetail:
            std::snprintf(text, sizeof(text), "%s: %s", item.label, kDetailName[settings.graphics.modelDetail]);
            break;
        case kWorldDetail:
            std::snprintf(text, sizeof(text), "%s: %s", item.label, kDetailName[settings.graphics.worldDetail]);
            break;
        case kModelLimit:
            if (settings.graphics.modelLimit == 0)
                std::snprintf(text, sizeof(text), "%s: auto", item.label);
            else
                std::snprintf(text, sizeof(text), "%s: %d", item.label, settings.graphics.modelLimit);
            break;
        case kDrawDistance:
            std::snprintf(text, sizeof(text), "%s: %s", item.label, kDrawDistanceName[settings.graphics.drawDistance]);
            break;
        case kHand:
            // (Which side of the touch screen its column of buttons is,
            // and which hand has the pad and the trigger.)
            std::snprintf(text, sizeof(text), "%s: %s", item.label, settings.leftHanded ? "left" : "right");
            break;
        case kNetMap:
            std::snprintf(text, sizeof(text), "%s: %s", item.label,
                          netGame.maps().empty() ? "none" : netGame.maps()[netMap_ % netGame.maps().size()].c_str());
            break;
        case kNetFrags:
            if (netFrags_ == 0)
                std::snprintf(text, sizeof(text), "%s: none", item.label);
            else
                std::snprintf(text, sizeof(text), "%s: %d", item.label, netFrags_);
            break;
        case kNetMinutes:
            if (netMinutes_ == 0)
                std::snprintf(text, sizeof(text), "%s: none", item.label);
            else
                std::snprintf(text, sizeof(text), "%s: %d minutes", item.label, netMinutes_);
            break;
        case kNetSkill:
        {
            static const char *const kNames[] = {"easy", "medium", "difficult"};
            std::snprintf(text, sizeof(text), "%s: %s", item.label, kNames[(skill_ - 1) % 3]);
            break;
        }
        case kScheme:
            std::snprintf(text, sizeof(text), "%s: %s", item.label,
                          settings.controlScheme == kControlsModern
                              ? "modern"
                              : (settings.controlScheme == kControlsCustom ? "custom" : "classic"));
            break;
        case kDuck:
            std::snprintf(text, sizeof(text), "%s: %s", item.label, settings.duckToggle ? "toggle" : "hold");
            break;
        case kInvert:
            std::snprintf(text, sizeof(text), "%s: %s", item.label, settings.invertPitch ? "on" : "off");
            break;
        case kFirmPress:
            std::snprintf(text, sizeof(text), "%s: %s", item.label,
                          Input::pressureSeen() ? kFirmName[settings.firmPress % kFirmActions] : "n/a here");
            break;
        case kKey:
        {
            // The action, and its buttons (or the question, while asked).
            const int action = keyActions[row];
            if (capturing_ == action)
            {
                std::snprintf(text, sizeof(text), "%s: press a button", actionName(action));
                break;
            }
            std::uint16_t keys[kActionCount];
            keysOf(settings.controls(), keys);
            char names[24] = "";
            int n = 0;
            for (int bit = 0; bit < 12; ++bit)
            {
                if (keys[action] & (1u << bit))
                    n += std::snprintf(names + n, sizeof(names) - static_cast<std::size_t>(n), "%s%s",
                                       n > 0 ? "/" : "", buttonName(1u << bit));
                if (n >= static_cast<int>(sizeof(names)) - 1)
                    break;
            }
            std::snprintf(text, sizeof(text), "%s: %s", actionName(action), n > 0 ? names : "-");
            break;
        }
        case kPadSide:
            std::snprintf(text, sizeof(text), "%s: %s", item.label, settings.padTurns ? "turn" : "sidestep");
            break;
        case kPadUp:
            std::snprintf(text, sizeof(text), "%s: %s", item.label, settings.padLooks ? "look" : "move");
            break;
        default:
            std::snprintf(text, sizeof(text), "%s", item.label);
            break;
        }
        canvas.button(at.x, y, at.width, at.height, down && !dead);
        drawTextCentred(canvas, font, at.x + at.width / 2, textY, text,
                        inkFor(font, dead ? kUiGrey : (down ? kUiWhite : kUiOrange)));
    }

    int Menu::rowAt(int x, int y) const
    {
        const Layout &at = big() ? kMainLayout : kOptionLayout;
        if (x < at.x || x >= at.x + at.width || y < at.top)
            return -1;
        const int row = (y - at.top) / at.pitch;
        return row < rowCount() && (y - at.top) % at.pitch < at.height ? row : -1;
    }

    /// The slider of `row` put where the stylus is. True if that moved it.
    bool Menu::slide(int row, int x)
    {
        int count = 0;
        const Row &item = rowsOf(static_cast<int>(page_), count)[row];
        const int least = slidLeast(item.id), most = slidMost(item.id);
        int level = ((x - kTrackLeft) * (most - least) + kTrackWidth / 2) / kTrackWidth + least;
        level = x < kTrackLeft ? least : (level > most ? most : level);
        std::uint8_t &value = slid(item.id);
        if (value == level)
            return false;
        value = static_cast<std::uint8_t>(level);
        if (isVolume(item.id))
            sound.applyVolumes();
        sound.ui(kUiSoundTick);
        return true;
    }

    Menu::Action Menu::fire(int row)
    {
        int count = 0;
        const Row &item = rowsOf(static_cast<int>(page_), count)[row];
        sound.ui(kUiSoundPress);
        switch (item.id)
        {
        case kResume:  return Action::Resume;
        case kSave:    return Action::Save;
        case kCommand: return Action::Command;
        case kNewGame:
            // (From the title there is nothing to lose: straight to how hard
            // it is to be. No save is touched either way: a new game only
            // begins.)
            asker_ = page_;
            if (title_)
            {
                page_ = Page::Difficulty;
                draw();
                break;
            }
            ask(Action::NewGame, "NEW GAME?");
            break;
        case kSkillEasy:
        case kSkillMedium:
        case kSkillHard:
            skill_ = hldata::kSkillEasy + (item.id - kSkillEasy);
            return Action::NewGame;
        case kContinue:
            if (saves_ != nullptr && saveMostRecent(*saves_) >= 0)
            {
                slot_ = saveMostRecent(*saves_);
                return Action::Continue;
            }
            break;
        case kSaveGame:
            showSaves(true);
            break;
        case kLoadGame:
            showSaves(false);
            break;
        case kSlot:
            if (listDead[row])
            {
                drawRow(row, false);
                break;
            }
            slot_ = row;
            if (saving_)
            {
                // One with a save in it is asked about first.
                if (saves_ != nullptr && saves_->slots[row].status != kSaveNone)
                    ask(Action::SaveTo, "OVERWRITE?");
                else
                    return Action::SaveTo;
            }
            else if (title_)
            {
                return Action::Load;
            }
            else
            {
                ask(Action::Load, "LOAD? UNSAVED IS LOST");
            }
            break;
        case kYes:
        {
            const Action action = asked_;
            asked_ = Action::None;
            // (A new game: yes, and then how hard.)
            if (action == Action::NewGame)
            {
                page_ = Page::Difficulty;
                draw();
                break;
            }
            return action;
        }
        case kNo:
            page_ = asker_ == Page::Saves || asked_ == Action::SaveTo || asked_ == Action::Load ? Page::Saves : asker_;
            asked_ = Action::None;
            draw();
            break;
        case kOptions:
            page_ = Page::Options;
            draw();
            break;
        case kAchievementsOpen:
            openAchievements();
            break;
        case kBack:
            // Out of a lobby: out of the game, and the radio silent.
            if (page_ == Page::NetHost || page_ == Page::NetJoin || page_ == Page::NetLobby)
            {
                netGame.leave();
                page_ = Page::Multiplayer;
                draw();
                break;
            }
            // (From the graphics, back to the options they are among; from
            // the list, to where it was asked for.)
            capturing_ = -1;
            page_ = page_ == Page::TouchAim || page_ == Page::Buttons
                        ? Page::Controls
                        : page_ == Page::Graphics || page_ == Page::Controls
                        ? Page::Options
                        : (page_ == Page::Saves || page_ == Page::Difficulty
                               ? asker_
                               : (title_ ? Page::Title : (netGame.playing() ? Page::NetPause : Page::Main)));
            settings.save();
            draw();
            break;
        case kMultiplayer:
            if (!netGame.maps().empty())
            {
                page_ = Page::Multiplayer;
                draw();
            }
            break;
        case kNetHost:
            if (netGame.host())
            {
                page_ = Page::NetHost;
                draw();
            }
            else
            {
                std::snprintf(question, sizeof(question), "The wireless would not start (%d)", netlinkFailStage());
                page_ = Page::Multiplayer;
                draw();
                const FontView &small = touchCanvas.font(kFontSmall);
                drawTextCentred(touchCanvas.canvas(), small, 128, 170, question, inkFor(small, kUiGrey));
            }
            break;
        case kNetJoin:
            if (netGame.scan())
                showFound();
            break;
        case kNetFound:
            if (row < foundCount && netGame.join(foundList[row].index))
                draw();
            break;
        case kNetMap:
            netMap_ = netGame.maps().empty() ? 0 : (netMap_ + 1) % static_cast<int>(netGame.maps().size());
            drawRow(row, false);
            break;
        case kNetFrags:
        {
            static const int kLimits[] = {0, 5, 10, 20, 30, 50};
            int at = 0;
            for (int i = 0; i < 6; ++i)
                at = kLimits[i] == netFrags_ ? i : at;
            netFrags_ = kLimits[(at + 1) % 6];
            drawRow(row, false);
            break;
        }
        case kNetMinutes:
        {
            static const int kLimits[] = {0, 5, 10, 15, 20, 30};
            int at = 0;
            for (int i = 0; i < 6; ++i)
                at = kLimits[i] == netMinutes_ ? i : at;
            netMinutes_ = kLimits[(at + 1) % 6];
            drawRow(row, false);
            break;
        }
        case kNetSkill:
            skill_ = skill_ % hldata::kSkillHard + 1;
            drawRow(row, false);
            break;
        case kNetStart:
            // (Whoever runs the screens loads the map when the match says
            // it has been started: main.cpp.)
            // (Alone, too: a host may look round a map before anyone comes --
            // though nobody can then, a match under way lets no one in.)
            if (!netGame.maps().empty())
                netGame.start(netGame.maps()[netMap_ % netGame.maps().size()], netFrags_, netMinutes_, skill_);
            break;
        case kLeave:
            return Action::Leave;
        case kGraphics:
            page_ = Page::Graphics;
            draw();
            break;
        case kFog:
            settings.graphics.fog = static_cast<std::uint8_t>((settings.graphics.fog + 1) % kFogTypes);
            drawRow(row, false);
            break;
        case kFogColour:
            settings.graphics.fogColour = static_cast<std::uint8_t>((settings.graphics.fogColour + 1) % kFogColours);
            drawRow(row, false);
            break;
        case kModelDetail:
            settings.graphics.modelDetail = static_cast<std::uint8_t>((settings.graphics.modelDetail + 1) % kDetailLevels);
            drawRow(row, false);
            break;
        case kWorldDetail:
            settings.graphics.worldDetail = static_cast<std::uint8_t>((settings.graphics.worldDetail + 1) % kDetailLevels);
            drawRow(row, false);
            break;
        case kModelLimit:
            // auto, 4, 6, 8, 10, 12, and round again.
            settings.graphics.modelLimit = settings.graphics.modelLimit == 0
                                               ? 4
                                               : (settings.graphics.modelLimit >= 12 ? 0 : settings.graphics.modelLimit + 2);
            drawRow(row, false);
            break;
        case kDrawDistance:
            settings.graphics.drawDistance = static_cast<std::uint8_t>((settings.graphics.drawDistance + 1) % kDrawDistances);
            drawRow(row, false);
            break;
        case kDsp:
            if (DspAudio::available())
            {
                // At once: the music goes on from where it was, by the
                // other way of playing it.
                settings.dspAudio = sound.setDsp(!sound.extended());
                settings.save();
            }
            drawRow(row, false);
            break;
        case kReadout:
            settings.debugReadout = !settings.debugReadout;
            drawRow(row, false);
            break;
        case kBob:
            settings.viewBob = !settings.viewBob;
            drawRow(row, false);
            break;
        case kAutoaim:
            settings.autoaim = !settings.autoaim;
            drawRow(row, false);
            break;
        case kControls:
            page_ = Page::Controls;
            draw();
            break;
        // (The whole page again: the lines under the rows say what the
        // buttons now do.)
        case kHand:
            settings.leftHanded = !settings.leftHanded;
            draw();
            break;
        case kScheme:
        {
            // (Into CUSTOM with nothing made yet: it starts as the layout
            // it came from.)
            const std::uint8_t next = static_cast<std::uint8_t>((settings.controlScheme + 1) % kControlsSchemes);
            bool none = true;
            for (std::uint16_t key : settings.keys)
                none = none && key == 0;
            if (next == kControlsCustom && none)
                settings.makeCustom();
            settings.controlScheme = next;
            draw();
            break;
        }
        case kPadSide:
            settings.padTurns = !settings.padTurns;
            draw();
            break;
        case kPadUp:
            settings.padLooks = !settings.padLooks;
            draw();
            break;
        case kDuck:
            settings.duckToggle = !settings.duckToggle;
            draw();
            break;
        case kButtons:
            showButtons(0);
            break;
        case kTouchAim:
            page_ = Page::TouchAim;
            draw();
            break;
        case kInvert:
            settings.invertPitch = !settings.invertPitch;
            drawRow(row, false);
            break;
        case kFirmPress:
            if (Input::pressureSeen())
                settings.firmPress = static_cast<std::uint8_t>((settings.firmPress + 1) % kFirmActions);
            drawRow(row, false);
            break;
        case kKey:
        {
            // Asked for: the next button pressed is this action's.
            capturing_ = keyActions[row];
            captureHeld_ = keysHeld() & 0xFFFu;
            draw();
            break;
        }
        case kMore:
            showButtons(buttonsScreen_ ^ 1);
            break;
        case kResetKeys:
            // Back to the layout the custom one was made from: modern.
            capturing_ = -1;
            settings.controlScheme = kControlsModern;
            for (std::uint16_t &key : settings.keys)
                key = 0;
            draw();
            break;
        default:
            break;
        }
        return Action::None;
    }

    Menu::Action Menu::frame()
    {
        if (page_ == Page::Achievements)
            return achievementsFrame();
        // A game between consoles: the lobby changes without being touched.
        if (page_ == Page::NetJoin)
        {
            // Let in: the host's lobby. Not let in, or the host gone: out.
            if (netGame.phase() == NetGame::Phase::Lobby)
            {
                page_ = Page::NetLobby;
                draw();
            }
            else if (netGame.phase() == NetGame::Phase::Off)
            {
                page_ = Page::Multiplayer;
                draw();
            }
            else if (netGame.phase() == NetGame::Phase::Scanning && ++netTick_ >= 45 && active_ < 0)
            {
                netTick_ = 0;
                showFound();
            }
        }
        else if (page_ == Page::NetHost || page_ == Page::NetLobby || page_ == Page::NetPause)
        {
            if (page_ != Page::NetPause && netGame.phase() == NetGame::Phase::Off)
            {
                page_ = Page::Multiplayer;
                draw();
            }
            else if (netSignature() != netShown_ && active_ < 0)
                draw();
        }
        // A button asked for: the next one pressed (START never minds).
        if (capturing_ >= 0)
        {
            const std::uint32_t held = keysHeld() & 0xFFFu;
            const std::uint32_t pressed = held & ~captureHeld_;
            captureHeld_ = held;
            if (pressed & KEY_START)
            {
                capturing_ = -1;
                draw();
            }
            else if (pressed & kAssignable)
            {
                std::uint32_t button = 1;
                while (!(pressed & kAssignable & button))
                    button <<= 1;
                settings.makeCustom();
                assignKey(settings.keys, capturing_, button);
                capturing_ = -1;
                sound.ui(kUiSoundTick);
                draw();
            }
        }
        const bool down = (keysHeld() & KEY_TOUCH) != 0;
        if (down)
        {
            touchPosition touch;
            touchRead(&touch);
            lastX_ = touch.px;
            lastY_ = touch.py;
            Input::notePressure(touch.z1, touch.z2);
        }
        int rowTotal = 0;
        const Row *rows = rowsOf(static_cast<int>(page_), rowTotal);
        const bool sliding = active_ >= 0 && active_ < rowTotal && rows[active_].kind == Kind::Slider;
        Action action = Action::None;
        if (down && !touching_)
        {
            active_ = rowAt(lastX_, lastY_);
            if (active_ >= 0)
            {
                if (rows[active_].kind == Kind::Slider)
                    slide(active_, lastX_);
                drawRow(active_, true);
            }
        }
        else if (down && sliding)
        {
            if (slide(active_, lastX_))
                drawRow(active_, true);
        }
        else if (!down && touching_ && active_ >= 0)
        {
            // Lifted: a button acts if it was lifted from where it was
            // pressed.
            const int row = active_;
            active_ = -1;
            if (sliding)
                drawRow(row, false);
            else if (rowAt(lastX_, lastY_) == row)
                action = fire(row);
            else
                drawRow(row, false);
        }
        touching_ = down;
        return action;
    }

    void Menu::console(ConsoleRun run)
    {
        static const char *const kKeys[4] = {"1234567890", "qwertyuiop", "asdfghjkl_", "zxcvbnm-.<"};
        // What the console has said, kept while the game runs (as the
        // game's own is): closed and opened again, it is all still there.
        static ConsoleLog log;
        if (log.count() == 0)
            log.print("help: the commands\n<command> help: how to use one");
        // The lines typed before, most recent last: up and down on the pad
        // bring them back.
        constexpr int kRemembered = 8;
        constexpr std::size_t kLongest = 32;
        static std::string earlier[kRemembered];
        static int earlierCount = 0;
        std::string typed;
        int recalled = earlierCount;
        auto remember = [&]()
        {
            if (typed.empty() || (earlierCount > 0 && earlier[earlierCount - 1] == typed))
                return;
            if (earlierCount == kRemembered)
            {
                for (int i = 1; i < kRemembered; ++i)
                    earlier[i - 1] = earlier[i];
                --earlierCount;
            }
            earlier[earlierCount++] = typed;
        };
        // The screen, from the top: what was said (kShown lines of the plain
        // letters), the line being typed, the keys, and SPACE / ENTER /
        // CANCEL, small, along the bottom.
        constexpr int kShown = 7, kLineStep = 9, kLogTop = 0;
        constexpr int kPromptTop = 64, kPromptHigh = 16;
        constexpr int kKey = 24, kLeft = 8, kTop = 82;
        constexpr int kBottom = 180, kWideHigh = 12;
        bottomScreen.release();
        Canvas &canvas = touchCanvas.canvas();
        const FontView &font = touchCanvas.font(kFontMedium), &small = touchCanvas.font(kFontSmall),
                       &plain = touchCanvas.plain();
        int scrolled = 0; // lines back from the newest
        auto said = [&]()
        {
            canvas.fill(0, kLogTop, 256, kShown * kLineStep, kUiBack);
            const int last = log.count() - scrolled;
            for (int n = 0; n < kShown; ++n)
            {
                const int at = last - kShown + n;
                if (at >= 0)
                    drawText(canvas, plain, 0, kLogTop + n * kLineStep, log.line(at), inkFor(plain, kUiGrey));
            }
            if (scrolled > 0)
                drawText(canvas, plain, 248, kLogTop, "^", inkFor(plain, kUiOrange));
        };
        auto prompt = [&]()
        {
            canvas.fill(0, kPromptTop, 256, kPromptHigh, kUiBack);
            canvas.frame(6, kPromptTop, 244, kPromptHigh, kUiEdge);
            // (A long line: its end, which is where the typing is.)
            char line[48];
            const std::size_t shown = 27;
            const char *from = typed.size() > shown ? typed.c_str() + (typed.size() - shown) : typed.c_str();
            std::snprintf(line, sizeof(line), "%s%s_", typed.size() > shown ? "<" : "] ", from);
            drawText(canvas, plain, 12, kPromptTop + (kPromptHigh - plain.height()) / 2, line,
                     inkFor(plain, kUiWhite));
        };
        auto key = [&](int row, int col, bool down)
        {
            const char text[2] = {kKeys[row][col], '\0'};
            if (text[0] == ' ')
                return;
            canvas.button(kLeft + col * kKey, kTop + row * kKey, kKey - 1, kKey - 1, down);
            drawTextCentred(canvas, font, kLeft + col * kKey + kKey / 2, kTop + row * kKey + (kKey - font.height()) / 2,
                            text, inkFor(font, down ? kUiWhite : kUiOrange));
        };
        auto wide = [&](int n, bool down)
        {
            static const char *const kNames[3] = {"SPACE", "ENTER", "CANCEL"};
            canvas.button(kLeft + n * 80, kBottom, 79, kWideHigh, down);
            drawTextCentred(canvas, small, kLeft + n * 80 + 40, kBottom + (kWideHigh - small.height()) / 2, kNames[n],
                            inkFor(small, down ? kUiWhite : kUiOrange));
        };
        canvas.clear();
        said();
        prompt();
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 10; ++col)
                key(row, col, false);
        }
        for (int n = 0; n < 3; ++n)
            wide(n, false);
        // The line given: said above, run, and what it said said too.
        // Whether the console is to close now.
        auto enter = [&]() -> bool
        {
            remember();
            recalled = earlierCount;
            const std::string line = typed;
            typed.clear();
            scrolled = 0;
            if (line.empty())
            {
                prompt();
                return false;
            }
            log.print(("] " + line).c_str());
            const ConsoleReply reply = run(line);
            if (!reply.text.empty())
                log.print(reply.text.c_str());
            if (reply.close)
                return true;
            // (What the line did may have had the touch screen -- a map
            // that did not load, a save: taken back, and drawn again whole.)
            bottomScreen.release();
            canvas.clear();
            said();
            prompt();
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 10; ++col)
                    key(row, col, false);
            }
            for (int n = 0; n < 3; ++n)
                wide(n, false);
            return false;
        };

        // The touch that asked for this is still down, and may be on a key
        // now: not until it has lifted is anything typed.
        bool touching = true;
        int heldRow = -1, heldCol = -1, heldWide = -1;
        while (true)
        {
            swiWaitForVBlank();
            sound.frame();
            scanKeys();
            const std::uint32_t pressed = keysDown();
            if (pressed & (KEY_B | KEY_START))
                return;
            if ((pressed & KEY_A) && enter())
                return;
            if ((pressed & (KEY_UP | KEY_DOWN)) && earlierCount > 0)
            {
                recalled += (pressed & KEY_UP) ? -1 : 1;
                recalled = recalled < 0 ? 0 : (recalled > earlierCount ? earlierCount : recalled);
                typed = recalled < earlierCount ? earlier[recalled] : std::string();
                sound.ui(kUiSoundTick);
                prompt();
            }
            if (pressed & (KEY_L | KEY_R))
            {
                // A page back (L) or on (R) through what was said.
                const int most = log.count() > kShown ? log.count() - kShown : 0;
                scrolled += (pressed & KEY_L) ? kShown - 1 : -(kShown - 1);
                scrolled = scrolled < 0 ? 0 : (scrolled > most ? most : scrolled);
                said();
            }
            const bool down = (keysHeld() & KEY_TOUCH) != 0;
            if (down && !touching)
            {
                touchPosition touch;
                touchRead(&touch);
                const int col = (touch.px - kLeft) / kKey, row = (touch.py - kTop) / kKey;
                if (touch.py >= kBottom && touch.py < kBottom + kWideHigh && touch.px >= kLeft && touch.px < kLeft + 240)
                {
                    const int n = (touch.px - kLeft) / 80;
                    sound.ui(kUiSoundPress);
                    if (n == 1 && enter())
                        return;
                    if (n == 2)
                        return;
                    if (n == 0 && typed.size() < kLongest)
                        typed.push_back(' ');
                    heldWide = n;
                    wide(n, true);
                    prompt();
                }
                else if (touch.px >= kLeft && touch.py >= kTop && col < 10 && row < 4 && kKeys[row][col] != ' ')
                {
                    const char c = kKeys[row][col];
                    sound.ui(kUiSoundTick);
                    if (c == '<')
                    {
                        if (!typed.empty())
                            typed.pop_back();
                    }
                    else if (typed.size() < kLongest)
                    {
                        typed.push_back(c);
                    }
                    heldRow = row;
                    heldCol = col;
                    key(row, col, true);
                    prompt();
                }
            }
            else if (!down && heldRow >= 0)
            {
                key(heldRow, heldCol, false);
                heldRow = heldCol = -1;
            }
            else if (!down && heldWide >= 0)
            {
                wide(heldWide, false);
                heldWide = -1;
            }
            touching = down;
        }
    }
} // namespace hlrt
