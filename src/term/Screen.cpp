#include "term/Screen.h"

#include <wchar.h>

#include <algorithm>

namespace apollo::term {
namespace {

const Cell kBlank{};

} // namespace

int charWidth(char32_t cp) {
    if (cp == 0) return 0;
    if (cp < 32 || (cp >= 0x7F && cp < 0xA0)) return 0;

    const int width = ::wcwidth(static_cast<wchar_t>(cp));
    if (width >= 0) return width;

    // wcwidth returns -1 for anything the locale can't name. Call those one column.
    return 1;
}

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) { out.push_back(static_cast<char>(cp)); return; }
    if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        return;
    }
    if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        return;
    }
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
}

const Cell& Row::at(int x) const {
    if (x < 0 || x >= static_cast<int>(cells.size())) return kBlank;
    return cells[static_cast<std::size_t>(x)];
}

void Row::ensure(int width) {
    if (static_cast<int>(cells.size()) < width) cells.resize(static_cast<std::size_t>(width));
}

void Row::trim() {
    while (!cells.empty() && cells.back().blank()) cells.pop_back();
    cells.shrink_to_fit();
}

Screen::Screen(int rows, int cols, int scrollbackLimit)
    : rows_(std::max(1, rows)),
      cols_(std::max(1, cols)),
      scrollbackLimit_(std::max(0, scrollbackLimit)) {
    grid_.resize(static_cast<std::size_t>(rows_));
    for (auto& row : grid_) row.ensure(cols_);
    regionBottom_ = rows_ - 1;
    resetTabStops();
}

void Screen::setScrollbackLimit(int lines) {
    scrollbackLimit_ = std::max(0, lines);
    while (static_cast<int>(history_.size()) > scrollbackLimit_) history_.pop_front();
}

const Row& Screen::row(int y) const {
    static const Row empty;
    if (y < 0 || y >= static_cast<int>(grid_.size())) return empty;
    return grid_[static_cast<std::size_t>(y)];
}

const Row& Screen::lineAt(int absolute) const {
    static const Row empty;
    if (absolute < 0) return empty;
    if (absolute < historyLines()) return history_[static_cast<std::size_t>(absolute)];
    return row(absolute - historyLines());
}

std::vector<int> Screen::promptLines() const {
    std::vector<int> marks;
    for (int i = 0; i < historyLines(); ++i) {
        if (history_[static_cast<std::size_t>(i)].promptStart) marks.push_back(i);
    }
    for (int y = 0; y < rows_; ++y) {
        if (grid_[static_cast<std::size_t>(y)].promptStart) marks.push_back(historyLines() + y);
    }
    return marks;
}

Row& Screen::currentRow() {
    y_ = std::clamp(y_, 0, rows_ - 1);
    Row& row = grid_[static_cast<std::size_t>(y_)];
    row.ensure(cols_);
    return row;
}

void Screen::clearRow(Row& row, int from, int to) {
    row.ensure(cols_);
    for (int x = std::max(0, from); x <= std::min(to, cols_ - 1); ++x) {
        Cell& cell = row.cells[static_cast<std::size_t>(x)];
        cell.cp = U' ';
        cell.width = 1;
        cell.attr = Attrs{kColorDefault, attrs_.bg, 0};
    }
}

void Screen::resetTabStops() {
    tabStops_.assign(static_cast<std::size_t>(std::max(cols_, 1)), false);
    for (int x = 8; x < cols_; x += 8) tabStops_[static_cast<std::size_t>(x)] = true;
}

void Screen::markPrompt() {
    currentRow().promptStart = true;
    touch();
}

// --- writing ---------------------------------------------------------------

