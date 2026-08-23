#include "term/VtParser.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace apollo::term {
namespace {

constexpr std::size_t kMaxParams = 32;
constexpr std::size_t kMaxOsc = 8192;

char32_t decGraphic(char32_t cp) {
    static const char32_t table[] = {
        0x00A0, 0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0,
        0x00B1, 0x2424, 0x240B, 0x2518, 0x2510, 0x250C, 0x2514, 0x253C,
        0x23BA, 0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C, 0x2524, 0x2534,
        0x252C, 0x2502, 0x2264, 0x2265, 0x03C0, 0x2260, 0x00A3, 0x00B7,
    };
    if (cp < 0x5F || cp > 0x7E) return cp;
    return table[cp - 0x5F];
}

// The xterm 256 colour cube, for indices past the theme's first sixteen.
void indexedToRgb(int index, int& r, int& g, int& b) {
    if (index < 16) { r = g = b = 0; return; }
    if (index < 232) {
        const int n = index - 16;
        static const int steps[6] = {0, 95, 135, 175, 215, 255};
        r = steps[(n / 36) % 6];
        g = steps[(n / 6) % 6];
        b = steps[n % 6];
        return;
    }
    const int level = 8 + (index - 232) * 10;
    r = g = b = level;
}

std::string percentDecode(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const std::string hex = text.substr(i + 1, 2);
            char* end = nullptr;
            const long value = std::strtol(hex.c_str(), &end, 16);
            if (end && *end == '\0') { out.push_back(static_cast<char>(value)); i += 2; continue; }
        }
        out.push_back(text[i]);
    }
    return out;
}

} // namespace

VtParser::VtParser(Screen& screen) : screen_(screen) {}

void VtParser::reset() {
    state_ = State::Ground;
    params_.clear();
    paramPending_ = false;
    intermediates_.clear();
    privateMarker_ = false;
    oscBuffer_.clear();
    utf8_ = 0;
    utf8Remaining_ = 0;
    charset_[0] = charset_[1] = 'B';
    activeCharset_ = 0;
}

std::string VtParser::takeReplies() {
    std::string out;
    out.swap(replies_);
    return out;
}

void VtParser::reply(const std::string& text) { replies_ += text; }

int VtParser::param(std::size_t index, int fallback) const {
    if (index >= params_.size() || params_[index] < 0) return fallback;
    return params_[index];
}

char32_t VtParser::translate(char32_t cp) const {
    if (charset_[activeCharset_] == '0') return decGraphic(cp);
    return cp;
}

void VtParser::feed(std::string_view bytes) {
    for (const char c : bytes) consume(static_cast<unsigned char>(c));
}

void VtParser::consume(unsigned char byte) {
    switch (state_) {
        case State::Ground:             groundByte(byte); break;
        case State::Escape:
        case State::EscapeIntermediate: escapeByte(byte); break;
        case State::CsiEntry:
        case State::CsiParam:
        case State::CsiIntermediate:
        case State::CsiIgnore:          csiByte(byte); break;
        case State::OscString:          oscByte(byte); break;
        case State::DcsIgnore:
        case State::StringIgnore:
            if (byte == 0x1B) state_ = State::Escape;
            else if (byte == 0x07) state_ = State::Ground;
            break;
    }
}

bool VtParser::handleControl(unsigned char byte) {
    switch (byte) {
        case 0x07: screen_.bellPending = true; return true;               // BEL
        case 0x08: screen_.backspace(); return true;                      // BS
        case 0x09: screen_.tab(); return true;                            // HT
        case 0x0A: case 0x0B: case 0x0C: screen_.lineFeed(); return true; // LF VT FF
        case 0x0D: screen_.carriageReturn(); return true;                 // CR
        case 0x0E: activeCharset_ = 1; return true;                       // SO
        case 0x0F: activeCharset_ = 0; return true;                       // SI
        case 0x18: case 0x1A:                                             // CAN SUB
            state_ = State::Ground;
            return true;
        default:
            return byte < 0x20 && byte != 0x1B; // other C0: swallow
    }
}

