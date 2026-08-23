// Shared interface pieces, and Apollo colours converted to FTXUI's.
#pragma once

#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "core/Config.h"
#include "core/Theme.h"
#include "term/Screen.h"

namespace apollo::ui {

ftxui::Color toFtx(const Rgb& colour);

ftxui::Color resolve(term::ColorRef colour, const Theme& theme, bool foreground);

ftxui::BorderStyle borderStyle(const std::string& name);

ftxui::Element panel(const std::string& title,
                     ftxui::Element content,
                     bool focused,
                     const Theme& theme,
                     const DecorationSettings& decoration,
                     const std::string& rightLabel = "");

ftxui::Element kbd(const std::string& label, const Theme& theme);

// "Ctrl+K  open the palette" rows, for the help and the empty states.
ftxui::Element hint(const std::string& keys, const std::string& what, const Theme& theme);

ftxui::Element highlighted(const std::string& text,
                           const std::vector<int>& positions,
                           const Theme& theme,
                           bool selected);

ftxui::Element modal(ftxui::Element content, const Theme& theme,
                     const DecorationSettings& decoration, int width, int height);

struct LineEdit {
    std::string text;
    int cursor = 0; // byte offset, always on a character boundary

    void set(const std::string& value);
    void clear();
    bool onKey(const KeyChord& chord, const std::string& raw);

    ftxui::Element render(const Theme& theme,
                          const std::string& placeholder,
                          bool focused,
                          bool mask = false) const;
};

// Cuts a string to `width` display columns, adding an ellipsis when it had to.
std::string elide(const std::string& text, int width);
std::string elidePath(const std::string& path, int width);
// Display width of a UTF-8 string, in terminal columns.
int displayWidth(const std::string& text);

} // namespace apollo::ui