void Screen::put(char32_t cp) {
    const int width = charWidth(cp);
    if (width == 0) return; // combining marks are dropped rather than misplaced

    if (pendingWrap_ && autoWrap) {
        currentRow().wrapped = true;
        pendingWrap_ = false;
        x_ = 0;
        lineFeed();
    }
    if (x_ + width > cols_) {
        if (!autoWrap) x_ = cols_ - width;
        else {
            currentRow().wrapped = true;
            x_ = 0;
            lineFeed();
        }
    }
    if (x_ < 0) x_ = 0;

    Row& row = currentRow();
    if (insertMode) {
        row.ensure(cols_);
        row.cells.insert(row.cells.begin() + x_, static_cast<std::size_t>(width), Cell{});
        row.cells.resize(static_cast<std::size_t>(cols_));
    }

    Cell& cell = row.cells[static_cast<std::size_t>(x_)];
    cell.cp = cp;
    cell.width = static_cast<std::uint8_t>(width);
    cell.attr = attrs_;

    if (width == 2 && x_ + 1 < cols_) {
        Cell& tail = row.cells[static_cast<std::size_t>(x_ + 1)];
        tail.cp = 0;
        tail.width = 0;
        tail.attr = attrs_;
    }

    lastPrinted_ = cp;
    x_ += width;
    if (x_ >= cols_) {
        x_ = cols_ - 1;
        pendingWrap_ = true;
    }
    touch();
}

void Screen::repeatLast(int count) {
    if (lastPrinted_ == 0) return;
    for (int i = 0; i < std::max(1, count); ++i) put(lastPrinted_);
}

void Screen::carriageReturn() {
    x_ = 0;
    pendingWrap_ = false;
    touch();
}

void Screen::lineFeed() {
    pendingWrap_ = false;
    if (y_ == regionBottom_) scrollRegionUp(1);
    else if (y_ < rows_ - 1) ++y_;
    touch();
}

void Screen::reverseLineFeed() {
    pendingWrap_ = false;
    if (y_ == regionTop_) scrollRegionDown(1);
    else if (y_ > 0) --y_;
    touch();
}

void Screen::nextLine() {
    carriageReturn();
    lineFeed();
}

void Screen::backspace() {
    pendingWrap_ = false;
    if (x_ > 0) --x_;
    touch();
}

void Screen::tab(int count) {
    for (int i = 0; i < std::max(1, count); ++i) {
        int next = x_ + 1;
        while (next < cols_ - 1 && !tabStops_[static_cast<std::size_t>(next)]) ++next;
        x_ = std::min(next, cols_ - 1);
    }
    pendingWrap_ = false;
    touch();
}

void Screen::backTab(int count) {
    for (int i = 0; i < std::max(1, count); ++i) {
        int previous = x_ - 1;
        while (previous > 0 && !tabStops_[static_cast<std::size_t>(previous)]) --previous;
        x_ = std::max(previous, 0);
    }
    touch();
}

void Screen::setTabStop() {
    if (x_ >= 0 && x_ < static_cast<int>(tabStops_.size())) {
        tabStops_[static_cast<std::size_t>(x_)] = true;
    }
}

void Screen::clearTabStop(bool all) {
    if (all) { tabStops_.assign(tabStops_.size(), false); return; }
    if (x_ >= 0 && x_ < static_cast<int>(tabStops_.size())) {
        tabStops_[static_cast<std::size_t>(x_)] = false;
    }
}

// --- cursor ----------------------------------------------------------------

void Screen::moveTo(int x, int y) {
    const int top = originMode ? regionTop_ : 0;
    const int bottom = originMode ? regionBottom_ : rows_ - 1;
    x_ = std::clamp(x, 0, cols_ - 1);
    y_ = std::clamp(y + top, top, bottom);
    pendingWrap_ = false;
    touch();
}

void Screen::moveBy(int dx, int dy) {
    // Relative moves stay inside the scroll region without scrolling it.
    const int top = y_ >= regionTop_ ? regionTop_ : 0;
    const int bottom = y_ <= regionBottom_ ? regionBottom_ : rows_ - 1;
    x_ = std::clamp(x_ + dx, 0, cols_ - 1);
    y_ = std::clamp(y_ + dy, top, bottom);
    pendingWrap_ = false;
    touch();
}

void Screen::moveToColumn(int x) {
    x_ = std::clamp(x, 0, cols_ - 1);
    pendingWrap_ = false;
    touch();
}

void Screen::moveToRow(int y) {
    const int top = originMode ? regionTop_ : 0;
    const int bottom = originMode ? regionBottom_ : rows_ - 1;
    y_ = std::clamp(y + top, top, bottom);
    pendingWrap_ = false;
    touch();
}

