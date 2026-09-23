#include "ui/Widgets.h"

#include <algorithm>
#include <cctype>

namespace apollo::ui {

using namespace ftxui;

Color toFtx(const Rgb& color) { return Color::RGB(color.r, color.g, color.b); }

Color resolve(term::ColorRef color, const Theme& theme, bool foreground) {
    if (term::isDefaultColor(color)) {
        return foreground ? toFtx(theme.fg) : toFtx(theme.bg);
    }
    if (term::isIndexedColor(color)) {
        const int index = term::colorIndex(color);
        if (index < 16) return toFtx(theme.ansi[static_cast<std::size_t>(index)]);
        return Color::Palette256(static_cast<Color::Palette256>(index));
    }
    return Color::RGB(static_cast<std::uint8_t>((color >> 16) & 0xFF),
                      static_cast<std::uint8_t>((color >> 8) & 0xFF),
                      static_cast<std::uint8_t>(color & 0xFF));
}

BorderStyle borderStyle(const std::string& name) {
    if (name == "light") return LIGHT;
    if (name == "heavy") return HEAVY;
    if (name == "double") return DOUBLE;
    if (name == "none") return EMPTY;
    return ROUNDED;
}

int displayWidth(const std::string& text) {
    int width = 0;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char byte = static_cast<unsigned char>(text[i]);
        std::size_t length = 1;
        char32_t cp = byte;
        if ((byte & 0xE0) == 0xC0) { length = 2; cp = byte & 0x1F; }
        else if ((byte & 0xF0) == 0xE0) { length = 3; cp = byte & 0x0F; }
        else if ((byte & 0xF8) == 0xF0) { length = 4; cp = byte & 0x07; }
        for (std::size_t k = 1; k < length && i + k < text.size(); ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        }
        width += term::charWidth(cp);
        i += length;
    }
    return width;
}

std::string elide(const std::string& text, int width) {
    if (width <= 0) return "";
    if (displayWidth(text) <= width) return text;
    if (width == 1) return "…";

    std::string out;
    int used = 0;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char byte = static_cast<unsigned char>(text[i]);
        std::size_t length = 1;
        char32_t cp = byte;
        if ((byte & 0xE0) == 0xC0) { length = 2; cp = byte & 0x1F; }
        else if ((byte & 0xF0) == 0xE0) { length = 3; cp = byte & 0x0F; }
        else if ((byte & 0xF8) == 0xF0) { length = 4; cp = byte & 0x07; }
        for (std::size_t k = 1; k < length && i + k < text.size(); ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        }
        const int step = term::charWidth(cp);
        if (used + step > width - 1) break;
        out.append(text, i, length);
        used += step;
        i += length;
    }
    return out + "…";
}

// --- LineEdit --------------------------------------------------------------

namespace {

// Whole characters, or an arrow key strands the cursor mid-sequence.
int stepLeft(const std::string& text, int at) {
    if (at <= 0) return 0;
    --at;
    while (at > 0 && (static_cast<unsigned char>(text[static_cast<std::size_t>(at)]) & 0xC0) == 0x80) --at;
    return at;
}

int stepRight(const std::string& text, int at) {
    const int size = static_cast<int>(text.size());
    if (at >= size) return size;
    ++at;
    while (at < size && (static_cast<unsigned char>(text[static_cast<std::size_t>(at)]) & 0xC0) == 0x80) ++at;
    return at;
}

bool isWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

} // namespace

void LineEdit::set(const std::string& value) {
    text = value;
    cursor = static_cast<int>(text.size());
}

void LineEdit::clear() {
    text.clear();
    cursor = 0;
}

