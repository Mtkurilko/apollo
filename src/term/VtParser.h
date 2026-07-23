// The escape sequence parser.
//
// Bytes arrive from the pty, and this turns them into operations on a Screen.
// The state machine follows the shape of the DEC/ECMA-48 one that xterm and
// every terminal since implements, which is what lets vim, tmux, htop, less and
// full colour output behave here exactly as they do anywhere else.
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "term/Screen.h"

namespace apollo::term {

class VtParser {
public:
    explicit VtParser(Screen& screen);

    void feed(std::string_view bytes);
    void reset();

    // Things the terminal owes the program: cursor position reports, device
    // attributes, and so on. The session writes these back to the pty.
    std::string takeReplies();

    // OSC 52. A program asking to put something on the clipboard; the UI
    // decides whether to honour it.
    std::function<void(const std::string&)> onClipboard;
    // OSC 133 D — a command finished, with its exit status if it gave one.
    std::function<void(int)> onCommandFinished;

private:
    enum class State {
        Ground, Escape, EscapeIntermediate,
        CsiEntry, CsiParam, CsiIntermediate, CsiIgnore,
        OscString, DcsIgnore, StringIgnore,
    };

    void consume(unsigned char byte);
    bool handleControl(unsigned char byte); // C0 handled in any state
    void groundByte(unsigned char byte);
    void escapeByte(unsigned char byte);
    void csiByte(unsigned char byte);
    void oscByte(unsigned char byte);

    void dispatchEscape(unsigned char final);
    void dispatchCsi(unsigned char final);
    void dispatchOsc();
    void applySgr();
    void setMode(bool enable);
    void setPrivateMode(bool enable);

    int param(std::size_t index, int fallback) const;
    void reply(const std::string& text);
    char32_t translate(char32_t cp) const;

    Screen& screen_;
    State state_ = State::Ground;

    std::vector<int> params_;
    bool paramPending_ = false;
    std::string intermediates_;
    bool privateMarker_ = false;
    std::string oscBuffer_;
    std::string replies_;

    // UTF-8 decoding, carried across feed() calls so a split sequence still
    // produces one character.
    char32_t utf8_ = 0;
    int utf8Remaining_ = 0;

    // Character sets. G0/G1 hold 'B' for ASCII or '0' for DEC line drawing,
    // which is how many programs still draw boxes.
    char charset_[2] = {'B', 'B'};
    int activeCharset_ = 0;
};

} // namespace apollo::term