void Screen::saveCursor() {
    SavedCursor& into = alternate_ ? savedAlternate_ : saved_;
    into = {x_, y_, attrs_, originMode};
}

void Screen::restoreCursor() {
    const SavedCursor& from = alternate_ ? savedAlternate_ : saved_;
    x_ = std::clamp(from.x, 0, cols_ - 1);
    y_ = std::clamp(from.y, 0, rows_ - 1);
    attrs_ = from.attrs;
    originMode = from.originMode;
    pendingWrap_ = false;
    touch();
}

// --- erasing and editing ---------------------------------------------------

void Screen::eraseDisplay(int mode) {
    switch (mode) {
        case 0:
            clearRow(currentRow(), x_, cols_ - 1);
            for (int y = y_ + 1; y < rows_; ++y) clearRow(grid_[static_cast<std::size_t>(y)], 0, cols_ - 1);
            break;
        case 1:
            clearRow(currentRow(), 0, x_);
            for (int y = 0; y < y_; ++y) clearRow(grid_[static_cast<std::size_t>(y)], 0, cols_ - 1);
            break;
        case 3:
            history_.clear();
            [[fallthrough]];
        case 2:
            for (auto& row : grid_) {
                clearRow(row, 0, cols_ - 1);
                row.wrapped = false;
                row.promptStart = false;
            }
            break;
        default:
            return;
    }
    touch();
}

void Screen::eraseLine(int mode) {
    Row& row = currentRow();
    if (mode == 0) clearRow(row, x_, cols_ - 1);
    else if (mode == 1) clearRow(row, 0, x_);
    else if (mode == 2) { clearRow(row, 0, cols_ - 1); row.wrapped = false; }
    touch();
}

void Screen::eraseChars(int count) {
    clearRow(currentRow(), x_, x_ + std::max(1, count) - 1);
    touch();
}

void Screen::insertChars(int count) {
    Row& row = currentRow();
    row.ensure(cols_);
    const int n = std::clamp(count, 1, cols_ - x_);
    row.cells.insert(row.cells.begin() + x_, static_cast<std::size_t>(n),
                     Cell{U' ', 1, Attrs{kColorDefault, attrs_.bg, 0}});
    row.cells.resize(static_cast<std::size_t>(cols_));
    touch();
}

void Screen::deleteChars(int count) {
    Row& row = currentRow();
    row.ensure(cols_);
    const int n = std::clamp(count, 1, cols_ - x_);
    row.cells.erase(row.cells.begin() + x_, row.cells.begin() + x_ + n);
    row.cells.resize(static_cast<std::size_t>(cols_),
                     Cell{U' ', 1, Attrs{kColorDefault, attrs_.bg, 0}});
    touch();
}

void Screen::insertLines(int count) {
    if (y_ < regionTop_ || y_ > regionBottom_) return;

    const int n = std::clamp(count, 1, regionBottom_ - y_ + 1);
    for (int i = 0; i < n; ++i) {
        grid_.erase(grid_.begin() + regionBottom_);
        Row fresh;
        fresh.ensure(cols_);
        clearRow(fresh, 0, cols_ - 1);
        grid_.insert(grid_.begin() + y_, std::move(fresh));
    }
    touch();
}

void Screen::deleteLines(int count) {
    if (y_ < regionTop_ || y_ > regionBottom_) return;

    const int n = std::clamp(count, 1, regionBottom_ - y_ + 1);
    for (int i = 0; i < n; ++i) {
        grid_.erase(grid_.begin() + y_);
        Row fresh;
        fresh.ensure(cols_);
        clearRow(fresh, 0, cols_ - 1);
        grid_.insert(grid_.begin() + regionBottom_, std::move(fresh));
    }
    touch();
}

void Screen::pushHistory(Row&& row) {
    if (scrollbackLimit_ == 0 || alternate_) return;

    row.trim();
    history_.push_back(std::move(row));
    while (static_cast<int>(history_.size()) > scrollbackLimit_) history_.pop_front();
}