void VtParser::groundByte(unsigned char byte) {
    if (byte == 0x1B) {
        state_ = State::Escape;
        intermediates_.clear();
        return;
    }
    if (byte < 0x20 || byte == 0x7F) {
        if (byte != 0x7F) handleControl(byte);
        return;
    }

    // UTF-8.
    if (utf8Remaining_ > 0) {
        if ((byte & 0xC0) != 0x80) {
            utf8Remaining_ = 0;
            screen_.put(0xFFFD);
            groundByte(byte);
            return;
        }
        utf8_ = (utf8_ << 6) | (byte & 0x3F);
        if (--utf8Remaining_ == 0) screen_.put(translate(utf8_));
        return;
    }
    if (byte < 0x80) { screen_.put(translate(byte)); return; }
    if ((byte & 0xE0) == 0xC0) { utf8_ = byte & 0x1F; utf8Remaining_ = 1; return; }
    if ((byte & 0xF0) == 0xE0) { utf8_ = byte & 0x0F; utf8Remaining_ = 2; return; }
    if ((byte & 0xF8) == 0xF0) { utf8_ = byte & 0x07; utf8Remaining_ = 3; return; }
    screen_.put(0xFFFD);
}

void VtParser::escapeByte(unsigned char byte) {
    if (byte == 0x1B) { state_ = State::Escape; intermediates_.clear(); return; }
    if (byte < 0x20) { handleControl(byte); return; }

    if (byte >= 0x20 && byte <= 0x2F) { // intermediate: ( ) * + # SP
        intermediates_.push_back(static_cast<char>(byte));
        state_ = State::EscapeIntermediate;
        return;
    }

    switch (byte) {
        case '[':
            state_ = State::CsiEntry;
            params_.clear();
            paramPending_ = false;
            intermediates_.clear();
            privateMarker_ = false;
            return;
        case ']':
            state_ = State::OscString;
            oscBuffer_.clear();
            return;
        case 'P':
            state_ = State::DcsIgnore;
            return;
        case 'X': case '^': case '_':
            state_ = State::StringIgnore;
            return;
        case '\\': // ST, terminating a string we were ignoring
            state_ = State::Ground;
            return;
        default:
            dispatchEscape(byte);
            state_ = State::Ground;
            return;
    }
}

void VtParser::dispatchEscape(unsigned char final) {
    if (!intermediates_.empty()) {
        const char which = intermediates_[0];
        if (which == '(' || which == ')' || which == '*' || which == '+') {
            charset_[which == '(' ? 0 : 1] = static_cast<char>(final);
            return;
        }
        if (which == '#' && final == '8') { screen_.alignmentTest(); return; }
        return;
    }

    switch (final) {
        case '7': screen_.saveCursor(); break;
        case '8': screen_.restoreCursor(); break;
        case 'D': screen_.lineFeed(); break;          // IND
        case 'E': screen_.nextLine(); break;          // NEL
        case 'H': screen_.setTabStop(); break;        // HTS
        case 'M': screen_.reverseLineFeed(); break;   // RI
        case 'c': screen_.reset(); reset(); break;    // RIS
        case '=': screen_.applicationKeypad = true; break;
        case '>': screen_.applicationKeypad = false; break;
        case 'Z': reply("\x1B[?6c"); break;           // DECID
        default: break;
    }
}

void VtParser::csiByte(unsigned char byte) {
    if (byte == 0x1B) { state_ = State::Escape; intermediates_.clear(); return; }
    if (byte < 0x20) { handleControl(byte); return; }

    if (state_ == State::CsiIgnore) {
        if (byte >= 0x40 && byte <= 0x7E) state_ = State::Ground;
        return;
    }

    if (byte >= 0x30 && byte <= 0x39) { // digit
        if (!paramPending_) { params_.push_back(0); paramPending_ = true; }
        if (params_.size() <= kMaxParams) {
            int& value = params_.back();
            if (value < 0) value = 0;
            value = std::min(value * 10 + (byte - '0'), 1 << 20);
        }
        state_ = State::CsiParam;
        return;
    }
    if (byte == ';' || byte == ':') {
        // A colon separates sub-parameters (24 bit SGR); most terminals treat both alike.
        if (!paramPending_) params_.push_back(-1);
        paramPending_ = false;
        if (params_.size() > kMaxParams) { state_ = State::CsiIgnore; return; }
        state_ = State::CsiParam;
        return;
    }
    if (byte >= 0x3C && byte <= 0x3F) { // < = > ?
        privateMarker_ = true;
        state_ = State::CsiParam;
        return;
    }
    if (byte >= 0x20 && byte <= 0x2F) {
        intermediates_.push_back(static_cast<char>(byte));
        state_ = State::CsiIntermediate;
        return;
    }
    if (byte >= 0x40 && byte <= 0x7E) {
        dispatchCsi(byte);
        state_ = State::Ground;
        params_.clear();
        paramPending_ = false;
        intermediates_.clear();
        privateMarker_ = false;
        return;
    }
}

