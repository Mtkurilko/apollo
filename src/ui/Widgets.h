// Shared UI pieces, plus Apollo colors converted to FTXUI's.
#pragma once

#include <deque>
#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/color.hpp>

#include "core/Config.h"
#include "core/Theme.h"
#include "term/Screen.h"

namespace apollo::ui {

ftxui::Color toFtx(const Rgb& color);

ftxui::Color resolve(term::ColorRef color, const Theme& theme, bool foreground);

ftxui::BorderStyle borderStyle(const std::string& name);

ftxui::Element panel(const std::string& title,
                     ftxui::Element content,
                     bool focused,
                     const Theme& theme,
                     const DecorationSettings& decoration,
                     const std::string& rightLabel = "");

ftxui::Element kbd(const std::string& label, const Theme& theme);

// "Ctrl+K  open the palette" rows. Used by the help and the empty states.
ftxui::Element hint(const std::string& keys, const std::string& what, const Theme& theme);

ftxui::Element highlighted(const std::string& text,
                           const std::vector<int>& positions,
                           const Theme& theme,
                           bool selected);

ftxui::Element modal(ftxui::Element content, const Theme& theme,
                     const DecorationSettings& decoration, int width, int height);

// Keeps track of where clickable things ended up. An overlay tracks its rows
// and buttons while rendering, then asks what's under the pointer on a click.
// Beats working out the geometry of a centered modal by hand.
class Hotspots {
public:
    void clear() { spots_.clear(); }

    // Wraps an element so rendering records the box it landed in under `id`.
    ftxui::Element track(int id, ftxui::Element element);

    // Smallest tracked box containing the point. -1 if nothing is there.
    int at(int x, int y) const;

private:
    struct Spot {
        int id = 0;
        ftxui::Box box;
    };
    // deque, not vector -- reflect() keeps a reference so these can't move.
    std::deque<Spot> spots_;
};

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

// Cuts a string to `width` columns. Adds an ellipsis if it had to cut.
std::string elide(const std::string& text, int width);
std::string elidePath(const std::string& path, int width);
// How wide a UTF-8 string actually is, in terminal columns.
int displayWidth(const std::string& text);

} // namespace apollo::ui