void Screen::scrollRegionUp(int count) {
    const int n = std::clamp(count, 1, regionBottom_ - regionTop_ + 1);
    for (int i = 0; i < n; ++i) {
        if (regionTop_ == 0 && regionBottom_ == rows_ - 1) {
            pushHistory(std::move(grid_[0]));
        }
        grid_.erase(grid_.begin() + regionTop_);
        Row fresh;
        fresh.ensure(cols_);
        clearRow(fresh, 0, cols_ - 1);
        grid_.insert(grid_.begin() + regionBottom_, std::move(fresh));
    }
    touch();
}

void Screen::scrollRegionDown(int count) {
    const int n = std::clamp(count, 1, regionBottom_ - regionTop_ + 1);
    for (int i = 0; i < n; ++i) {
        grid_.erase(grid_.begin() + regionBottom_);
        Row fresh;
        fresh.ensure(cols_);
        clearRow(fresh, 0, cols_ - 1);
        grid_.insert(grid_.begin() + regionTop_, std::move(fresh));
    }
    touch();
}

void Screen::scrollUp(int count) { scrollRegionUp(count); }
void Screen::scrollDown(int count) { scrollRegionDown(count); }

void Screen::setScrollRegion(int top, int bottom) {
    if (top >= bottom) { regionTop_ = 0; regionBottom_ = rows_ - 1; }
    else {
        regionTop_ = std::clamp(top, 0, rows_ - 1);
        regionBottom_ = std::clamp(bottom, regionTop_, rows_ - 1);
    }
    moveTo(0, 0);
}

void Screen::alignmentTest() {
    for (auto& row : grid_) {
        row.ensure(cols_);
        for (auto& cell : row.cells) { cell.cp = U'E'; cell.width = 1; cell.attr = Attrs{}; }
        row.wrapped = false;
    }
    moveTo(0, 0);
}

void Screen::useAlternate(bool on, bool clearIt) {
    if (on == alternate_) return;

    if (on) {
        savedGrid_ = grid_;
        alternate_ = true;
        if (clearIt) {
            for (auto& row : grid_) {
                row.cells.assign(static_cast<std::size_t>(cols_), Cell{});
                row.wrapped = false;
                row.promptStart = false;
            }
        }
    } else {
        alternate_ = false;
        grid_ = std::move(savedGrid_);
        savedGrid_.clear();
        grid_.resize(static_cast<std::size_t>(rows_));
        for (auto& row : grid_) row.ensure(cols_);
    }
    regionTop_ = 0;
    regionBottom_ = rows_ - 1;
    touch();
}

void Screen::reset() {
    attrs_ = Attrs{};
    autoWrap = true;
    originMode = false;
    insertMode = false;
    applicationCursor = false;
    applicationKeypad = false;
    bracketedPaste = false;
    reverseVideo = false;
    focusEvents = false;
    mouseTracking = 0;
    mouseSgr = false;
    cursorVisible_ = true;
    cursorStyle_ = 0;
    pendingWrap_ = false;
    if (alternate_) useAlternate(false, false);
    regionTop_ = 0;
    regionBottom_ = rows_ - 1;
    resetTabStops();
    eraseDisplay(2);
    moveTo(0, 0);
}

// --- resizing --------------------------------------------------------------

