#pragma once

// The pause menu, on the touch screen: pages of rows -- buttons, which
// act when the stylus is lifted from them; rows that show a setting and
// change it when pressed; and sliders, which follow the stylus -- and the
// keyboard the debug command line is typed on.
//
// What changes a setting is done here; what changes the game (resume, save,
// a new game, a command) is handed back to whoever runs the game.

#include <string>

#include "HlSaveFiles.hpp"

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
            SaveTo,   ///< ... into slot() -- one of the player's six
            Load,     ///< the save in slot()
            Continue, ///< the most recent save (the title's)
            NewGame,
            Command,
            Leave, ///< out of a game between consoles, to the title
        };

        /// Take the touch screen and show the first page.
        void open();
        /// The same, as the game's title: CONTINUE, LOAD GAME, NEW GAME,
        /// OPTIONS, with no game behind it.
        void openTitle();
        /// The list of saves that the load and save pages show (the
        /// caller's: SaveGame::index()).
        void setSaves(const hldata::SaveIndex *saves) { saves_ = saves; }
        /// Which save an action is about (a kSlot*).
        int slot() const { return slot_; }
        /// How hard a new game (Action::NewGame) is to be: kSkillEasy..Hard.
        int skill() const { return skill_; }
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

    private:
        enum class Page
        {
            Main,
            Options,
            Graphics,
            Title,
            Saves,   ///< the list: to load from, or to save into
            Confirm, ///< "overwrite?", "load?", "new game?"
            Controls, ///< the layout of the buttons
            Multiplayer, ///< host a game between consoles, or join one
            NetHost,     ///< the host's lobby: which map, what limits, who is there
            NetJoin,     ///< the games found
            NetLobby,    ///< a client waiting for the host to start
            NetPause,    ///< the pause menu of a game between consoles (which goes on)
            TouchAim,    ///< how the stylus turns the view, and how hard it must press
            Buttons,     ///< which button does what (a custom layout)
            Difficulty,  ///< a new game's: EASY, MEDIUM, DIFFICULT
            Achievements, ///< those earned and those not, a chapter at a time (its own page)
        };
        // The achievements' page: drawn and touched apart from the rows
        // (Menu.cpp). A chapter at a time, its achievements as rows to
        // touch; the list slid aside for another chapter, dragged up and
        // down when it is longer than there is room for.
        int achChapter_ = 0;   ///< the chapter shown (kept while the console is on)
        int achScroll_ = 0;    ///< how far down its list is scrolled, in pixels
        int achSelected_ = -1; ///< the row read out in the box under the list; -1: none
        int achSlide_ = 0;     ///< how far across the list is slid, in pixels
        int achSlideTo_ = 0;   ///< ... where it is going by itself: 0 back; -256, 256 the next, the last chapter
        int achPressed_ = -1;  ///< what the stylus went down on (a row, or kAch* in Menu.cpp)
        int achDrag_ = 0;      ///< 0 a touch; 1 scrolling; 2 sliding
        int achDownX_ = 0, achDownY_ = 0, achX_ = 0, achY_ = 0, achScrollAt_ = 0;
        bool achTouching_ = false;
        std::uint32_t achHeld_ = 0; ///< the buttons held a frame ago (so that only a press counts)
        void openAchievements();
        Action achievementsFrame();
        Action achLeave();
        void achTurn(int step);
        void achSelect(int row);
        int achHit(int x, int y) const;
        void drawAchievements();
        void drawAchHeader();
        void drawAchList();
        void drawAchRows(int chapter, int x, int scroll);
        void drawAchDetail();
        void drawAchBar();
        /// The buttons page: which of its two screens; and the action whose
        /// button is being asked for (-1: none), with what was held when it
        /// was asked (so that only a new press counts).
        int buttonsScreen_ = 0;
        int capturing_ = -1;
        std::uint32_t captureHeld_ = 0;
        void showButtons(int screen);
        const hldata::SaveIndex *saves_ = nullptr;
        bool title_ = false;   ///< it is the title's menu, not the pause's
        bool saving_ = false;  ///< the list is shown to save into
        int slot_ = -1;
        int skill_ = 2; ///< (kSkillMedium)
        Action asked_ = Action::None; ///< what "yes" on the confirm page does
        Page asker_ = Page::Main;     ///< ... and where "no" goes back to
        void showSaves(bool saving);
        void ask(Action action, const char *question);
        Page page_ = Page::Main;
        // A game between consoles: what the host has chosen, and what was
        // last drawn of who is there (to draw again when it changes).
        int netMap_ = 0, netFrags_ = 20, netMinutes_ = 10;
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
        bool slide(int row, int x);
    };

    extern Menu menu;
} // namespace hlrt