bool LineEdit::onKey(const KeyChord& chord, const std::string& raw) {
    const auto eraseRange = [&](int from, int to) {
        from = std::clamp(from, 0, static_cast<int>(text.size()));
        to = std::clamp(to, from, static_cast<int>(text.size()));
        text.erase(static_cast<std::size_t>(from), static_cast<std::size_t>(to - from));
        cursor = from;
    };

    if (chord.mods == ModCtrl) {
        if (chord.key == "a") { cursor = 0; return true; }
        if (chord.key == "e") { cursor = static_cast<int>(text.size()); return true; }
        if (chord.key == "u") { eraseRange(0, cursor); return true; }
        if (chord.key == "k") { eraseRange(cursor, static_cast<int>(text.size())); return true; }
        if (chord.key == "w") {
            int at = cursor;
            while (at > 0 && !isWordChar(text[static_cast<std::size_t>(at - 1)])) --at;
            while (at > 0 && isWordChar(text[static_cast<std::size_t>(at - 1)])) --at;
            eraseRange(at, cursor);
            return true;
        }
        return false;
    }

    if (chord.mods == ModNone || chord.mods == ModShift) {
        if (chord.key == "left")  { cursor = stepLeft(text, cursor); return true; }
        if (chord.key == "right") { cursor = stepRight(text, cursor); return true; }
        if (chord.key == "home")  { cursor = 0; return true; }
        if (chord.key == "end")   { cursor = static_cast<int>(text.size()); return true; }
        if (chord.key == "backspace") { eraseRange(stepLeft(text, cursor), cursor); return true; }
        if (chord.key == "delete") { eraseRange(cursor, stepRight(text, cursor)); return true; }
    }

    // Anything printable, whole UTF-8 characters included, goes in as-is.
    if (!raw.empty() && static_cast<unsigned char>(raw[0]) >= 0x20 &&
        static_cast<unsigned char>(raw[0]) != 0x7F) {
        text.insert(static_cast<std::size_t>(cursor), raw);
        cursor += static_cast<int>(raw.size());
        return true;
    }
    return false;
}

Element LineEdit::render(const Theme& theme, const std::string& placeholder, bool focused,
                         bool mask, int width) const {
    const std::string shown = mask ? std::string(this->text.size(), '*') : this->text;

    if (shown.empty()) {
        Elements parts;
        if (focused) {
            parts.push_back(ftxui::text(" ") | bgcolor(toFtx(theme.accent)));
        }
        if (!placeholder.empty()) {
            parts.push_back(ftxui::text(placeholder) | color(toFtx(theme.muted)));
        }
        if (parts.empty()) parts.push_back(ftxui::text(""));
        return hbox(std::move(parts));
    }

    const int at = std::clamp(cursor, 0, static_cast<int>(shown.size()));
    std::string before = shown.substr(0, static_cast<std::size_t>(at));
    std::string after = shown.substr(static_cast<std::size_t>(at));

    // Too long for its room: scroll so the cursor stays in sight, and mark
    // whatever is cut off with an ellipsis.
    if (width > 0 && displayWidth(shown) + 1 > width) {
        const int keep = std::max(1, width - 2);
        if (displayWidth(before) > keep) {
            while (!before.empty() && displayWidth(before) > keep - 1) {
                std::size_t drop = 1;
                while (drop < before.size() &&
                       (static_cast<unsigned char>(before[drop]) & 0xC0) == 0x80) {
                    ++drop;
                }
                before.erase(0, drop);
            }
            before = "…" + before;
        }
        const int room = std::max(1, width - displayWidth(before) - 1);
        if (displayWidth(after) > room) after = elide(after, room);
    }

    Elements parts;
    parts.push_back(ftxui::text(before) | color(toFtx(theme.fg)));
    if (focused) {
        // Cursor is a block drawn over whatever character it sits on.
        std::size_t bytes = 1;
        if (!after.empty()) {
            const unsigned char lead = static_cast<unsigned char>(after[0]);
            if ((lead & 0xE0) == 0xC0) bytes = 2;
            else if ((lead & 0xF0) == 0xE0) bytes = 3;
            else if ((lead & 0xF8) == 0xF0) bytes = 4;
        }
        const std::string under = after.empty() ? " " : after.substr(0, bytes);
        parts.push_back(ftxui::text(under) | bgcolor(toFtx(theme.accent)) | color(toFtx(theme.bg)));
        if (after.size() > bytes) {
            parts.push_back(ftxui::text(after.substr(bytes)) | color(toFtx(theme.fg)));
        }
    } else if (!after.empty()) {
        parts.push_back(ftxui::text(after) | color(toFtx(theme.fg)));
    }
    return hbox(std::move(parts));
}