void Screen::reflow(int newCols) {
    struct Logical {
        std::vector<Cell> cells;
        bool promptStart = false;
    };

    std::vector<Logical> logical;
    const auto absorb = [&](const Row& row, bool startsNew) {
        if (startsNew || logical.empty()) {
            logical.push_back(Logical{});
            logical.back().promptStart = row.promptStart;
        }
        auto& into = logical.back().cells;
        std::vector<Cell> trimmed = row.cells;
        while (!trimmed.empty() && trimmed.back().blank()) trimmed.pop_back();
        into.insert(into.end(), trimmed.begin(), trimmed.end());
    };

    bool continuing = false;
    for (const auto& row : history_) {
        absorb(row, !continuing);
        continuing = row.wrapped;
    }

    int cursorLogical = -1;
    int cursorOffset = 0;
    for (int y = 0; y < static_cast<int>(grid_.size()); ++y) {
        const Row& row = grid_[static_cast<std::size_t>(y)];
        absorb(row, !continuing);
        if (y == y_) {
            cursorLogical = static_cast<int>(logical.size()) - 1;
            cursorOffset = static_cast<int>(logical.back().cells.size()) -
                           static_cast<int>([&] {
                               std::vector<Cell> t = row.cells;
                               while (!t.empty() && t.back().blank()) t.pop_back();
                               return t.size();
                           }()) + x_;
        }
        continuing = row.wrapped;
    }

    while (logical.size() > 1 && logical.back().cells.empty()) {
        if (cursorLogical == static_cast<int>(logical.size()) - 1) break;
        logical.pop_back();
    }

    std::vector<Row> laidOut;
    int cursorRow = 0, cursorCol = 0;
    for (std::size_t i = 0; i < logical.size(); ++i) {
        const auto& source = logical[i];
        std::size_t at = 0;
        bool first = true;
        do {
            Row row;
            const std::size_t take = std::min(static_cast<std::size_t>(newCols),
                                              source.cells.size() - at);
            row.cells.assign(source.cells.begin() + static_cast<std::ptrdiff_t>(at),
                             source.cells.begin() + static_cast<std::ptrdiff_t>(at + take));
            row.promptStart = first && source.promptStart;
            at += take;
            row.wrapped = at < source.cells.size();

            if (static_cast<int>(i) == cursorLogical &&
                cursorOffset >= static_cast<int>(at - take) &&
                (cursorOffset < static_cast<int>(at) || !row.wrapped)) {
                cursorRow = static_cast<int>(laidOut.size());
                cursorCol = cursorOffset - static_cast<int>(at - take);
            }
            laidOut.push_back(std::move(row));
            first = false;
        } while (at < source.cells.size());
    }

    const int keep = std::min(static_cast<int>(laidOut.size()), rows_);
    const int historyEnd = static_cast<int>(laidOut.size()) - keep;

    history_.clear();
    for (int i = 0; i < historyEnd; ++i) {
        Row row = std::move(laidOut[static_cast<std::size_t>(i)]);
        row.trim();
        history_.push_back(std::move(row));
    }
    while (static_cast<int>(history_.size()) > scrollbackLimit_) history_.pop_front();

    grid_.assign(static_cast<std::size_t>(rows_), Row{});
    for (int i = 0; i < keep; ++i) {
        grid_[static_cast<std::size_t>(i)] = std::move(laidOut[static_cast<std::size_t>(historyEnd + i)]);
    }
    for (auto& row : grid_) row.ensure(newCols);

    y_ = std::clamp(cursorRow - historyEnd, 0, rows_ - 1);
    x_ = std::clamp(cursorCol, 0, newCols - 1);
}

void Screen::resize(int rows, int cols) {
    rows = std::max(1, rows);
    cols = std::max(1, cols);
    if (rows == rows_ && cols == cols_) return;

    const bool widthChanged = cols != cols_;
    rows_ = rows;

    if (alternate_) {
        // Full screen programs repaint on SIGWINCH. Reflowing the alt buffer just flickers.
        cols_ = cols;
        grid_.assign(static_cast<std::size_t>(rows_), Row{});
        for (auto& row : grid_) row.ensure(cols_);
        savedGrid_.resize(static_cast<std::size_t>(rows_));
        for (auto& row : savedGrid_) row.ensure(cols_);
    } else if (widthChanged) {
        // reflow() rebuilds history and grid, and sizes the grid itself.
        reflow(cols);
        cols_ = cols;
    } else {
        while (static_cast<int>(grid_.size()) > rows_) {
            if (y_ > 0) { pushHistory(std::move(grid_.front())); grid_.erase(grid_.begin()); --y_; }
            else grid_.pop_back();
        }
        grid_.resize(static_cast<std::size_t>(rows_));
        for (auto& row : grid_) row.ensure(cols_);
    }

    x_ = std::clamp(x_, 0, cols_ - 1);
    y_ = std::clamp(y_, 0, rows_ - 1);
    regionTop_ = 0;
    regionBottom_ = rows_ - 1;
    pendingWrap_ = false;
    resetTabStops();
    touch();
}

} // namespace apollo::term
