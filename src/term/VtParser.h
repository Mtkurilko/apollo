// Escape sequence parser. DEC/ECMA-48 state machine driving a Screen.
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

    std::string takeReplies();

    std::function<void(const std::string&)> onClipboard; // OSC 52
    // OSC 133 D. A command finished, with its exit status if it gave one.
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

    // Carried across feed() calls so a split sequence still lands as one character.
    char32_t utf8_ = 0;
    int utf8Remaining_ = 0;

    char charset_[2] = {'B', 'B'};
    int activeCharset_ = 0;
};

} // namespace apollo::term
