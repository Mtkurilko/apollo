// The terminal grid: what is on screen, what has scrolled off, and the modes
// a program can set. Knows nothing about escape sequences or drawing.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace apollo::term {

using ColorRef = std::uint32_t;

constexpr ColorRef kColorDefault = 0;
constexpr ColorRef kIndexedTag = 0x02000000u;
constexpr ColorRef kRgbTag = 0x01000000u;

constexpr ColorRef indexedColor(int index) {
    return kIndexedTag | static_cast<ColorRef>(index & 0xFF);
}
constexpr ColorRef rgbColor(int r, int g, int b) {
    return kRgbTag | (static_cast<ColorRef>(r & 0xFF) << 16) |
           (static_cast<ColorRef>(g & 0xFF) << 8) | static_cast<ColorRef>(b & 0xFF);
}
constexpr bool isDefaultColor(ColorRef c) { return c == kColorDefault; }
constexpr bool isIndexedColor(ColorRef c) { return (c & 0xFF000000u) == kIndexedTag; }
constexpr bool isRgbColor(ColorRef c) { return (c & 0xFF000000u) == kRgbTag; }
constexpr int colorIndex(ColorRef c) { return static_cast<int>(c & 0xFF); }

enum CellFlag : std::uint16_t {
    FlagBold      = 1 << 0,
    FlagDim       = 1 << 1,
    FlagItalic    = 1 << 2,
    FlagUnderline = 1 << 3,
    FlagBlink     = 1 << 4,
    FlagReverse   = 1 << 5,
    FlagHidden    = 1 << 6,
    FlagStrike    = 1 << 7,
};

struct Attrs {
    ColorRef fg = kColorDefault;
    ColorRef bg = kColorDefault;
    std::uint16_t flags = 0;

    bool operator==(const Attrs& o) const {
        return fg == o.fg && bg == o.bg && flags == o.flags;
    }
    bool operator!=(const Attrs& o) const { return !(*this == o); }
    bool plain() const { return fg == kColorDefault && bg == kColorDefault && flags == 0; }
};

struct Cell {
    char32_t cp = U' ';
    std::uint8_t width = 1; // 0 marks the second half of a double-width glyph
    Attrs attr;

    bool blank() const { return (cp == U' ' || cp == 0) && attr.plain(); }
};

struct Row {
    std::vector<Cell> cells;
    bool wrapped = false;
    // Set by OSC 133, so Apollo can jump between commands.
    bool promptStart = false;

    const Cell& at(int x) const;
    void ensure(int width);
    void trim();
};

class Screen {
public:
    Screen(int rows, int cols, int scrollbackLimit);

    // --- geometry ---------------------------------------------------------
    int rows() const { return rows_; }
    int cols() const { return cols_; }
    void resize(int rows, int cols);
    void setScrollbackLimit(int lines);

    // --- reading ----------------------------------------------------------
    const Row& row(int y) const;
    int historyLines() const { return static_cast<int>(history_.size()); }
    int totalLines() const { return historyLines() + rows_; }
    const Row& lineAt(int absolute) const;
    // Absolute line numbers carrying an OSC 133 prompt mark, oldest first.
    std::vector<int> promptLines() const;

    int cursorX() const { return x_; }
    int cursorY() const { return y_; }
    bool cursorVisible() const { return cursorVisible_; }
    bool alternate() const { return alternate_; }
    const Attrs& attrs() const { return attrs_; }
    Attrs& attrs() { return attrs_; }

    // --- writing ----------------------------------------------------------
    void put(char32_t cp);
    void carriageReturn();
    void lineFeed();
    void reverseLineFeed();
    void nextLine();
    void backspace();
    void tab(int count = 1);
    void backTab(int count = 1);

    void moveTo(int x, int y);
    void moveBy(int dx, int dy);
    void moveToColumn(int x);
    void moveToRow(int y);
    void saveCursor();
    void restoreCursor();

    void eraseDisplay(int mode); // 0 below, 1 above, 2 all, 3 all + history
    void eraseLine(int mode);    // 0 right, 1 left, 2 all
    void eraseChars(int count);
    void insertChars(int count);
    void deleteChars(int count);
    void insertLines(int count);
    void deleteLines(int count);
    void scrollUp(int count);
    void scrollDown(int count);
    void repeatLast(int count);

    void setScrollRegion(int top, int bottom); // 0-based, inclusive
    void setTabStop();
    void clearTabStop(bool all);
    void resetTabStops();
    void alignmentTest();
    void useAlternate(bool on, bool clearIt);
    void reset();
    void markPrompt();

    void setCursorVisible(bool visible) { cursorVisible_ = visible; }
    void setCursorStyle(int style) { cursorStyle_ = style; }
    int cursorStyle() const { return cursorStyle_; }

    // --- modes ------------------------------------------------------------
    bool autoWrap = true;
    bool originMode = false;
    bool insertMode = false;
    bool applicationCursor = false;
    bool applicationKeypad = false;
    bool bracketedPaste = false;
    bool reverseVideo = false;
    bool focusEvents = false;
    int mouseTracking = 0; // 0 off, 1000 click, 1002 drag, 1003 any motion
    bool mouseSgr = false;

    // --- things the UI wants to know about --------------------------------
    std::string title;
    std::string cwd;      // OSC 7
    // The pid of a shell on the far end of a connection, which reports it once
    // at startup. Nothing local needs this: it is how a remote directory is
    // found without installing anything over there.
    int shellPid = 0;
    bool bellPending = false;
    // OSC 133 C and D: the shell telling us a command started and finished.
    bool commandRunning = false;
    // Bumped whenever anything visible changes, so the UI can skip redraws.
    std::uint64_t revision() const { return revision_; }

private:
    Row& currentRow();
    void clearRow(Row& row, int from, int to);
    void scrollRegionUp(int count);
    void scrollRegionDown(int count);
    void pushHistory(Row&& row);
    void reflow(int newCols);
    int regionTop() const { return regionTop_; }
    int regionBottom() const { return regionBottom_; }
    void touch() { ++revision_; }

    int rows_;
    int cols_;
    int scrollbackLimit_;

    std::vector<Row> grid_;
    std::vector<Row> savedGrid_; // the primary screen, while the alternate is up
    std::deque<Row> history_;
    std::vector<bool> tabStops_;

    int x_ = 0, y_ = 0;
    bool pendingWrap_ = false;
    bool cursorVisible_ = true;
    int cursorStyle_ = 0;
    Attrs attrs_;

    struct SavedCursor {
        int x = 0, y = 0;
        Attrs attrs;
        bool originMode = false;
    };
    SavedCursor saved_;
    SavedCursor savedAlternate_;

    int regionTop_ = 0;
    int regionBottom_ = 0;
    bool alternate_ = false;
    char32_t lastPrinted_ = 0;
    std::uint64_t revision_ = 1;
};

int charWidth(char32_t cp);

// One code point as UTF-8.
void appendUtf8(std::string& out, char32_t cp);
std::string encodeUtf8(char32_t cp);

} // namespace apollo::term