std::string elidePath(const std::string& path, int width) {
    if (width <= 0) return "";
    if (displayWidth(path) <= width) return path;
    if (width <= 2) return "…";

    std::size_t at = 0;
    while (at < path.size()) {
        const std::size_t slash = path.find('/', at + 1);
        if (slash == std::string::npos) break;
        if (displayWidth(path.substr(slash)) + 1 <= width) {
            at = slash;
            break;
        }
        at = slash;
    }

    const std::string tail = path.substr(at);
    if (displayWidth(tail) + 1 <= width) return "…" + tail;
    return "…" + tail.substr(tail.size() - static_cast<std::size_t>(width - 1));
}

Element panel(const std::string& title,
              Element content,
              bool focused,
              const Theme& theme,
              const DecorationSettings& decoration,
              const std::string& rightLabel) {
    const Color edge = focused ? toFtx(theme.accent) : toFtx(theme.border);
    const Color label = focused ? toFtx(theme.accent) : toFtx(theme.muted);

    Element titleText = text(" " + title + " ") | color(label);
    if (focused) titleText = bold(std::move(titleText));

    Elements header;
    header.push_back(std::move(titleText));
    if (!rightLabel.empty()) {
        header.push_back(filler());
        header.push_back(text(" " + rightLabel + " ") | color(toFtx(theme.muted)));
    }

    // Empty text() elements leave two blank rows where the title was.
    Elements rows;
    if (decoration.titleBar) {
        rows.push_back(hbox(std::move(header)));
        rows.push_back(separator() | color(edge));
    }
    rows.push_back(std::move(content) | flex);
    Element body = vbox(std::move(rows));

    if (decoration.border == "none") return body;
    return body | borderStyled(borderStyle(decoration.border), edge);
}

Element kbd(const std::string& label, const Theme& theme) {
    return text(" " + label + " ") | bgcolor(toFtx(theme.surface)) | color(toFtx(theme.fg));
}

Element hint(const std::string& keys, const std::string& what, const Theme& theme) {
    return hbox({
        kbd(keys, theme),
        text("  "),
        text(what) | color(toFtx(theme.muted)),
    });
}

Element highlighted(const std::string& value,
                    const std::vector<int>& positions,
                    const Theme& theme,
                    bool selected) {
    Elements pieces;
    const Color plain = selected ? toFtx(theme.fg) : toFtx(theme.fg);
    const Color hit = toFtx(theme.accent);

    std::string run;
    bool runIsHit = false;
    const auto flush = [&] {
        if (run.empty()) return;
        pieces.push_back(runIsHit ? (text(run) | color(hit) | bold) : (text(run) | color(plain)));
        run.clear();
    };

    for (int i = 0; i < static_cast<int>(value.size()); ++i) {
        const bool isHit = std::find(positions.begin(), positions.end(), i) != positions.end();
        if (isHit != runIsHit) { flush(); runIsHit = isHit; }
        run.push_back(value[static_cast<std::size_t>(i)]);
    }
    flush();
    return hbox(std::move(pieces));
}

Element modal(Element content, const Theme& theme, const DecorationSettings& decoration,
              int width, int height) {
    Element framed = clear_under(std::move(content) | bgcolor(toFtx(theme.surface))) |
                     borderStyled(borderStyle(decoration.border), toFtx(theme.accent));
    if (width > 0) framed = std::move(framed) | size(WIDTH, EQUAL, width);
    if (height > 0) framed = std::move(framed) | size(HEIGHT, LESS_THAN, height);

    Element ringed = clear_under(vbox({
        text(""),
        hbox({text(" "), std::move(framed), text(" ")}),
        text(""),
    }));

    return vbox({
        filler(),
        hbox({filler(), std::move(ringed), filler()}),
        filler(),
    });
}

Element Hotspots::track(int id, Element element) {
    spots_.push_back({id, Box{}});
    return std::move(element) | reflect(spots_.back().box);
}

int Hotspots::at(int x, int y) const {
    int best = -1;
    int smallest = 0;
    for (const Spot& spot : spots_) {
        if (!spot.box.Contain(x, y)) continue;
        const int area = (spot.box.x_max - spot.box.x_min + 1) *
                         (spot.box.y_max - spot.box.y_min + 1);
        if (best < 0 || area < smallest) { best = spot.id; smallest = area; }
    }
    return best;
}

} // namespace apollo::ui