void VtParser::dispatchCsi(unsigned char final) {
    const int first = param(0, 1);

    switch (final) {
        case '@': screen_.insertChars(first); break;
        case 'A': screen_.moveBy(0, -first); break;
        case 'B': screen_.moveBy(0, first); break;
        case 'C': screen_.moveBy(first, 0); break;
        case 'D': screen_.moveBy(-first, 0); break;
        case 'E': screen_.moveToColumn(0); screen_.moveBy(0, first); break;
        case 'F': screen_.moveToColumn(0); screen_.moveBy(0, -first); break;
        case 'G': case '`': screen_.moveToColumn(first - 1); break;
        case 'H': case 'f': screen_.moveTo(param(1, 1) - 1, param(0, 1) - 1); break;
        case 'I': screen_.tab(first); break;
        case 'J': screen_.eraseDisplay(param(0, 0)); break;
        case 'K': screen_.eraseLine(param(0, 0)); break;
        case 'L': screen_.insertLines(first); break;
        case 'M': screen_.deleteLines(first); break;
        case 'P': screen_.deleteChars(first); break;
        case 'S': screen_.scrollUp(first); break;
        case 'T': screen_.scrollDown(first); break;
        case 'X': screen_.eraseChars(first); break;
        case 'Z': screen_.backTab(first); break;
        case 'a': screen_.moveBy(first, 0); break;
        case 'b': screen_.repeatLast(first); break;
        case 'd': screen_.moveToRow(first - 1); break;
        case 'e': screen_.moveBy(0, first); break;
        case 'g': screen_.clearTabStop(param(0, 0) == 3); break;
        case 'h': privateMarker_ ? setPrivateMode(true) : setMode(true); break;
        case 'l': privateMarker_ ? setPrivateMode(false) : setMode(false); break;
        case 'm': applySgr(); break;

        case 'c': // Device attributes: a VT220 with colour is a safe answer.
            if (!privateMarker_) reply("\x1B[?62;1;6;9;15;22c");
            break;

        case 'n':
            if (param(0, 0) == 5) reply("\x1B[0n");
            else if (param(0, 0) == 6) {
                reply("\x1B[" + std::to_string(screen_.cursorY() + 1) + ";" +
                      std::to_string(screen_.cursorX() + 1) + "R");
            }
            break;

        case 'p':
            if (intermediates_ == "!") { screen_.reset(); reset(); } // DECSTR
            break;

        case 'q':
            if (intermediates_ == " ") screen_.setCursorStyle(param(0, 0)); // DECSCUSR
            break;

        case 'r':
            if (!privateMarker_) {
                screen_.setScrollRegion(param(0, 1) - 1, param(1, screen_.rows()) - 1);
            }
            break;

        case 's': screen_.saveCursor(); break;
        case 'u': screen_.restoreCursor(); break;

        case 't': // Window operations. Only the size reports are worth answering.
            if (param(0, 0) == 18) {
                reply("\x1B[8;" + std::to_string(screen_.rows()) + ";" +
                      std::to_string(screen_.cols()) + "t");
            }
            break;

        default: break;
    }
}

void VtParser::setMode(bool enable) {
    for (std::size_t i = 0; i < params_.size(); ++i) {
        switch (param(i, 0)) {
            case 4: screen_.insertMode = enable; break;
            default: break;
        }
    }
}

void VtParser::setPrivateMode(bool enable) {
    for (std::size_t i = 0; i < params_.size(); ++i) {
        switch (param(i, 0)) {
            case 1:    screen_.applicationCursor = enable; break;
            case 3:    screen_.eraseDisplay(2); screen_.moveTo(0, 0); break; // DECCOLM
            case 5:    screen_.reverseVideo = enable; break;
            case 6:    screen_.originMode = enable; screen_.moveTo(0, 0); break;
            case 7:    screen_.autoWrap = enable; break;
            case 25:   screen_.setCursorVisible(enable); break;
            case 1000: case 1002: case 1003:
                screen_.mouseTracking = enable ? param(i, 0) : 0;
                break;
            case 1004: screen_.focusEvents = enable; break;
            case 1005: case 1015: break; // older mouse encodings; SGR is enough
            case 1006: screen_.mouseSgr = enable; break;
            case 47: case 1047:
                screen_.useAlternate(enable, true);
                break;
            case 1048:
                enable ? screen_.saveCursor() : screen_.restoreCursor();
                break;
            case 1049:
                if (enable) { screen_.saveCursor(); screen_.useAlternate(true, true); }
                else { screen_.useAlternate(false, false); screen_.restoreCursor(); }
                break;
            case 2004: screen_.bracketedPaste = enable; break;
            default: break;
        }
    }
}

