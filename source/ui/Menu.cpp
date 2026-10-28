#include "global_header.hpp"

#include "ui/Menu.hpp"

#include "HlCommands.hpp"
#include "HlSkill.hpp"
#include "Achievements.hpp"
#include "Settings.hpp"
#include "audio/DspAudio.hpp"
#include "game/Input.hpp"
#include "audio/Sound.hpp"
#include "data/LevelFile.hpp"
#include "game/SaveGame.hpp"
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
            kDsp, ///< the room effects
            kOutput,
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
            kPopups,       ///< the achievements' pop-ups (a setting)
            kGameHalfLife, ///< NEW GAME's: the game itself
            kGameTraining, ///< ... the Hazard Course
            kGameUplink,   ///< ... Half-Life: Uplink
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
        // Which game: the game itself, its Hazard Course, or Uplink (each
        // only if it is on the card).
        const Row kNewGameRows[] = {{Kind::Button, kGameHalfLife, "HALF-LIFE"},
                                    {Kind::Button, kGameTraining, "HAZARD COURSE"},
                                    {Kind::Button, kGameUplink, "UPLINK"},
                                    {Kind::Button, kBack, "BACK"}};
        char question[40];
        const char *const kFirmName[kFirmActions] = {"off", "fire", "use", "jump", "alt fire"};

        // Where the rows are: the first page's are big and few, the
        // others' as many as the screen holds.
        struct Layout
        {
            int x, top, width, height, pitch;
        };
        constexpr Layout kMainLayout = {28, 32, 200, 18, 20};
        constexpr Layout kOptionLayout = {8, 2, 240, 17, 19};

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
            // (A sheet -- the settings, the saves, the achievements -- has
            // no rows of these: Menu::drawSheet.)
            count = 0;
            switch (page)
            {
            case 0: count = static_cast<int>(sizeof(kMain) / sizeof(kMain[0])); return kMain;
            case 3: count = static_cast<int>(sizeof(kTitleRows) / sizeof(kTitleRows[0])); return kTitleRows;
            case 5: count = 2; return kConfirmRows;
            case 7: count = static_cast<int>(sizeof(kMultiRows) / sizeof(kMultiRows[0])); return kMultiRows;
            case 8: count = static_cast<int>(sizeof(kHostRows) / sizeof(kHostRows[0])); return kHostRows;
            case 9: count = foundCount + 1; return foundRows;
            case 10: count = static_cast<int>(sizeof(kLobbyRows) / sizeof(kLobbyRows[0])); return kLobbyRows;
            case 11: count = static_cast<int>(sizeof(kNetPauseRows) / sizeof(kNetPauseRows[0])); return kNetPauseRows;
            case 14: count = static_cast<int>(sizeof(kSkillRows) / sizeof(kSkillRows[0])); return kSkillRows;
            case 16: count = static_cast<int>(sizeof(kNewGameRows) / sizeof(kNewGameRows[0])); return kNewGameRows;
            default: return kMain;
            }
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

    /// Which game: the game, its Hazard Course, Uplink -- each one there
    /// only if its first map is on the card.
    void Menu::openNewGame()
    {
        static const char *const kFirst[3] = {nullptr, "t0a0", "hldemo1"};
        for (int i = 1; i < 3; ++i)
        {
            hldata::LevelHeader header{};
            gameThere_[i] = Level::peekHeader(kFirst[i], header);
        }
        page_ = Page::NewGame;
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
               page_ == Page::NetPause || page_ == Page::Difficulty || page_ == Page::NewGame;
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
        if (sheet())
        {
            drawSheet();
            return;
        }
        if (big())
        {
            const FontView &title = touchCanvas.font(page_ == Page::Confirm ? kFontMedium : kFontTitle);
            drawTextCentred(canvas, title, 128, page_ == Page::Confirm ? 14 : 8,
                            page_ == Page::Main || page_ == Page::NetPause
                                ? "PAUSED"
                                : (page_ == Page::Title       ? "HALF-LIFE"
                                   : page_ == Page::Multiplayer ? "MULTIPLAYER"
                                   : page_ == Page::Difficulty  ? "DIFFICULTY"
                                   : page_ == Page::NewGame     ? "NEW GAME"
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
        if (page_ == Page::NetPause)
            drawPlayers(kMainLayout.top + rowCount() * kMainLayout.pitch + 6, true);
    }

    // -------------------------------------------------------------- sheets

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
        /// `count` characters of `text` (all of it if negative) at (x, y), in
        /// no more than `width`: what does not fit is left off, and "..."
        /// says so. Where it ends.
        int drawFitted(Canvas &canvas, const FontView &font, int x, int y, const char *text, const Ink &ink, int width,
                       int count = -1)
        {
            if (textWidth(font, text, count) <= width)
                return drawText(canvas, font, x, y, text, ink, count);
            int fits = textFit(font, text, width - textWidth(font, "..."));
            while (fits > 0 && text[fits - 1] == ' ')
                --fits;
            const int end = drawText(canvas, font, x, y, text, ink, fits);
            return drawText(canvas, font, end, y, "...", ink);
        }
        /// `text` in the lines of a box from `line` to `last` (of `pitch`),
        /// a word at a time; on the last, what does not fit is left off and
        /// said to be.
        void drawWrapped(Canvas &canvas, const FontView &font, int x, int y, int pitch, int line, int last,
                         const char *text, const Ink &ink, int width)
        {
            for (; line <= last && *text != '\0'; ++line)
            {
                if (line == last)
                {
                    drawFitted(canvas, font, x, y + line * pitch, text, ink, width);
                    return;
                }
                const int fits = wrapped(font, text, width);
                drawText(canvas, font, x, y + line * pitch, text, ink, fits);
                text = skipSpaces(text + fits);
            }
        }

        // Where things are: the chapter between its arrows along the top;
        // the list, a band of whole rows of tiles (so that what is drawn
        // in it, scrolled or slid, is cut at its edges for nothing); the
        // box that reads out the row touched; the buttons at the bottom.
        // Every one of them at least 20 high, for a finger.
        constexpr int kSheetHeadHeight = 22;
        constexpr int kSheetListTop = 24;
        constexpr int kSheetBarTop = 171;
        constexpr int kSheetBarHeight = 20;
        constexpr int kSheetListWidth = 246;
        constexpr int kSheetSlop = 6;   ///< how far a touch may wander and still be a touch
        constexpr int kSheetSwipe = 48; ///< how far the list has to be slid to turn to another chapter
        // A slider's track in its row, from the row's left.
        constexpr int kSheetTrackLeft = 112, kSheetTrackWidth = 124;
        // What a touch can be on, besides a row (its number).
        constexpr int kSheetNothing = -1, kSheetLeft = -2, kSheetRight = -3, kSheetList = -6, kSheetYes = -20,
                      kSheetNo = -21;
        constexpr int kSheetBar = -10; ///< ... and the buttons along the bottom: kSheetBar - n
        struct SheetBox
        {
            int x, y, w, h;
            bool has(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
        };
        constexpr SheetBox kSheetLeftBox = {2, 1, 30, 20}, kSheetRightBox = {224, 1, 30, 20};
        constexpr SheetBox kSheetYesBox = {148, kSheetBarTop, 50, kSheetBarHeight};
        constexpr SheetBox kSheetNoBox = {202, kSheetBarTop, 50, kSheetBarHeight};
        /// The `n`th of `count` buttons along the bottom.
        SheetBox sheetBarBox(int n, int count)
        {
            if (count == 2)
                return n == 0 ? SheetBox{4, kSheetBarTop, 148, kSheetBarHeight} : SheetBox{156, kSheetBarTop, 96, kSheetBarHeight};
            return SheetBox{4 + n * 84, kSheetBarTop, 80, kSheetBarHeight};
        }
        // What YES does, once asked.
        constexpr int kAskReset = 1, kAskOverwrite = 2, kAskLoad = 3, kAskDelete = 4;

        /// What depends on the letters' height.
        struct SheetGeometry
        {
            int line, row, listHeight, detailTop, detailHeight;
        };
        SheetGeometry sheetGeometry()
        {
            SheetGeometry g;
            g.line = touchCanvas.font(kFontSmall).height();
            g.row = g.line + 5 > 20 ? g.line + 5 : 20;
            g.detailHeight = 4 * g.line + 4;
            g.detailTop = kSheetBarTop - 2 - g.detailHeight;
            g.listHeight = (g.detailTop - 2 - kSheetListTop) / 8 * 8;
            return g;
        }
        /// The list's band of the touch screen, as a canvas of its own.
        Canvas sheetBand(const SheetGeometry &g)
        {
            Canvas &screen = touchCanvas.canvas();
            Canvas band = screen;
            band.tiles = screen.tiles + (kSheetListTop / 8) * (screen.width / 8) * 8;
            band.height = g.listHeight;
            return band;
        }

        /// A button of a sheet, with its word in the middle of it (grey if
        /// it does nothing now).
        void drawSheetButton(const SheetBox &box, const char *label, bool down, bool dead = false)
        {
            Canvas &canvas = touchCanvas.canvas();
            const FontView &medium = touchCanvas.font(kFontMedium), &small = touchCanvas.font(kFontSmall);
            const FontView &font = textWidth(medium, label) <= box.w - 8 ? medium : small;
            canvas.button(box.x, box.y, box.w, box.h, down && !dead);
            drawTextCentred(canvas, font, box.x + box.w / 2, box.y + (box.h - font.height()) / 2, label,
                            inkFor(font, dead ? kUiGrey : (down ? kUiWhite : kUiOrange)));
        }

        // --- The achievements.

        int achCount(int chapter)
        {
            return hldata::kAchChapterStart[chapter + 1] - hldata::kAchChapterStart[chapter];
        }
        int achOf(int chapter, int n)
        {
            const int id = hldata::kAchByChapter[hldata::kAchChapterStart[chapter] + n];
            return id < hldata::kAchievementCount ? id : 0;
        }
        /// A hidden one says nothing of itself until it is earned.
        const char *achTitle(const hldata::AchievementTracker &tracker, int id)
        {
            return hldata::kAchievements[id].hidden && !tracker.has(id) ? "Hidden achievement"
                                                                         : hldata::kAchievements[id].title;
        }
        /// What a row says at its end: earned, how far along, or nothing.
        void achStatus(const hldata::AchievementTracker &tracker, int id, char *out, std::size_t size)
        {
            if (tracker.has(id))
                std::snprintf(out, size, "earned");
            else if (hldata::kAchievements[id].goal != 0)
                std::snprintf(out, size, "%d/%d", tracker.progress(id), hldata::kAchievements[id].goal);
            else
                out[0] = '\0';
        }
        /// How wide a row's title may be, beside that.
        int achTitleRoom(const FontView &font, const char *status)
        {
            return kSheetListWidth - 28 - textWidth(font, status);
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

        // --- The settings: a category a page, each a list of rows.

        constexpr int kSettingsChapters = 6;
        const char *const kSettingsChapterNames[kSettingsChapters] = {"SOUND",     "GRAPHICS", "CONTROLS",
                                                                      "TOUCH AIM", "BUTTONS",  "OTHER"};
        const Row kSoundRows[] = {{Kind::Value, kDsp, "Room effects"},  {Kind::Value, kOutput, "Output"},
                                  {Kind::Slider, kMusic, "Music"},       {Kind::Slider, kEffects, "Effects"},
                                  {Kind::Slider, kVoices, "Voices"},     {Kind::Slider, kAmbience, "Ambience"}};
        const char *const kRoomName[hldata::kRoomChoices] = {"off", "classic", "enhanced"};
        const char *const kOutputName[hldata::kOutputChoices] = {"speakers", "headphones", "mono"};
        const Row kGraphicsRows[] = {{Kind::Value, kFog, "Fog"},
                                     {Kind::Slider, kFogStrength, "Fog strength"},
                                     {Kind::Slider, kFogDistance, "Fog distance"},
                                     {Kind::Value, kFogColour, "Fog colour"},
                                     {Kind::Value, kModelDetail, "Model detail"},
                                     {Kind::Value, kWorldDetail, "World detail"},
                                     {Kind::Value, kModelLimit, "Models at once"},
                                     {Kind::Value, kDrawDistance, "Draw distance"}};
        const Row kControlRows[] = {{Kind::Value, kScheme, "Layout"},          {Kind::Value, kHand, "Stylus hand"},
                                    {Kind::Value, kDuck, "Crouch"},            {Kind::Value, kPadSide, "Pad left/right"},
                                    {Kind::Value, kPadUp, "Pad up/down"},      {Kind::Value, kAutoaim, "Autoaim"}};
        const Row kTouchRows[] = {{Kind::Slider, kAimSpeed, "Aim speed"},
                                  {Kind::Slider, kPitchSpeed, "Up/down speed"},
                                  {Kind::Value, kInvert, "Invert up/down"},
                                  {Kind::Slider, kFirmness, "Firmness"},
                                  {Kind::Value, kFirmPress, "Firm press"}};
        const Row kOtherRows[] = {{Kind::Value, kBob, "View bob"},
                                  {Kind::Value, kReadout, "Debug readout"},
                                  {Kind::Value, kPopups, "Achievement pop-ups"}};
        /// Category `chapter`'s rows (BUTTONS: one an action, made here).
        const Row *settingsRows(int chapter, int &count)
        {
            static Row keyRows[kActionCount];
            switch (chapter)
            {
            case 0: count = static_cast<int>(sizeof(kSoundRows) / sizeof(kSoundRows[0])); return kSoundRows;
            case 1: count = static_cast<int>(sizeof(kGraphicsRows) / sizeof(kGraphicsRows[0])); return kGraphicsRows;
            case 2: count = static_cast<int>(sizeof(kControlRows) / sizeof(kControlRows[0])); return kControlRows;
            case 3: count = static_cast<int>(sizeof(kTouchRows) / sizeof(kTouchRows[0])); return kTouchRows;
            case 4:
                for (int a = 0; a < kActionCount; ++a)
                    keyRows[a] = Row{Kind::Value, kKey, actionName(a)};
                count = kActionCount;
                return keyRows;
            default: count = static_cast<int>(sizeof(kOtherRows) / sizeof(kOtherRows[0])); return kOtherRows;
            }
        }
        /// What a setting is now, as its row says it.
        void settingValue(Id id, int action, char *out, std::size_t size)
        {
            switch (id)
            {
            case kDsp: std::snprintf(out, size, "%s", kRoomName[settings.roomEffects]); break;
            case kOutput: std::snprintf(out, size, "%s", kOutputName[settings.outputMode]); break;
            case kReadout: std::snprintf(out, size, "%s", settings.debugReadout ? "on" : "off"); break;
            case kBob: std::snprintf(out, size, "%s", settings.viewBob ? "on" : "off"); break;
            case kAutoaim: std::snprintf(out, size, "%s", settings.autoaim ? "on" : "off"); break;
            case kPopups: std::snprintf(out, size, "%s", settings.achievementPopups ? "on" : "off"); break;
            case kFog: std::snprintf(out, size, "%s", kFogTypeName[settings.graphics.fog]); break;
            case kFogColour: std::snprintf(out, size, "%s", kFogColourName[settings.graphics.fogColour]); break;
            case kModelDetail: std::snprintf(out, size, "%s", kDetailName[settings.graphics.modelDetail]); break;
            case kWorldDetail: std::snprintf(out, size, "%s", kDetailName[settings.graphics.worldDetail]); break;
            case kModelLimit:
                if (settings.graphics.modelLimit == 0)
                    std::snprintf(out, size, "auto");
                else
                    std::snprintf(out, size, "%d", settings.graphics.modelLimit);
                break;
            case kDrawDistance: std::snprintf(out, size, "%s", kDrawDistanceName[settings.graphics.drawDistance]); break;
            // (Which side of the touch screen its column of buttons is, and
            // which hand has the pad and the trigger.)
            case kHand: std::snprintf(out, size, "%s", settings.leftHanded ? "left" : "right"); break;
            case kScheme:
                std::snprintf(out, size, "%s",
                              settings.controlScheme == kControlsModern
                                  ? "modern"
                                  : (settings.controlScheme == kControlsCustom ? "custom" : "classic"));
                break;
            case kDuck: std::snprintf(out, size, "%s", settings.duckToggle ? "toggle" : "hold"); break;
            case kInvert: std::snprintf(out, size, "%s", settings.invertPitch ? "on" : "off"); break;
            case kFirmPress:
                std::snprintf(out, size, "%s", Input::pressureSeen() ? kFirmName[settings.firmPress % kFirmActions] : "n/a here");
                break;
            case kPadSide: std::snprintf(out, size, "%s", settings.padTurns ? "turn" : "sidestep"); break;
            case kPadUp: std::snprintf(out, size, "%s", settings.padLooks ? "look" : "move"); break;
            case kKey:
            {
                // Its buttons (or the question, while asked).
                std::uint16_t keys[kActionCount];
                keysOf(settings.controls(), keys);
                int n = 0;
                out[0] = '\0';
                for (int bit = 0; bit < 12 && n < static_cast<int>(size) - 1; ++bit)
                {
                    if (keys[action] & (1u << bit))
                        n += std::snprintf(out + n, size - static_cast<std::size_t>(n), "%s%s", n > 0 ? "/" : "",
                                           buttonName(1u << bit));
                }
                if (n == 0)
                    std::snprintf(out, size, "-");
                break;
            }
            default: out[0] = '\0'; break;
            }
        }
        /// What a setting does, for the box under the list.
        const char *settingHelp(Id id)
        {
            switch (id)
            {
            case kDsp:
                return DspAudio::available()
                           ? "The rooms heard: off, as the game has them, or richer from the DSi's sound processor."
                           : "The rooms heard -- a tunnel, a chamber, under water -- as the game has them, or off.";
            case kOutput: return "Speakers: a little narrower, for the console's own. Headphones: all of it. Mono: one.";
            case kMusic: return "How loud the music is.";
            case kEffects: return "How loud the guns, the blows and the world are.";
            case kVoices: return "How loud the people and the monsters speak.";
            case kAmbience: return "How loud the hum and drip of a place is.";
            case kFog: return "Fog, where the view ends: off, only where the map is too big to draw whole, or always.";
            case kFogStrength: return "How thick the fog is.";
            case kFogDistance: return "How near the fog begins.";
            case kFogColour: return "The colour the far distance fades into.";
            case kModelDetail: return "How much of the people and monsters is drawn. Lower is faster.";
            case kWorldDetail: return "How much of the walls is drawn far off. Lower is faster.";
            case kModelLimit: return "How many people and monsters are drawn at once: auto, or a number.";
            case kDrawDistance: return "How far the world is drawn. Nearer is faster.";
            case kScheme: return "Which button does what: classic, modern, or your own (BUTTONS).";
            case kHand: return "Which hand holds the stylus: the touch screen's buttons go to the other side.";
            case kDuck: return "Crouch held down, or pressed once to crouch and again to stand.";
            case kPadSide: return "What the pad's left and right do: step aside, or turn.";
            case kPadUp: return "What the pad's up and down do: walk, or look up and down.";
            case kAutoaim: return "The aim bends towards a monster near it, on EASY and MEDIUM, as the game's does.";
            case kAimSpeed: return "How far the view turns across for a stroke of the stylus.";
            case kPitchSpeed: return "How far the view turns up and down for a stroke of the stylus.";
            case kInvert: return "The stylus moved down looks up.";
            case kFirmness: return "How light a touch of the stylus is ignored.";
            case kFirmPress: return "What pressing the stylus hard does, where the console can tell.";
            case kBob: return "The view bobs with a walk and leans into a step aside, as the game's does.";
            case kReadout: return "Numbers along the top of the touch screen: how fast, where, how much.";
            case kPopups: return "An achievement earned is shown on the top screen as it is. Off, it is still earned.";
            case kKey: return "Touch an action, then press its button. START: leave it as it was.";
            default: return "";
            }
        }
        /// A setting changed by a step (a value to its next or last, a
        /// slider a notch). True if it changed.
        bool settingStep(Id id, int step)
        {
            auto cycle = [&](std::uint8_t &value, int count)
            {
                value = static_cast<std::uint8_t>((value + count + step) % count);
            };
            switch (id)
            {
            case kDsp:
            {
                // (Enhanced only where there is a DSP to give it.) At once:
                // the music goes on from where it was.
                const int choices = DspAudio::available() ? hldata::kRoomChoices : hldata::kRoomEnhanced;
                const int at = settings.roomEffects < choices ? settings.roomEffects : hldata::kRoomClassic;
                settings.roomEffects = static_cast<std::uint8_t>((at + choices + step) % choices);
                sound.applySoundMode();
                return true;
            }
            case kOutput:
                cycle(settings.outputMode, hldata::kOutputChoices);
                sound.applySoundMode();
                return true;
            case kReadout: settings.debugReadout = !settings.debugReadout; return true;
            case kBob: settings.viewBob = !settings.viewBob; return true;
            case kAutoaim: settings.autoaim = !settings.autoaim; return true;
            case kPopups: settings.achievementPopups = !settings.achievementPopups; return true;
            case kFog: cycle(settings.graphics.fog, kFogTypes); return true;
            case kFogColour: cycle(settings.graphics.fogColour, kFogColours); return true;
            case kModelDetail: cycle(settings.graphics.modelDetail, kDetailLevels); return true;
            case kWorldDetail: cycle(settings.graphics.worldDetail, kDetailLevels); return true;
            case kDrawDistance: cycle(settings.graphics.drawDistance, kDrawDistances); return true;
            case kModelLimit:
            {
                // auto, 4, 6, 8, 10, 12, and round again.
                static const std::uint8_t kLimits[] = {0, 4, 6, 8, 10, 12};
                int at = 0;
                for (int i = 0; i < 6; ++i)
                    at = kLimits[i] == settings.graphics.modelLimit ? i : at;
                settings.graphics.modelLimit = kLimits[(at + 6 + step) % 6];
                return true;
            }
            case kHand: settings.leftHanded = !settings.leftHanded; return true;
            case kScheme:
            {
                // (Into CUSTOM with nothing made yet: it starts as the layout
                // it came from.)
                const std::uint8_t next = static_cast<std::uint8_t>((settings.controlScheme + kControlsSchemes + step) % kControlsSchemes);
                bool none = true;
                for (std::uint16_t key : settings.keys)
                    none = none && key == 0;
                if (next == kControlsCustom && none)
                    settings.makeCustom();
                settings.controlScheme = next;
                return true;
            }
            case kPadSide: settings.padTurns = !settings.padTurns; return true;
            case kPadUp: settings.padLooks = !settings.padLooks; return true;
            case kDuck: settings.duckToggle = !settings.duckToggle; return true;
            case kInvert: settings.invertPitch = !settings.invertPitch; return true;
            case kFirmPress:
                if (!Input::pressureSeen())
                    return false;
                cycle(settings.firmPress, kFirmActions);
                return true;
            case kMusic:
            case kEffects:
            case kVoices:
            case kAmbience:
            case kFogStrength:
            case kFogDistance:
            case kAimSpeed:
            case kPitchSpeed:
            case kFirmness:
            {
                std::uint8_t &value = slid(id);
                const int level = value + step;
                if (level < slidLeast(id) || level > slidMost(id))
                    return false;
                value = static_cast<std::uint8_t>(level);
                if (isVolume(id))
                    sound.applyVolumes();
                return true;
            }
            default:
                return false;
            }
        }

        // --- The saves.

        /// What a save is called in the list.
        void saveName(int slot, char *out, std::size_t size)
        {
            if (slot == kSlotAuto)
                std::snprintf(out, size, "AUTO");
            else if (slot == kSlotQuick)
                std::snprintf(out, size, "QUICK");
            else
                std::snprintf(out, size, "SAVE %u", static_cast<unsigned>(slot - kSlotManual + 1) % 100u);
        }
        /// When it was made: the day and the time if the console's clock
        /// said, else how long had been played.
        void saveWhen(const SaveHeading &h, char *out, std::size_t size)
        {
            if (h.year != 0)
                std::snprintf(out, size, "%02u/%02u %02u:%02u", h.day, h.month, h.hour, h.minute);
            else
                std::snprintf(out, size, "%u:%02u", static_cast<unsigned>(h.playSeconds / 3600),
                              static_cast<unsigned>(h.playSeconds / 60 % 60));
        }
    } // namespace

    void Menu::openSheet(Page page)
    {
        page_ = page;
        sheetScroll_ = 0;
        sheetSelected_ = -1;
        sheetSlide_ = sheetSlideTo_ = 0;
        sheetPressed_ = kSheetNothing;
        sheetDrag_ = 0;
        sheetAsking_ = 0;
        capturing_ = -1;
        if (page == Page::Saves)
            listSaves();
        // (The touch that opened it is not one here, nor a button still held.)
        sheetHeld_ = keysHeld();
        sheetTouching_ = (sheetHeld_ & KEY_TOUCH) != 0;
        draw();
    }

    /// The saves the sheet lists: to load from, all but the arrival; to
    /// save into, the player's own (after NEW SAVE). The most recent first.
    void Menu::listSaves()
    {
        savesListed_.clear();
        if (saves_ != nullptr)
            saveListed(*saves_, !saving_, savesListed_);
    }

    int &Menu::sheetChapter()
    {
        static int none = 0;
        none = 0;
        return page_ == Page::Achievements ? achChapter_ : (page_ == Page::Settings ? settingsChapter_ : none);
    }

    int Menu::sheetChapters() const
    {
        return page_ == Page::Achievements ? hldata::kAchChapterCount : (page_ == Page::Settings ? kSettingsChapters : 1);
    }

    int Menu::sheetCount(int chapter) const
    {
        if (page_ == Page::Achievements)
            return achCount(chapter);
        if (page_ == Page::Settings)
        {
            int count = 0;
            settingsRows(chapter, count);
            return count;
        }
        return static_cast<int>(savesListed_.size()) + (saving_ ? 1 : 0);
    }

    void Menu::drawSheet()
    {
        drawSheetHeader();
        drawSheetList();
        drawSheetDetail();
        drawSheetBar();
    }

    /// The chapter between its arrows (a sheet of one: its name alone).
    void Menu::drawSheetHeader()
    {
        Canvas &canvas = touchCanvas.canvas();
        const FontView &small = touchCanvas.font(kFontSmall);
        canvas.fill(0, 0, 256, kSheetListTop, kUiBack);
        const bool arrows = sheetChapters() > 1;
        if (arrows)
        {
            drawSheetButton(kSheetLeftBox, "<", sheetPressed_ == kSheetLeft);
            drawSheetButton(kSheetRightBox, ">", sheetPressed_ == kSheetRight);
        }
        const int chapter = sheetChapter();
        const char *name = page_ == Page::Achievements ? hldata::kAchChapterNames[chapter]
                           : page_ == Page::Settings   ? kSettingsChapterNames[chapter]
                           : saving_                   ? "SAVE GAME"
                                                       : "LOAD GAME";
        const int left = arrows ? kSheetLeftBox.x + kSheetLeftBox.w + 4 : 4;
        const int room = (arrows ? kSheetRightBox.x - 4 : 252) - left;
        const int wide = textWidth(small, name);
        const int x = wide <= room ? left + (room - wide) / 2 : left;
        drawFitted(canvas, small, x, (kSheetHeadHeight - small.height()) / 2 + 1, name, inkFor(small, kUiOrange), room);
    }

    /// The list: the chapter's rows where the stylus has put them, and,
    /// while it is slid aside, the next or the last chapter's coming in.
    void Menu::drawSheetList()
    {
        const SheetGeometry g = sheetGeometry();
        Canvas band = sheetBand(g);
        band.fill(0, 0, 256, g.listHeight, kUiBack);
        const int chapter = sheetChapter(), chapters = sheetChapters();
        drawSheetRows(chapter, sheetSlide_, sheetScroll_);
        if (sheetSlide_ < 0)
            drawSheetRows((chapter + 1) % chapters, sheetSlide_ + 256, 0);
        else if (sheetSlide_ > 0)
            drawSheetRows((chapter + chapters - 1) % chapters, sheetSlide_ - 256, 0);
        // How far down, when there is more than shows.
        const int whole = sheetCount(chapter) * g.row;
        if (whole > g.listHeight && sheetSlide_ == 0)
        {
            const int most = whole - g.listHeight;
            const int tall = g.listHeight * g.listHeight / whole;
            band.fill(251, sheetScroll_ * (g.listHeight - tall) / most, 3, tall, kUiGrey + 1);
        }
    }

    /// `chapter`'s rows, `x` across and scrolled `scroll` down.
    void Menu::drawSheetRows(int chapter, int x, int scroll)
    {
        const SheetGeometry g = sheetGeometry();
        Canvas band = sheetBand(g);
        const FontView &small = touchCanvas.font(kFontSmall);
        const bool current = chapter == sheetChapter() && x == sheetSlide_;
        int settingCount = 0;
        const Row *rows = page_ == Page::Settings ? settingsRows(chapter, settingCount) : nullptr;
        for (int n = 0; n < sheetCount(chapter); ++n)
        {
            const int y = n * g.row - scroll;
            if (y + g.row <= 0 || y >= g.listHeight)
                continue;
            const bool chosen = current && n == sheetSelected_;
            const bool pressed = current && n == sheetPressed_ && sheetDrag_ == 0;
            band.button(x + 2, y + 1, kSheetListWidth, g.row - 2, chosen || pressed);
            if (chosen)
                band.frame(x + 2, y + 1, kSheetListWidth, g.row - 2, kUiOrange + 2);
            const int textY = y + (g.row - g.line) / 2;
            if (page_ == Page::Achievements)
            {
                const hldata::AchievementTracker &tracker = achievements.tracker;
                const int id = achOf(chapter, n);
                const bool got = tracker.has(id);
                drawTrophy(band, x + 6, y + (g.row - 12) / 2, got);
                char status[16];
                achStatus(tracker, id, status, sizeof(status));
                drawFitted(band, small, x + 22, textY, achTitle(tracker, id), inkFor(small, got ? kUiWhite : kUiGrey),
                           achTitleRoom(small, status));
                drawText(band, small, x + kSheetListWidth - 2 - textWidth(small, status), textY, status,
                         inkFor(small, got ? kUiOrange : kUiWhite));
            }
            else if (page_ == Page::Settings)
            {
                const Row &item = rows[n];
                const bool dead = item.id == kFirmPress && !Input::pressureSeen();
                drawText(band, small, x + 8, textY, item.label, inkFor(small, dead ? kUiGrey : kUiWhite));
                if (item.kind == Kind::Slider)
                {
                    // The track, how far along, and the knob.
                    const int least = slidLeast(item.id), most = slidMost(item.id);
                    const int left = x + kSheetTrackLeft, middle = y + g.row / 2;
                    const int knob = left + kSheetTrackWidth * (slid(item.id) - least) / (most - least);
                    band.fill(left, middle - 3, kSheetTrackWidth, 6, kUiTrack);
                    band.frame(left - 1, middle - 4, kSheetTrackWidth + 2, 8, kUiEdge);
                    band.fill(left, middle - 3, knob - left, 6, kUiOrange + (chosen ? 2 : 1));
                    band.fill(knob - 3, y + 3, 7, g.row - 6, chosen ? kUiWhite + 2 : kUiWhite + 1);
                    band.frame(knob - 3, y + 3, 7, g.row - 6, kUiShadow);
                }
                else
                {
                    char value[40];
                    if (item.id == kKey && capturing_ == n)
                        std::snprintf(value, sizeof(value), "press a button");
                    else
                        settingValue(item.id, n, value, sizeof(value));
                    const int room = kSheetListWidth - 16 - textWidth(small, item.label);
                    const int wide = textWidth(small, value) < room ? textWidth(small, value) : room;
                    drawFitted(band, small, x + kSheetListWidth - 4 - wide, textY, value,
                               inkFor(small, dead ? kUiGrey : kUiOrange), room);
                }
            }
            else
            {
                // NEW SAVE; or a save, its name, its chapter and when.
                const int k = n - (saving_ ? 1 : 0);
                if (k < 0)
                {
                    const bool full = saves_ == nullptr || saveFreeManual(*saves_) < 0;
                    drawText(band, small, x + 8, textY, "NEW SAVE", inkFor(small, full ? kUiGrey : kUiOrange));
                    continue;
                }
                const int slot = savesListed_[k];
                const SaveSlot &save = saves_->slots[slot];
                const bool good = save.status == kSaveGood;
                char name[16], when[24];
                saveName(slot, name, sizeof(name));
                if (good)
                    saveWhen(save.heading, when, sizeof(when));
                else
                    std::snprintf(when, sizeof(when), "%s", save.status == kSaveBad ? "damaged" : "other version");
                const int end = drawText(band, small, x + 8, textY, name, inkFor(small, good ? kUiOrange : kUiGrey));
                const int whenWide = textWidth(small, when);
                const char *chapter = save.heading.chapter[0] != 0 ? save.heading.chapter : save.heading.map;
                if (good)
                    drawFitted(band, small, end + 8, textY, chapter, inkFor(small, kUiWhite),
                               x + kSheetListWidth - 10 - whenWide - (end + 8));
                drawText(band, small, x + kSheetListWidth - 4 - whenWide, textY, when, inkFor(small, kUiGrey));
            }
        }
    }

    /// The box under the list: the row touched, read out in full; or, with
    /// none, what the page is.
    void Menu::drawSheetDetail()
    {
        const SheetGeometry g = sheetGeometry();
        Canvas &canvas = touchCanvas.canvas();
        const FontView &small = touchCanvas.font(kFontSmall);
        canvas.fill(0, g.detailTop - 1, 256, g.detailHeight + 2, kUiBack);
        canvas.frame(2, g.detailTop, 252, g.detailHeight, kUiEdge);
        constexpr int kLast = 3, kWidth = 244;
        const int x = 7, y = g.detailTop + 2;
        const int chapter = sheetChapter();
        const bool chosen = sheetSelected_ >= 0 && sheetSelected_ < sheetCount(chapter);
        if (page_ == Page::Achievements)
        {
            const hldata::AchievementTracker &tracker = achievements.tracker;
            if (chosen)
            {
                // Its whole title -- if its row had to cut it short; else what
                // it asks has all four lines.
                const int id = achOf(chapter, sheetSelected_);
                const bool got = tracker.has(id);
                const char *title = achTitle(tracker, id);
                char status[16];
                achStatus(tracker, id, status, sizeof(status));
                int first = 0;
                if (textWidth(small, title) > achTitleRoom(small, status))
                {
                    drawFitted(canvas, small, x, y, title, inkFor(small, got ? kUiOrange : kUiWhite), kWidth);
                    first = 1;
                }
                drawWrapped(canvas, small, x, y, g.line, first, kLast, achText(tracker, id),
                            inkFor(small, got ? kUiWhite : kUiGrey), kWidth);
                return;
            }
            int here = 0;
            for (int n = 0; n < achCount(chapter); ++n)
                here += tracker.has(achOf(chapter, n)) ? 1 : 0;
            char earned[48];
            std::snprintf(earned, sizeof(earned), "This chapter: %d of %d - in all: %d of %d", here, achCount(chapter),
                          tracker.earned(), static_cast<int>(hldata::kAchievementCount));
            drawFitted(canvas, small, x, y, earned, inkFor(small, kUiOrange), kWidth);
            drawWrapped(canvas, small, x, y, g.line, 1, kLast,
                        "Touch one to read it. Slide the list, or touch < >, for another chapter.",
                        inkFor(small, kUiGrey), kWidth);
            return;
        }
        if (page_ == Page::Settings)
        {
            if (capturing_ >= 0)
            {
                drawWrapped(canvas, small, x, y, g.line, 0, kLast, "Press its button now. START: leave it as it was.",
                            inkFor(small, kUiOrange), kWidth);
                return;
            }
            if (!chosen)
            {
                drawWrapped(canvas, small, x, y, g.line, 0, kLast,
                            "Touch a setting to change it, or to read what it does; drag a slider. Slide the list, or "
                            "touch < >, for the other settings. RESET: this page's as they were at first.",
                            inkFor(small, kUiGrey), kWidth);
                return;
            }
            int count = 0;
            const Row &item = settingsRows(chapter, count)[sheetSelected_];
            const char *help = item.id == kFirmPress && !Input::pressureSeen()
                                   ? "This console does not say how hard the stylus presses: not used here."
                                   : settingHelp(item.id);
            drawWrapped(canvas, small, x, y, g.line, 0, item.id == kScheme ? 1 : kLast, help, inkFor(small, kUiWhite), kWidth);
            // (The layout: what the buttons then do, as much as there is room for.)
            if (item.id == kScheme)
            {
                for (int line = 0; line + 2 <= kLast && controlsLegend(settings.controls(), line) != nullptr; ++line)
                    drawFitted(canvas, small, x, y + (line + 2) * g.line, controlsLegend(settings.controls(), line),
                               inkFor(small, kUiGrey), kWidth);
            }
            return;
        }
        // The saves.
        if (!chosen)
        {
            drawWrapped(canvas, small, x, y, g.line, 0, kLast,
                        saving_ ? "Touch NEW SAVE, or a save to put this game in its place."
                                : (savesListed_.empty() ? "There are no saves yet." : "Touch a save, then LOAD."),
                        inkFor(small, kUiGrey), kWidth);
            return;
        }
        const int k = sheetSelected_ - (saving_ ? 1 : 0);
        if (k < 0)
        {
            const bool full = saves_ == nullptr || saveFreeManual(*saves_) < 0;
            drawWrapped(canvas, small, x, y, g.line, 0, kLast,
                        full ? "99 saves: there is no room for another. Put it in the place of one, or delete one."
                             : "A new save of the game as it is now, kept beside the others.",
                        inkFor(small, full ? kUiOrange : kUiWhite), kWidth);
            return;
        }
        const int slot = savesListed_[k];
        const SaveSlot &save = saves_->slots[slot];
        if (save.status != kSaveGood)
        {
            drawWrapped(canvas, small, x, y, g.line, 0, kLast, saveLine(save).c_str(), inkFor(small, kUiGrey), kWidth);
            return;
        }
        const SaveHeading &h = save.heading;
        char line[64], when[24];
        drawFitted(canvas, small, x, y, h.chapter[0] != 0 ? h.chapter : h.map, inkFor(small, kUiOrange), kWidth);
        std::snprintf(line, sizeof(line), "%s - health %d", h.map, static_cast<int>(h.health));
        drawFitted(canvas, small, x, y + g.line, line, inkFor(small, kUiWhite), kWidth);
        if (h.year != 0)
        {
            std::snprintf(when, sizeof(when), "%02u/%02u/%04u %02u:%02u", h.day, h.month, h.year, h.hour, h.minute);
            drawFitted(canvas, small, x, y + 2 * g.line, when, inkFor(small, kUiGrey), kWidth);
        }
        std::snprintf(line, sizeof(line), "played %u:%02u", static_cast<unsigned>(h.playSeconds / 3600),
                      static_cast<unsigned>(h.playSeconds / 60 % 60));
        drawFitted(canvas, small, x, y + (h.year != 0 ? 3 : 2) * g.line, line, inkFor(small, kUiGrey), kWidth);
    }

    /// The buttons along the bottom; or, while something is asked, the
    /// question and YES, NO.
    void Menu::drawSheetBar()
    {
        Canvas &canvas = touchCanvas.canvas();
        canvas.fill(0, kSheetBarTop, 256, 192 - kSheetBarTop, kUiBack);
        if (sheetAsking_ != 0)
        {
            const FontView &small = touchCanvas.font(kFontSmall);
            drawFitted(canvas, small, 6, kSheetBarTop + (kSheetBarHeight - small.height()) / 2, sheetQuestion_,
                       inkFor(small, kUiOrange), kSheetYesBox.x - 10);
            drawSheetButton(kSheetYesBox, "YES", sheetPressed_ == kSheetYes);
            drawSheetButton(kSheetNoBox, "NO", sheetPressed_ == kSheetNo);
            return;
        }
        if (page_ == Page::Achievements)
        {
            drawSheetButton(sheetBarBox(0, 2), settings.achievementPopups ? "POP-UPS: ON" : "POP-UPS: OFF",
                            sheetPressed_ == kSheetBar);
            drawSheetButton(sheetBarBox(1, 2), "BACK", sheetPressed_ == kSheetBar - 1);
            return;
        }
        if (page_ == Page::Settings)
        {
            drawSheetButton(sheetBarBox(0, 2), "RESET", sheetPressed_ == kSheetBar);
            drawSheetButton(sheetBarBox(1, 2), "BACK", sheetPressed_ == kSheetBar - 1);
            return;
        }
        // The saves: SAVE or LOAD the one touched; DELETE it, if it is the
        // player's; BACK.
        const int k = sheetSelected_ - (saving_ ? 1 : 0);
        const int slot = sheetSelected_ >= 0 && k >= 0 && k < static_cast<int>(savesListed_.size()) ? savesListed_[k] : -1;
        const bool newSave = saving_ && sheetSelected_ == 0;
        const bool can = newSave ? saves_ != nullptr && saveFreeManual(*saves_) >= 0
                                 : slot >= 0 && (saving_ || saves_->slots[slot].status == kSaveGood);
        drawSheetButton(sheetBarBox(0, 3), saving_ ? "SAVE" : "LOAD", sheetPressed_ == kSheetBar, !can);
        drawSheetButton(sheetBarBox(1, 3), "DELETE", sheetPressed_ == kSheetBar - 1, slot < kSlotManual);
        drawSheetButton(sheetBarBox(2, 3), "BACK", sheetPressed_ == kSheetBar - 2);
    }

    int Menu::sheetHit(int x, int y) const
    {
        if (sheetAsking_ != 0)
            return kSheetYesBox.has(x, y) ? kSheetYes : (kSheetNoBox.has(x, y) ? kSheetNo : kSheetNothing);
        if (sheetChapters() > 1)
        {
            if (kSheetLeftBox.has(x, y))
                return kSheetLeft;
            if (kSheetRightBox.has(x, y))
                return kSheetRight;
        }
        const int buttons = page_ == Page::Saves ? 3 : 2;
        for (int b = 0; b < buttons; ++b)
        {
            if (sheetBarBox(b, buttons).has(x, y))
                return kSheetBar - b;
        }
        const SheetGeometry g = sheetGeometry();
        if (y < kSheetListTop || y >= kSheetListTop + g.listHeight)
            return kSheetNothing;
        const int n = (y - kSheetListTop + sheetScroll_) / g.row;
        int chapter = const_cast<Menu *>(this)->sheetChapter();
        return n < sheetCount(chapter) && x >= 2 && x < 2 + kSheetListWidth ? n : kSheetList;
    }

    /// The next chapter (1) or the last (-1), slid in.
    void Menu::sheetTurn(int step)
    {
        if (sheetChapters() < 2)
            return;
        sound.ui(kUiSoundPress);
        sheetSlideTo_ = step > 0 ? -256 : 256;
    }

    /// Row `row` read out (-1: none), and kept in sight.
    void Menu::sheetSelect(int row)
    {
        const SheetGeometry g = sheetGeometry();
        sheetSelected_ = row;
        if (row >= 0)
        {
            if (row * g.row < sheetScroll_)
                sheetScroll_ = row * g.row;
            if ((row + 1) * g.row > sheetScroll_ + g.listHeight)
                sheetScroll_ = (row + 1) * g.row - g.listHeight;
        }
        const int over = sheetCount(sheetChapter()) * g.row - g.listHeight;
        const int most = over > 0 ? over : 0;
        sheetScroll_ = sheetScroll_ < 0 ? 0 : (sheetScroll_ > most ? most : sheetScroll_);
        drawSheetList();
        drawSheetDetail();
        drawSheetBar();
    }

    /// A setting's row stepped: its value to the next (or the last), its
    /// slider a notch. True if it changed.
    bool Menu::sheetAdjust(int row, int step)
    {
        if (page_ != Page::Settings || row < 0)
            return false;
        int count = 0;
        const Row *rows = settingsRows(settingsChapter_, count);
        if (row >= count)
            return false;
        if (rows[row].id == kKey)
        {
            // Asked for: the next button pressed is this action's.
            capturing_ = row;
            captureHeld_ = keysHeld() & 0xFFFu;
            drawSheetList();
            drawSheetDetail();
            return true;
        }
        if (!settingStep(rows[row].id, step))
            return false;
        sound.ui(rows[row].kind == Kind::Slider ? kUiSoundTick : kUiSoundPress);
        drawSheetList();
        drawSheetDetail();
        return true;
    }

    /// A slider of a setting's row put where the stylus is (`x` on the
    /// screen). True if it moved.
    bool Menu::sheetSlider(int row, int x)
    {
        int count = 0;
        const Row *rows = settingsRows(settingsChapter_, count);
        if (row < 0 || row >= count || rows[row].kind != Kind::Slider)
            return false;
        const Id id = rows[row].id;
        const int least = slidLeast(id), most = slidMost(id);
        const int left = 2 + kSheetTrackLeft;
        int level = ((x - left) * (most - least) + kSheetTrackWidth / 2) / kSheetTrackWidth + least;
        level = x < left ? least : (level > most ? most : level);
        std::uint8_t &value = slid(id);
        if (value == level)
            return false;
        value = static_cast<std::uint8_t>(level);
        if (isVolume(id))
            sound.applyVolumes();
        sound.ui(kUiSoundTick);
        drawSheetList();
        return true;
    }

    void Menu::sheetAsk(int what, const char *question)
    {
        sheetAsking_ = what;
        std::snprintf(sheetQuestion_, sizeof(sheetQuestion_), "%s", question);
        drawSheetBar();
    }

    /// YES to what was asked.
    Menu::Action Menu::sheetYes()
    {
        const int what = sheetAsking_;
        sheetAsking_ = 0;
        switch (what)
        {
        case kAskReset:
        {
            // This page's settings as they were at first.
            settings.reset(settingsChapter_);
            if (settingsChapter_ == 0)
            {
                sound.applySoundMode();
                sound.applyVolumes();
            }
            capturing_ = -1;
            settings.save();
            drawSheet();
            return Action::None;
        }
        case kAskOverwrite:
            return Action::SaveTo;
        case kAskLoad:
            return Action::Load;
        case kAskDelete:
            SaveGame::remove(slot_);
            listSaves();
            sheetSelected_ = -1;
            drawSheet();
            return Action::None;
        default:
            drawSheetBar();
            return Action::None;
        }
    }

    /// A row touched a second time (or SAVE, LOAD): done.
    Menu::Action Menu::sheetAct(int row)
    {
        if (page_ == Page::Settings)
        {
            sheetAdjust(row, 1);
            return Action::None;
        }
        if (page_ != Page::Saves || row < 0)
            return Action::None;
        const int k = row - (saving_ ? 1 : 0);
        if (k < 0)
        {
            // NEW SAVE: in the first place there is room in.
            const int free = saves_ != nullptr ? saveFreeManual(*saves_) : -1;
            if (free < 0)
                return Action::None;
            slot_ = free;
            return Action::SaveTo;
        }
        if (k >= static_cast<int>(savesListed_.size()))
            return Action::None;
        slot_ = savesListed_[k];
        char name[16], question[40];
        saveName(slot_, name, sizeof(name));
        if (saving_)
        {
            std::snprintf(question, sizeof(question), "OVERWRITE %s?", name);
            sheetAsk(kAskOverwrite, question);
            return Action::None;
        }
        if (saves_->slots[slot_].status != kSaveGood)
            return Action::None;
        if (title_)
            return Action::Load;
        sheetAsk(kAskLoad, "LOAD? UNSAVED IS LOST");
        return Action::None;
    }

    Menu::Action Menu::sheetBarPress(int button)
    {
        if (page_ == Page::Achievements)
        {
            if (button == 0)
            {
                settings.achievementPopups = !settings.achievementPopups;
                drawSheetBar();
                return Action::None;
            }
            return sheetLeave();
        }
        if (page_ == Page::Settings)
        {
            if (button == 0)
            {
                char question[40];
                std::snprintf(question, sizeof(question), "RESET %s?", kSettingsChapterNames[settingsChapter_]);
                sheetAsk(kAskReset, question);
                return Action::None;
            }
            return sheetLeave();
        }
        if (button == 0)
            return sheetAct(sheetSelected_);
        if (button == 1)
        {
            const int k = sheetSelected_ - (saving_ ? 1 : 0);
            if (sheetSelected_ < 0 || k < 0 || k >= static_cast<int>(savesListed_.size()) ||
                savesListed_[k] < kSlotManual)
                return Action::None;
            slot_ = savesListed_[k];
            char name[16], question[40];
            saveName(slot_, name, sizeof(name));
            std::snprintf(question, sizeof(question), "DELETE %s?", name);
            sheetAsk(kAskDelete, question);
            return Action::None;
        }
        return sheetLeave();
    }

    Menu::Action Menu::sheetLeave()
    {
        sound.ui(kUiSoundPress);
        capturing_ = -1;
        sheetAsking_ = 0;
        page_ = page_ == Page::Saves ? asker_ : (title_ ? Page::Title : (netGame.playing() ? Page::NetPause : Page::Main));
        settings.save();
        active_ = -1;
        touching_ = (keysHeld() & KEY_TOUCH) != 0;
        draw();
        return Action::None;
    }

    Menu::Action Menu::sheetFrame()
    {
        const SheetGeometry g = sheetGeometry();
        const std::uint32_t held = keysHeld();
        const std::uint32_t pressed = held & ~sheetHeld_;
        sheetHeld_ = held;

        // A button being asked for (BUTTONS): the next one pressed, START
        // to leave it as it was.
        if (capturing_ >= 0)
        {
            const std::uint32_t fresh = (held & 0xFFFu) & ~captureHeld_;
            captureHeld_ = held & 0xFFFu;
            if (fresh & KEY_START)
            {
                capturing_ = -1;
                drawSheetList();
                drawSheetDetail();
            }
            else if (fresh & kAssignable)
            {
                std::uint32_t button = 1;
                while (!(fresh & kAssignable & button))
                    button <<= 1;
                settings.makeCustom();
                assignKey(settings.keys, capturing_, button);
                capturing_ = -1;
                sound.ui(kUiSoundTick);
                drawSheetList();
                drawSheetDetail();
            }
            sheetTouching_ = (held & KEY_TOUCH) != 0;
            return Action::None;
        }

        // The buttons: L and R another chapter (left and right too, where
        // they change nothing else); up and down the row read out; A to do
        // it; B back (or NO).
        if (sheetDrag_ == 0 && sheetSlide_ == 0 && sheetSlideTo_ == 0 && !sheetTouching_)
        {
            const bool setting = page_ == Page::Settings && sheetSelected_ >= 0;
            if (sheetAsking_ != 0)
            {
                if (pressed & KEY_A)
                    return sheetYes();
                if (pressed & KEY_B)
                {
                    sheetAsking_ = 0;
                    drawSheetBar();
                }
            }
            else if (pressed & KEY_R)
                sheetTurn(1);
            else if (pressed & KEY_L)
                sheetTurn(-1);
            else if (pressed & (KEY_RIGHT | KEY_LEFT))
            {
                const int step = (pressed & KEY_RIGHT) ? 1 : -1;
                if (!(setting && sheetAdjust(sheetSelected_, step)))
                    sheetTurn(step);
            }
            else if (pressed & (KEY_UP | KEY_DOWN))
            {
                const int last = sheetCount(sheetChapter()) - 1;
                if (last >= 0)
                {
                    int row = sheetSelected_ < 0 ? 0 : sheetSelected_ + ((pressed & KEY_DOWN) ? 1 : -1);
                    row = row < 0 ? 0 : (row > last ? last : row);
                    sound.ui(kUiSoundTick);
                    sheetSelect(row);
                }
            }
            else if (pressed & KEY_A)
            {
                if (sheetSelected_ >= 0)
                    return sheetAct(sheetSelected_);
            }
            else if (pressed & KEY_B)
            {
                return sheetLeave();
            }
        }

        // A slide let go of (or asked for) finishes by itself: quickly,
        // then slowing; past the edge, the next chapter is the one shown.
        if (sheetDrag_ != 2 && sheetSlide_ != sheetSlideTo_)
        {
            const int left = sheetSlideTo_ - sheetSlide_;
            int step = left / 3;
            if (step > -8 && step < 8)
                step = left > 0 ? (left < 8 ? left : 8) : (left > -8 ? left : -8);
            sheetSlide_ += step;
            if (sheetSlide_ == sheetSlideTo_ && sheetSlideTo_ != 0)
            {
                const int chapters = sheetChapters();
                int &chapter = sheetChapter();
                chapter = (chapter + (sheetSlideTo_ < 0 ? 1 : chapters - 1)) % chapters;
                sheetSlide_ = sheetSlideTo_ = 0;
                sheetScroll_ = 0;
                sheetSelected_ = -1;
                drawSheetHeader();
                drawSheetDetail();
            }
            drawSheetList();
        }

        // The stylus.
        const bool down = (held & KEY_TOUCH) != 0;
        if (down)
        {
            touchPosition touch;
            touchRead(&touch);
            sheetX_ = touch.px;
            sheetY_ = touch.py;
            Input::notePressure(touch.z1, touch.z2);
        }
        if (down && !sheetTouching_)
        {
            // (While a slide is going on, a touch only waits for it.)
            sheetPressed_ = sheetSlide_ == sheetSlideTo_ ? sheetHit(sheetX_, sheetY_) : kSheetNothing;
            sheetDownX_ = sheetX_;
            sheetDownY_ = sheetY_;
            sheetScrollAt_ = sheetScroll_;
            sheetDrag_ = 0;
            if (sheetPressed_ == kSheetLeft || sheetPressed_ == kSheetRight)
                drawSheetHeader();
            else if (sheetPressed_ <= kSheetBar || sheetPressed_ == kSheetYes || sheetPressed_ == kSheetNo)
                drawSheetBar();
            else if (sheetPressed_ >= 0)
            {
                // A slider touched on its track: it follows the stylus.
                int count = 0;
                const Row *rows = page_ == Page::Settings ? settingsRows(settingsChapter_, count) : nullptr;
                if (rows != nullptr && sheetPressed_ < count && rows[sheetPressed_].kind == Kind::Slider &&
                    sheetX_ >= 2 + kSheetTrackLeft - 6)
                {
                    sheetDrag_ = 3;
                    if (sheetSelected_ != sheetPressed_)
                    {
                        sheetSelected_ = sheetPressed_;
                        drawSheetDetail();
                    }
                    if (!sheetSlider(sheetPressed_, sheetX_))
                        drawSheetList();
                }
                else
                {
                    drawSheetList();
                }
            }
        }
        else if (down && sheetTouching_)
        {
            const int dx = sheetX_ - sheetDownX_, dy = sheetY_ - sheetDownY_;
            const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            if (sheetDrag_ == 3)
            {
                sheetSlider(sheetPressed_, sheetX_);
            }
            else
            {
                if (sheetDrag_ == 0 && (sheetPressed_ >= 0 || sheetPressed_ == kSheetList) &&
                    (ax > kSheetSlop || ay > kSheetSlop))
                {
                    // Moved: no longer a touch of a row, but the list moving
                    // (across only where there is another chapter).
                    sheetDrag_ = ax > ay && sheetChapters() > 1 ? 2 : 1;
                    drawSheetList();
                }
                if (sheetDrag_ == 1)
                {
                    const int over = sheetCount(sheetChapter()) * g.row - g.listHeight;
                    const int most = over > 0 ? over : 0;
                    int scroll = sheetScrollAt_ - dy;
                    scroll = scroll < 0 ? 0 : (scroll > most ? most : scroll);
                    if (scroll != sheetScroll_)
                    {
                        sheetScroll_ = scroll;
                        drawSheetList();
                    }
                }
                else if (sheetDrag_ == 2)
                {
                    const int slide = dx < -256 ? -256 : (dx > 256 ? 256 : dx);
                    if (slide != sheetSlide_)
                    {
                        sheetSlide_ = slide;
                        drawSheetList();
                    }
                }
            }
        }
        else if (!down && sheetTouching_)
        {
            const int was = sheetPressed_;
            const int drag = sheetDrag_;
            sheetPressed_ = kSheetNothing;
            sheetDrag_ = 0;
            const bool same = drag == 0 && was != kSheetNothing && was != kSheetList && sheetHit(sheetX_, sheetY_) == was;
            Action action = Action::None;
            if (drag == 2)
            {
                // Slid far enough (a fifth of the way, a flick): the rest of
                // the way; else back.
                sheetSlideTo_ = sheetSlide_ < -kSheetSwipe ? -256 : (sheetSlide_ > kSheetSwipe ? 256 : 0);
                if (sheetSlideTo_ != 0)
                    sound.ui(kUiSoundTick);
            }
            else if (drag == 3)
            {
                drawSheetList();
            }
            else if (same && was == kSheetLeft)
            {
                sheetTurn(-1);
                drawSheetHeader();
            }
            else if (same && was == kSheetRight)
            {
                sheetTurn(1);
                drawSheetHeader();
            }
            else if (same && was == kSheetYes)
            {
                sound.ui(kUiSoundPress);
                action = sheetYes();
            }
            else if (same && was == kSheetNo)
            {
                sound.ui(kUiSoundPress);
                sheetAsking_ = 0;
                drawSheetBar();
            }
            else if (same && was <= kSheetBar)
            {
                sound.ui(kUiSoundPress);
                if (kSheetBar - was == (page_ == Page::Saves ? 2 : 1))
                {
                    sheetTouching_ = false;
                    return sheetLeave();
                }
                action = sheetBarPress(kSheetBar - was);
                drawSheetBar();
            }
            else if (same && was >= 0)
            {
                // A row: read out; touched again (a setting: at once), done.
                sound.ui(kUiSoundPress);
                const bool again = was == sheetSelected_;
                if (sheetAsking_ != 0)
                {
                    sheetAsking_ = 0;
                    drawSheetBar();
                }
                if (page_ == Page::Settings)
                {
                    if (!again)
                        sheetSelect(was);
                    sheetAdjust(was, 1);
                }
                else if (again && page_ == Page::Saves)
                    action = sheetAct(was);
                else
                    sheetSelect(was);
            }
            else
            {
                // (Let go somewhere else: nothing, and nothing pressed.)
                if (was == kSheetLeft || was == kSheetRight)
                    drawSheetHeader();
                else if (was <= kSheetBar || was == kSheetYes || was == kSheetNo)
                    drawSheetBar();
                else
                    drawSheetList();
            }
            sheetTouching_ = down;
            return action;
        }
        sheetTouching_ = down;
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
        const bool dead = (item.id == kContinue && (saves_ == nullptr || saveMostRecent(*saves_) < 0)) ||
                          (item.id == kCommand && title_) || (item.id == kMultiplayer && netGame.maps().empty()) ||
                          (item.id == kGameHalfLife && !gameThere_[0]) || (item.id == kGameTraining && !gameThere_[1]) ||
                          (item.id == kGameUplink && !gameThere_[2]);
        char text[48];
        switch (item.id)
        {
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
            // (From the title there is nothing to lose: straight to which
            // game. No save is touched either way: a new game only begins.)
            asker_ = page_;
            if (title_)
            {
                openNewGame();
                break;
            }
            ask(Action::NewGame, "NEW GAME?");
            break;
        case kGameHalfLife:
        case kGameTraining:
        case kGameUplink:
        {
            const int which = item.id - kGameHalfLife;
            if (!gameThere_[which])
            {
                drawRow(row, false);
                break;
            }
            static const char *const kMaps[3] = {"", "t0a0", "hldemo1"};
            map_ = kMaps[which];
            // (The Hazard Course is the same at any difficulty: begun at
            // once.)
            if (item.id == kGameTraining)
            {
                skill_ = hldata::kSkillMedium;
                return Action::NewGame;
            }
            page_ = Page::Difficulty;
            draw();
            break;
        }
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
        case kLoadGame:
            saving_ = item.id == kSaveGame;
            asker_ = page_;
            openSheet(Page::Saves);
            break;
        case kYes:
        {
            const Action action = asked_;
            asked_ = Action::None;
            // (A new game: yes, and then which.)
            if (action == Action::NewGame)
            {
                openNewGame();
                break;
            }
            return action;
        }
        case kNo:
            page_ = asker_;
            asked_ = Action::None;
            draw();
            break;
        case kOptions:
            openSheet(Page::Settings);
            break;
        case kAchievementsOpen:
            openSheet(Page::Achievements);
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
            // (From how hard, back to which game; from which game, to where
            // it was asked for.)
            page_ = page_ == Page::Difficulty ? Page::NewGame
                                              : (title_ ? Page::Title : (netGame.playing() ? Page::NetPause : Page::Main));
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
        default:
            break;
        }
        return Action::None;
    }

    Menu::Action Menu::frame()
    {
        if (sheet())
            return sheetFrame();
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
        const bool down = (keysHeld() & KEY_TOUCH) != 0;
        if (down)
        {
            touchPosition touch;
            touchRead(&touch);
            lastX_ = touch.px;
            lastY_ = touch.py;
            Input::notePressure(touch.z1, touch.z2);
        }
        Action action = Action::None;
        if (down && !touching_)
        {
            active_ = rowAt(lastX_, lastY_);
            if (active_ >= 0)
                drawRow(active_, true);
        }
        else if (!down && touching_ && active_ >= 0)
        {
            // Lifted: a button acts if it was lifted from where it was
            // pressed.
            const int row = active_;
            active_ = -1;
            if (rowAt(lastX_, lastY_) == row)
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
