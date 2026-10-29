#pragma once

// The pause menu, on the touch screen: pages of rows -- buttons, which
// act when the stylus is lifted from them; rows that show a setting and
// change it when pressed; and sliders, which follow the stylus -- and the
// keyboard the debug command line is typed on.
//
// What changes a setting is done here; what changes the game (resume, save,
// a new game, a command) is handed back to whoever runs the game.

#include <cstdint>
#include <string>
#include <vector>

#include "HlSaveFiles.hpp"
#include "HlUi.hpp"

namespace hlrt
{
    class Menu
    {
    public:
        enum class Action
        {
            None,
            Resume,
            Save,     ///< the quick one
            SaveTo,   ///< ... into slot() -- one of the player's
            Load,     ///< the save in slot()
            Continue, ///< the most recent save (the title's)
            NewGame,
            Command,
            Leave, ///< out of a game between consoles, to the title
            Quit,  ///< the title's QUIT: back to whatever started the game
        };

        /// Whether QUIT is offered: there is somewhere to go back to (the
        /// loader that started the game said where it was).
        void setQuittable(bool can);

        /// Take the touch screen and show the first page.
        void open();
        /// The same, as the game's title: CONTINUE, LOAD GAME, NEW GAME,
        /// OPTIONS, with no game behind it.
        void openTitle();
        /// The same, on the lobby of the game between consoles this console
        /// is in (a match over: back to it).
        void openLobby();
        /// The list of saves that the load and save pages show (the
        /// caller's: SaveGame::index()).
        void setSaves(const hldata::SaveIndex *saves) { saves_ = saves; }
        /// Which save an action is about (a kSlot*).
        int slot() const { return slot_; }
        /// How hard a new game (Action::NewGame) is to be: kSkillEasy..Hard.
        int skill() const { return skill_; }
        /// Which map a new game begins in: "" for the game's own start (the
        /// manifest's), or the Hazard Course's or Uplink's first.
        const std::string &map() const { return map_; }
        /// Once a frame while it is open (after the keys have been scanned).
        Action frame();
        /// What a console line said, and whether it closes the console.
        struct ConsoleReply
        {
            std::string text;
            bool close = false;
        };
        using ConsoleRun = ConsoleReply (*)(const std::string &line);
        /// The console: the game's own, on the touch screen -- what it has
        /// said above, the line being typed, the keyboard. Each line given
        /// (ENTER, A) is run by `run`, and it and what it said are added
        /// above; it is closed by CANCEL, B or START, or by a line that
        /// says so. Stands the game still meanwhile. The sound is whatever
        /// it was (the menu's caller has it paused); the keys' own clicks
        /// are heard.
        void console(ConsoleRun run);
        /// A line typed on the touch screen's keyboard (the profile's name),
        /// at most `most` characters, under `heading`: true, with `text` the
        /// line, if it was given; false if it was not (and `text` is as it
        /// was).
        bool typeLine(std::string &text, std::size_t most, const char *heading);

    private:
        /// Why a row is greyed out (a line to show when it is touched), or
        /// null if it is not.
        const char *whyDead(int id) const;
        void showReason(const char *why);
        /// What is on the card, for the rows that are there only if it is.
        void probeCard();
        // (The numbers are rowsOf's, in Menu.cpp; a sheet has none.)
        enum class Page
        {
            Main = 0,
            Settings = 1,     ///< OPTIONS: a sheet, a category at a time
            Title = 3,
            Saves = 4,        ///< a sheet: the saves, to load from or to save into
            Confirm = 5,      ///< "new game?"
            Multiplayer = 7,  ///< host a game between consoles, or join one
            NetHost = 8,      ///< the host's lobby: which map, what limits, who is there
            NetJoin = 9,      ///< the games found
            NetLobby = 10,    ///< a client waiting for the host to start
            NetPause = 11,    ///< the pause menu of a game between consoles (which goes on)
            Difficulty = 14,  ///< a new game's: EASY, MEDIUM, DIFFICULT
            Achievements = 15, ///< a sheet: those earned and those not, a chapter at a time
            NewGame = 16,     ///< which game: Half-Life, the Hazard Course, Uplink
            Profile = 17,     ///< a sheet: the player as the others see them, shown on the top screen
        };