void VtParser::applySgr() {
    Attrs& attrs = screen_.attrs();
    if (params_.empty()) { attrs = Attrs{}; return; }

    for (std::size_t i = 0; i < params_.size(); ++i) {
        const int code = param(i, 0);
        switch (code) {
            case 0:  attrs = Attrs{}; break;
            case 1:  attrs.flags |= FlagBold; break;
            case 2:  attrs.flags |= FlagDim; break;
            case 3:  attrs.flags |= FlagItalic; break;
            case 4:  attrs.flags |= FlagUnderline; break;
            case 5: case 6: attrs.flags |= FlagBlink; break;
            case 7:  attrs.flags |= FlagReverse; break;
            case 8:  attrs.flags |= FlagHidden; break;
            case 9:  attrs.flags |= FlagStrike; break;
            case 21: case 22: attrs.flags &= ~(FlagBold | FlagDim); break;
            case 23: attrs.flags &= ~FlagItalic; break;
            case 24: attrs.flags &= ~FlagUnderline; break;
            case 25: attrs.flags &= ~FlagBlink; break;
            case 27: attrs.flags &= ~FlagReverse; break;
            case 28: attrs.flags &= ~FlagHidden; break;
            case 29: attrs.flags &= ~FlagStrike; break;
            case 39: attrs.fg = kColorDefault; break;
            case 49: attrs.bg = kColorDefault; break;

            case 38: case 48: {
                const bool foreground = code == 38;
                const int kind = param(i + 1, 0);
                if (kind == 5) {
                    const int index = param(i + 2, 0);
                    ColorRef colour;
                    if (index < 16) {
                        colour = indexedColor(index);
                    } else {
                        int r = 0, g = 0, b = 0;
                        indexedToRgb(index, r, g, b);
                        colour = rgbColor(r, g, b);
                    }
                    (foreground ? attrs.fg : attrs.bg) = colour;
                    i += 2;
                } else if (kind == 2) {
                    (foreground ? attrs.fg : attrs.bg) =
                        rgbColor(param(i + 2, 0), param(i + 3, 0), param(i + 4, 0));
                    i += 4;
                }
                break;
            }

            default:
                if (code >= 30 && code <= 37) attrs.fg = indexedColor(code - 30);
                else if (code >= 40 && code <= 47) attrs.bg = indexedColor(code - 40);
                else if (code >= 90 && code <= 97) attrs.fg = indexedColor(code - 90 + 8);
                else if (code >= 100 && code <= 107) attrs.bg = indexedColor(code - 100 + 8);
                break;
        }
    }
}

void VtParser::oscByte(unsigned char byte) {
    if (byte == 0x07) { dispatchOsc(); state_ = State::Ground; return; }
    if (byte == 0x1B) {
        dispatchOsc();
        state_ = State::Escape;
        intermediates_.clear();
        return;
    }
    if (oscBuffer_.size() < kMaxOsc) oscBuffer_.push_back(static_cast<char>(byte));
}

void VtParser::dispatchOsc() {
    const std::string body = oscBuffer_;
    oscBuffer_.clear();

    const auto semi = body.find(';');
    const std::string code = body.substr(0, semi);
    const std::string rest = semi == std::string::npos ? "" : body.substr(semi + 1);

    if (code == "0" || code == "1" || code == "2") {
        screen_.title = rest;
        return;
    }

    if (code == "7") {
        std::string path = rest;
        if (path.rfind("file://", 0) == 0) {
            const auto slash = path.find('/', 7);
            path = slash == std::string::npos ? "" : path.substr(slash);
        }
        if (!path.empty()) screen_.cwd = percentDecode(path);
        return;
    }

    if (code == "52") {
        // Clipboard.
        const auto split = rest.find(';');
        if (split != std::string::npos && onClipboard) onClipboard(rest.substr(split + 1));
        return;
    }

    if (code == "133") {
        if (rest.rfind("A", 0) == 0) { screen_.markPrompt(); screen_.commandRunning = false; }
        else if (rest.rfind("C", 0) == 0) screen_.commandRunning = true;
        else if (rest.rfind("D", 0) == 0) {
            screen_.commandRunning = false;
            if (!onCommandFinished) return;
            const auto semicolon = rest.find(';');
            int status = 0;
            if (semicolon != std::string::npos) {
                status = std::atoi(rest.c_str() + semicolon + 1);
            }
            onCommandFinished(status);
        }
        return;
    }
}

} // namespace apollo::term
