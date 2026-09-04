#include "ui/TerminalView.h"

#include <algorithm>

#include "ui/Widgets.h"

namespace apollo::ui {

using namespace ftxui;
using namespace apollo::term;

namespace {

struct Style {
    Color fg;
    Color bg;
    std::uint16_t flags = 0;

    bool operator==(const Style& o) const {
        return fg == o.fg && bg == o.bg && flags == o.flags;
    }
};

Element decorate(Element element, const Style& style) {
    element = std::move(element) | color(style.fg) | bgcolor(style.bg);
    if (style.flags & FlagBold) element = bold(std::move(element));
    if (style.flags & FlagDim) element = dim(std::move(element));
    if (style.flags & FlagItalic) element = italic(std::move(element));
    if (style.flags & FlagUnderline) element = underlined(std::move(element));
    if (style.flags & FlagStrike) element = strikethrough(std::move(element));
    if (style.flags & FlagBlink) element = blink(std::move(element));
    return element;
}

std::vector<std::pair<int, int>> matchRanges(const std::string& haystack,
                                             const std::string& needle) {
    std::vector<std::pair<int, int>> ranges;
    if (needle.empty() || haystack.empty()) return ranges;

    std::string a = haystack, b = needle;
    std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return std::tolower(c); });
    std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return std::tolower(c); });

    for (std::size_t at = a.find(b); at != std::string::npos; at = a.find(b, at + 1)) {
        ranges.emplace_back(static_cast<int>(at), static_cast<int>(at + b.size()));
    }
    return ranges;
}

Element withCursor(Element cell, const std::string& shape, bool blink) {
    if (shape == "bar") return blink ? focusCursorBarBlinking(std::move(cell))
                                     : focusCursorBar(std::move(cell));
    if (shape == "underline") return blink ? focusCursorUnderlineBlinking(std::move(cell))
                                           : focusCursorUnderline(std::move(cell));
    return blink ? focusCursorBlockBlinking(std::move(cell)) : focusCursorBlock(std::move(cell));
}

} // namespace

Element renderTerminal(const Session& session,
                       const Theme& theme,
                       const TerminalSettings& settings,
                       const TerminalViewOptions& options) {
    const Screen& screen = session.screen();
    const int rows = std::min(options.height, screen.rows());
    if (rows <= 0) return text("");

    const int bottom = screen.totalLines() - session.scrollOffset();
    const int top = bottom - rows;

    int selFromLine = 0, selFromCol = 0, selToLine = -1, selToCol = 0;
    const bool hasSelection = !session.selection.empty();
    if (hasSelection) {
        session.selection.normalized(selFromLine, selFromCol, selToLine, selToCol);
    }

    const bool liveCursor = options.focused && screen.cursorVisible() &&
                            session.scrollOffset() == 0;
    const bool ghostCursor = !options.focused && screen.cursorVisible() &&
                             session.scrollOffset() == 0;
    const int cursorLine = screen.totalLines() - screen.rows() + screen.cursorY();

    const Color defaultFg = toFtx(theme.fg);
    const Color defaultBg = toFtx(theme.bg);
    const Color selectionBg = toFtx(theme.selection);
    const Color searchBg = toFtx(theme.warning);
    const Color searchFg = toFtx(theme.bg);

    Elements lines;
    lines.reserve(static_cast<std::size_t>(rows));

    for (int y = 0; y < rows; ++y) {
        const int absolute = top + y;
        if (absolute < 0) { lines.push_back(text("")); continue; }

        const Row& row = screen.lineAt(absolute);
        const auto highlights =
            options.searchTerm.empty()
                ? std::vector<std::pair<int, int>>{}
                : matchRanges(session.lineText(absolute), options.searchTerm);

        Elements runs;
        std::string buffer;
        Style current{defaultFg, defaultBg, 0};
        bool started = false;

        const auto flush = [&] {
            if (!started || buffer.empty()) { buffer.clear(); return; }
            runs.push_back(decorate(text(buffer), current));
            buffer.clear();
        };

        const int width = screen.cols();
        for (int x = 0; x < width; ++x) {
            const Cell& cell = row.at(x);
            if (cell.width == 0 && cell.cp == 0) continue; // tail of a wide glyph

            Style style;
            style.flags = cell.attr.flags;
            style.fg = resolve(cell.attr.fg, theme, true);
            style.bg = resolve(cell.attr.bg, theme, false);

            const bool reversed =
                ((cell.attr.flags & FlagReverse) != 0) != screen.reverseVideo;
            if (reversed) std::swap(style.fg, style.bg);
            if (cell.attr.flags & FlagHidden) style.fg = style.bg;

            const bool selected = hasSelection && absolute >= selFromLine && absolute <= selToLine &&
                                  (absolute != selFromLine || x >= selFromCol) &&
                                  (absolute != selToLine || x < selToCol);
            if (selected) style.bg = selectionBg;

            for (const auto& [from, to] : highlights) {
                if (x >= from && x < to) {
                    style.bg = searchBg;
                    style.fg = searchFg;
                    break;
                }
            }

            const bool onCursor = absolute == cursorLine && x == screen.cursorX();
            if (onCursor && ghostCursor) std::swap(style.fg, style.bg);

            // Cursor gets its own run. Merge it into a neighbor and it's gone.
            const bool isCursor = onCursor && (liveCursor || ghostCursor);
            if (!started || !(style == current) || isCursor) {
                flush();
                current = style;
                started = true;
            }
            appendUtf8(buffer, cell.cp == 0 ? U' ' : cell.cp);
            if (isCursor) {
                const bool live = onCursor && liveCursor;
                std::string glyph;
                glyph.swap(buffer);
                Element painted = decorate(text(glyph), current);
                if (live) {
                    painted = withCursor(std::move(painted), settings.cursor,
                                         settings.cursorBlink);
                }
                runs.push_back(std::move(painted));
                started = false;
            }
        }
        flush();

        if (runs.empty()) runs.push_back(text("") | bgcolor(defaultBg));
        lines.push_back(hbox(std::move(runs)));
    }

    return vbox(std::move(lines)) | bgcolor(defaultBg);
}

Element renderTerminalStatus(const Session& session, const Theme& theme, int width) {
    const Screen& screen = session.screen();

    Elements parts;
    if (session.scrolled()) {
        const int position = screen.historyLines() - session.scrollOffset();
        const int percent = screen.historyLines() > 0
                                ? position * 100 / screen.historyLines()
                                : 100;
        parts.push_back(text(" scrollback " + std::to_string(percent) + "% ") |
                        bgcolor(toFtx(theme.warning)) | color(toFtx(theme.bg)));
        parts.push_back(text("  "));
        parts.push_back(text("End to return") | color(toFtx(theme.muted)));
    } else if (!session.running()) {
        parts.push_back(text(" exited " + std::to_string(session.exitCode()) + " ") |
                        bgcolor(toFtx(theme.error)) | color(toFtx(theme.bg)));
    }

    if (parts.empty()) return text("");
    parts.push_back(filler());
    return hbox(std::move(parts)) | size(WIDTH, LESS_THAN, std::max(width, 1));
}

} // namespace apollo::ui
