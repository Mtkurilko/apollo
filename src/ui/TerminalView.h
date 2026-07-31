// Draws a terminal session.
//
// The grid is turned into FTXUI elements one run of identical attributes at a
// time, so a screen full of plain text costs a handful of elements per line
// rather than one per cell.
#pragma once

#include <string>

#include <ftxui/dom/elements.hpp>

#include "core/Config.h"
#include "term/Session.h"

namespace apollo::ui {

struct TerminalViewOptions {
    bool focused = true;
    bool cursorPhase = true; // false during the dark half of a blink
    int height = 24;
    std::string searchTerm;  // matches are highlighted while a search is open
    int searchLine = -1;     // the match currently being stepped through
};

ftxui::Element renderTerminal(const term::Session& session,
                              const Theme& theme,
                              const TerminalSettings& settings,
                              const TerminalViewOptions& options);

// The line along the bottom of the terminal pane: where the scrollback is
// parked, and what the session is attached to.
ftxui::Element renderTerminalStatus(const term::Session& session,
                                    const Theme& theme,
                                    int width);

} // namespace apollo::ui
