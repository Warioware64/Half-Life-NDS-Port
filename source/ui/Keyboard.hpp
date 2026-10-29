#pragma once

// The touch screen's keyboard: a line being typed, the keys under it, and
// SPACE / ENTER / CANCEL along the bottom. The COMMAND console types its
// lines with it, and the profile the player's name.
//
// It draws itself and says what each frame's touches and buttons did;
// what is done about ENTER (a line run, a name kept) is its user's. The
// user's loop waits for the blank and scans the keys, as every loop of the
// menus does, then asks poll().

#include <cstddef>
#include <string>

namespace hlrt
{
    class Keyboard
    {
    public:
        enum class Event
        {
            None,
            Changed,  ///< a key typed or rubbed out: the line is as it is now
            Enter,    ///< ENTER, or A
            Cancel,   ///< CANCEL, B or START
            Up,       ///< up on the pad (the console: an earlier line)
            Down,
            PageBack, ///< L (the console: what was said before)
            PageOn,   ///< R
        };

        /// Typing into `text`, at most `most` characters, with the line at
        /// `promptTop` and the keys below it.
        Keyboard(std::string &text, std::size_t most, int promptTop = 64);

        /// The line, the keys and the bottom row, all of it.
        void draw();
        /// The line alone (after `text` was changed from outside).
        void drawPrompt();
        /// This frame's: what was touched or pressed (keysDown() and the
        /// touch as scanKeys() left them).
        Event poll();

    private:
        void key(int row, int col, bool down);
        void wide(int n, bool down);

        std::string &text_;
        std::size_t most_;
        int promptTop_;
        int keysTop_;
        // The touch that opened it is still down, and may be on a key: not
        // until it has lifted is anything typed.
        bool touching_ = true;
        int heldRow_ = -1, heldCol_ = -1, heldWide_ = -1;
    };
} // namespace hlrt
