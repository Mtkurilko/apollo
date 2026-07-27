#include "ui/Widgets.h"

#include <algorithm>
#include <cctype>

namespace apollo::ui {

using namespace ftxui;

Color toFtx(const Rgb& colour) { return Color::RGB(colour.r, colour.g, colour.b); }

Color resolve(term::ColorRef colour, const Theme& theme, bool foreground) {
    if (term::isDefaultColor(colour)) {
        return foreground ? toFtx(theme.fg) : toFtx(theme.bg);
    }
    if (term::isIndexedColor(colour)) {
        const int index = term::colorIndex(colour);
        if (index < 16) return toFtx(theme.ansi[static_cast<std::size_t>(index)]);
        return Color::Palette256(static_cast<Color::Palette256>(index));
    }
    return Color::RGB(static_cast<std::uint8_t>((colour >> 16) & 0xFF),
                      static_cast<std::uint8_t>((colour >> 8) & 0xFF),
                      static_cast<std::uint8_t>(colour & 0xFF));
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

// Steps one whole UTF-8 character, so arrowing through an accented word does
// not leave the cursor stranded mid-sequence.
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

    // Anything printable, including whole UTF-8 characters, is inserted as-is.
    if (!raw.empty() && static_cast<unsigned char>(raw[0]) >= 0x20 &&
        static_cast<unsigned char>(raw[0]) != 0x7F) {
        text.insert(static_cast<std::size_t>(cursor), raw);
        cursor += static_cast<int>(raw.size());
        return true;
    }
    return false;
}

Element LineEdit::render(const Theme& theme, const std::string& placeholder, bool focused,
                         bool mask) const {
    // `text` is this struct's own field, so the element builder needs
    // qualifying throughout.
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
    const std::string before = shown.substr(0, static_cast<std::size_t>(at));
    const std::string after = shown.substr(static_cast<std::size_t>(at));

    Elements parts;
    parts.push_back(ftxui::text(before) | color(toFtx(theme.fg)));
    if (focused) {
        // Draw the cursor as a block over the character it sits on, which is
        // where the eye expects it in a terminal.
        std::size_t width = 1;
        if (!after.empty()) {
            const unsigned char lead = static_cast<unsigned char>(after[0]);
            if ((lead & 0xE0) == 0xC0) width = 2;
            else if ((lead & 0xF0) == 0xE0) width = 3;
            else if ((lead & 0xF8) == 0xF0) width = 4;
        }
        const std::string under = after.empty() ? " " : after.substr(0, width);
        parts.push_back(ftxui::text(under) | bgcolor(toFtx(theme.accent)) | color(toFtx(theme.bg)));
        if (after.size() > width) {
            parts.push_back(ftxui::text(after.substr(width)) | color(toFtx(theme.fg)));
        }
    } else if (!after.empty()) {
        parts.push_back(ftxui::text(after) | color(toFtx(theme.fg)));
    }
    return hbox(std::move(parts));
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

    Element body = vbox({
        decoration.titleBar ? hbox(std::move(header)) : text(""),
        decoration.titleBar ? separator() | color(edge) : text(""),
        std::move(content) | flex,
    });

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
    // clear_under wipes whatever is beneath before drawing: without it the
    // panes below show through wherever the modal has a filler rather than
    // text, and their borders cut across it.
    Element framed = clear_under(std::move(content) | bgcolor(toFtx(theme.surface))) |
                     borderStyled(borderStyle(decoration.border), toFtx(theme.accent));
    framed = clear_under(std::move(framed));
    if (width > 0) framed = std::move(framed) | size(WIDTH, EQUAL, width);
    if (height > 0) framed = std::move(framed) | size(HEIGHT, LESS_THAN, height);

    return vbox({
        filler(),
        hbox({filler(), std::move(framed), filler()}),
        filler(),
    });
}

} // namespace apollo::ui
