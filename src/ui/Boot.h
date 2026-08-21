// The splash on the way in.
//
// It is deliberately brief and never blocking: the shell is already starting
// underneath it, any key dismisses it, and `decoration.boot = false` removes it
// altogether. A boot screen that made you wait would be a boot screen that
// wore out its welcome on the second day.
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

    // `lines` are the few facts shown under the mark: where the config is,
    // which workspace, what it connected to.
    void start(std::vector<Line> lines, bool animate);
    void dismiss() { running_ = false; }
    bool running() const;

    ftxui::Element render(const Theme& theme, const DecorationSettings& decoration,
                          int width, int height) const;

    // The word, in block letters, as one string per row. Public so the tests
    // can check every row is the same width — a ragged row would tear the
    // reveal in half.
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
