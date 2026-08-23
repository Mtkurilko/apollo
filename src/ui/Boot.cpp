#include "ui/Boot.h"

#include <algorithm>

#include "ui/Widgets.h"

namespace apollo::ui {

using namespace ftxui;

namespace {

// Assembled per letter so the rows cannot drift out of alignment.
const std::vector<std::string>& glyph(char letter) {
    static const std::vector<std::string> a = {
        " █████╗ ", "██╔══██╗", "███████║", "██╔══██║", "██║  ██║", "╚═╝  ╚═╝",
    };
    static const std::vector<std::string> p = {
        "██████╗ ", "██╔══██╗", "██████╔╝", "██╔═══╝ ", "██║     ", "╚═╝     ",
    };
    static const std::vector<std::string> o = {
        " ██████╗ ", "██╔═══██╗", "██║   ██║", "██║   ██║", "╚██████╔╝", " ╚═════╝ ",
    };
    static const std::vector<std::string> l = {
        "██╗     ", "██║     ", "██║     ", "██║     ", "███████╗", "╚══════╝",
    };
    static const std::vector<std::string> blank = {"", "", "", "", "", ""};

    switch (letter) {
        case 'A': return a;
        case 'P': return p;
        case 'O': return o;
        case 'L': return l;
        default:  return blank;
    }
}

constexpr int kWipeDone = 420;
constexpr int kLinesDone = 700;
constexpr int kHoldDone = 900;
constexpr int kStaticDone = 450; // when animation is off

// First `columns` display columns, so the wipe crosses multi-byte letters cleanly.
std::string prefixColumns(const std::string& row, int columns) {
    if (columns <= 0) return "";
    std::string out;
    int used = 0;
    for (std::size_t i = 0; i < row.size() && used < columns;) {
        const unsigned char lead = static_cast<unsigned char>(row[i]);
        std::size_t length = 1;
        if ((lead & 0xE0) == 0xC0) length = 2;
        else if ((lead & 0xF0) == 0xE0) length = 3;
        else if ((lead & 0xF8) == 0xF0) length = 4;
        length = std::min(length, row.size() - i);
        out.append(row, i, length);
        i += length;
        ++used;
    }
    return out;
}

} // namespace

const std::vector<std::string>& Boot::mark() {
    static const std::vector<std::string> rows = [] {
        std::vector<std::string> out(6);
        for (const char letter : std::string("APOLLO")) {
            const auto& piece = glyph(letter);
            for (std::size_t row = 0; row < out.size(); ++row) out[row] += piece[row];
        }
        return out;
    }();
    return rows;
}

int Boot::markWidth() { return displayWidth(mark().front()); }

void Boot::start(std::vector<Line> lines, bool animate) {
    lines_ = std::move(lines);
    animate_ = animate;
    started_ = std::chrono::steady_clock::now();
    running_ = true;
}

std::chrono::milliseconds Boot::elapsed() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_);
}

bool Boot::running() const {
    if (!running_) return false;
    return elapsed().count() < (animate_ ? kHoldDone : kStaticDone);
}

Element Boot::render(const Theme& theme, const DecorationSettings& decoration,
                     int width, int height) const {
    const int age = static_cast<int>(elapsed().count());

    // Too small for the block letters.
    if (width < markWidth() + 6 || height < 14) {
        return vbox({
            filler(),
            hbox({filler(), text("APOLLO") | bold | color(toFtx(theme.accent)), filler()}),
            hbox({filler(), text(APOLLO_VERSION) | color(toFtx(theme.muted)), filler()}),
            filler(),
        }) | bgcolor(toFtx(theme.bg));
    }

    const int full = markWidth();
    const int revealed = !animate_ ? full
                                   : std::clamp(age * full / std::max(1, kWipeDone), 0, full);

    Elements art;
    const auto& rows = mark();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(rows.size() - 1);
        const Rgb tint = theme.accent.mix(theme.accentAlt, t);
        art.push_back(hbox({
            filler(),
            text(prefixColumns(rows[i], revealed)) | color(toFtx(tint)),
            text(std::string(static_cast<std::size_t>(full - revealed), ' ')),
            filler(),
        }));
    }

    Elements facts;
    const int shown =
        !animate_ ? static_cast<int>(lines_.size())
                  : std::clamp((age - kWipeDone) * static_cast<int>(lines_.size() + 1) /
                                   std::max(1, kLinesDone - kWipeDone),
                               0, static_cast<int>(lines_.size()));

    const int valueRoom = std::max(8, width / 2);
    std::size_t labelWidth = 0;
    std::size_t valueWidth = 0;
    for (const auto& line : lines_) {
        labelWidth = std::max(labelWidth, line.label.size());
        valueWidth = std::max(valueWidth, elide(line.value, valueRoom).size());
    }

    for (int i = 0; i < shown && i < static_cast<int>(lines_.size()); ++i) {
        std::string label = lines_[static_cast<std::size_t>(i)].label;
        label.resize(labelWidth, ' ');
        std::string value = elide(lines_[static_cast<std::size_t>(i)].value, valueRoom);
        value.resize(std::max(value.size(), valueWidth), ' ');

        facts.push_back(hbox({
            filler(),
            text(label + "  ") | color(toFtx(theme.muted)),
            text(value) | color(toFtx(theme.fg)),
            filler(),
        }));
    }
    while (facts.size() < lines_.size()) facts.push_back(text(""));

    Elements body{
        filler(),
        vbox(std::move(art)),
        text(""),
        hbox({filler(),
              text("a terminal workspace  ·  " APOLLO_VERSION) | color(toFtx(theme.muted)),
              filler()}),
        text(""),
        vbox(std::move(facts)),
        text(""),
        hbox({filler(), text("press any key") | color(toFtx(theme.border)), filler()}),
        filler(),
    };
    (void)decoration;

    return vbox(std::move(body)) | bgcolor(toFtx(theme.bg));
}

} // namespace apollo::ui
