// Small shared pieces of the interface, and the one place where Apollo's own
// colours are turned into FTXUI's.
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

// Resolves a cell's colour against the theme. Indexed colours come from the
// theme's own ramp, so `ls` output looks like it belongs to the rest of Apollo.
ftxui::Color resolve(term::ColorRef colour, const Theme& theme, bool foreground);

ftxui::BorderStyle borderStyle(const std::string& name);

// A titled pane. The focused one gets the accent colour and a solid title; the
// others recede, which is the whole of Apollo's focus indication.
ftxui::Element panel(const std::string& title,
                     ftxui::Element content,
                     bool focused,
                     const Theme& theme,
                     const DecorationSettings& decoration,
                     const std::string& rightLabel = "");

// A key, drawn as a key.
ftxui::Element kbd(const std::string& label, const Theme& theme);

// "Ctrl+K  open the palette" rows, for the help and the empty states.
ftxui::Element hint(const std::string& keys, const std::string& what, const Theme& theme);

// Highlights the characters a fuzzy match landed on.
ftxui::Element highlighted(const std::string& text,
                           const std::vector<int>& positions,
                           const Theme& theme,
                           bool selected);

// A centred modal over a dimmed background.
ftxui::Element modal(ftxui::Element content, const Theme& theme,
                     const DecorationSettings& decoration, int width, int height);

// A one line text field. Apollo uses its own rather than FTXUI's Input so that
// the same editing keys work identically in the palette, the config editor and
// the setup wizard, and so that nothing here competes with the terminal for
// key events.
struct LineEdit {
    std::string text;
    int cursor = 0; // byte offset, always on a character boundary

    void set(const std::string& value);
    void clear();
    // True when the key belonged to the field.
    bool onKey(const KeyChord& chord, const std::string& raw);

    ftxui::Element render(const Theme& theme,
                          const std::string& placeholder,
                          bool focused,
                          bool mask = false) const;
};

// Cuts a string to `width` display columns, adding an ellipsis when it had to.
std::string elide(const std::string& text, int width);
// The same, but keeping the end. For a path the last components are the ones
// worth reading, so `…/apollo/src/ui` beats `/Users/someone/very/lo…`.
std::string elidePath(const std::string& path, int width);
// Display width of a UTF-8 string, in terminal columns.
int displayWidth(const std::string& text);

} // namespace apollo::ui
