// Splash screen. Skippable, and never holds up the shell starting.
#pragma once

#include <chrono>
#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>

#include "core/Config.h"

namespace apollo::ui {

class Boot {
public:
    struct Line {
        std::string label;
        std::string value;
    };

    void start(std::vector<Line> lines, bool animate);
    void dismiss() { running_ = false; }
    bool running() const;

    ftxui::Element render(const Theme& theme, const DecorationSettings& decoration,
                          int width, int height) const;

    // The word in block letters, one string per row.
    static const std::vector<std::string>& mark();
    static int markWidth();

private:
    std::chrono::milliseconds elapsed() const;

    bool running_ = false;
    bool animate_ = true;
    std::chrono::steady_clock::time_point started_{};
    std::vector<Line> lines_;
};

} // namespace apollo::ui