        // A sheet: a page of its own, drawn and touched apart from the rows
        // (Menu.cpp) -- a chapter at a time between arrows, its rows to
        // touch in a list that is slid aside for another chapter and
        // dragged up and down when it is longer than there is room for, a
        // box that reads out the row touched, and buttons along the bottom
        // (which ask "...? YES NO" there when something is to be made sure
        // of). The achievements, the settings and the saves are sheets.
        bool sheet() const
        {
            return page_ == Page::Achievements || page_ == Page::Settings || page_ == Page::Saves ||
                   page_ == Page::Profile;
        }
        /// A sheet of settings' rows: OPTIONS, PROFILE.
        bool optionSheet() const { return page_ == Page::Settings || page_ == Page::Profile; }
        int achChapter_ = 0, settingsChapter_ = 0; ///< the chapter each was last on (kept while the console is on)
        int sheetScroll_ = 0;    ///< how far down the list is scrolled, in pixels
        int sheetSelected_ = -1; ///< the row read out in the box; -1: none
        int sheetSlide_ = 0;     ///< how far across the list is slid, in pixels
        int sheetSlideTo_ = 0;   ///< ... where it is going by itself: 0 back; -256, 256 the next, the last chapter
        int sheetPressed_ = -1;  ///< what the stylus went down on (a row, or kSheet* in Menu.cpp)
        int sheetDrag_ = 0;      ///< 0 a touch; 1 scrolling; 2 sliding; 3 a slider moved
        int sheetDownX_ = 0, sheetDownY_ = 0, sheetX_ = 0, sheetY_ = 0, sheetScrollAt_ = 0;
        bool sheetTouching_ = false;
        std::uint32_t sheetHeld_ = 0; ///< the buttons held a frame ago (so that only a press counts)
        int sheetAsking_ = 0;         ///< what YES does (kAsk* in Menu.cpp); 0: nothing asked
        char sheetQuestion_[40] = {};
        void openSheet(Page page);
        int &sheetChapter();
        int sheetChapters() const;
        int sheetCount(int chapter) const;
        Action sheetFrame();
        Action sheetLeave();
        Action sheetAct(int row);
        void typeName();
        Action sheetBarPress(int button);
        Action sheetYes();
        void sheetAsk(int what, const char *question);
        void sheetTurn(int step);
        void sheetSelect(int row);
        bool sheetAdjust(int row, int step);
        bool sheetSlider(int row, int x);
        /// The level of the slider in `row` (-1: not a slider).
        int sheetSliderLevel(int row);
        /// The slider being slid: heard where it rests (HlUi.hpp).
        hldata::UiSettle sliderSettle_;
        int sheetHit(int x, int y) const;
        void drawSheet();
        void drawSheetHeader();
        void drawSheetList();
        void drawSheetRows(int chapter, int x, int scroll);
        void drawSheetDetail();
        void drawSheetBar();

        /// A button of the BUTTONS category being asked for (-1: none), with
        /// what was held when it was asked (so that only a new press counts).
        int capturing_ = -1;
        std::uint32_t captureHeld_ = 0;
        /// The saves the sheet lists, as it lists them (kSlot*), and whether
        /// it lists them to save into (with NEW SAVE first).
        std::vector<int> savesListed_;
        void listSaves();
        /// Which game NEW GAME begins: the game's own start (""), or the
        /// Hazard Course's, or Uplink's; and which of those are on the card.
        std::string map_;
        bool gameThere_[3] = {true, true, true};
        const hldata::SaveIndex *saves_ = nullptr;
        bool title_ = false;   ///< it is the title's menu, not the pause's
        bool saving_ = false;  ///< the list is shown to save into
        int slot_ = -1;
        int skill_ = 2; ///< (kSkillMedium)
        Action asked_ = Action::None; ///< what "yes" on the confirm page does
        Page asker_ = Page::Main;     ///< ... and where "no" goes back to
        void ask(Action action, const char *question);
        void openNewGame();
        Page page_ = Page::Main;
        // A game between consoles: what the host has chosen, and what was
        // last drawn of who is there (to draw again when it changes).
        // (The map and the limits are the settings': the match last hosted.)
        /// Which of netGame.maps() is hosted: the settings' (0 if empty).
        std::size_t hostMap() const;
        unsigned netShown_ = 0;
        int netTick_ = 0;
        void showFound();
        void drawPlayers(int top, bool scores);
        unsigned netSignature() const;
        bool big() const;
        int active_ = -1; ///< the row the stylus went down on, or -1
        bool touching_ = false;
        int lastX_ = 0, lastY_ = 0;
        void draw();
        void drawRow(int row, bool down);
        int rowAt(int x, int y) const;
        int rowCount() const;
        Action fire(int row);
    };

    extern Menu menu;
} // namespace hlrt
