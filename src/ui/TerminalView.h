// Draws a terminal session.
#pragma once

#include <string>

#include <ftxui/dom/elements.hpp>

#include "core/Config.h"
#include "term/Session.h"

namespace apollo::ui {

struct TerminalViewOptions {
    bool focused = true;
    int height = 24;
    std::string searchTerm;  // matches are highlighted while a search is open
    int searchLine = -1;     // the match currently being stepped through
};

ftxui::Element renderTerminal(const term::Session& session,
                              const Theme& theme,
                              const TerminalSettings& settings,
                              const TerminalViewOptions& options);

ftxui::Element renderTerminalStatus(const term::Session& session,
                                    const Theme& theme,
                                    int width);

} // namespace apollo::ui
