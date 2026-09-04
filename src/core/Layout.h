// Pane geometry. The terminal size has to be exactly the space it's drawn in
// or the renderer clips the last column of every row.
#pragma once

#include <string>

namespace apollo::layout {

struct Request {
    int width = 80;
    int height = 24;

    std::string border = "rounded"; // "none" means no frame
    bool titleBar = true;
    bool statusBar = true;
    bool tabBar = false;
    int gaps = 1;

    bool browserVisible = true;
    bool stacked = false;
    int browserWidth = 34;
};

struct Panes {
    int browserWidth = 0; // 0 when the browser is not shown
    int browserRows = 0;
    int terminalCols = 80;
    int terminalRows = 24;

    bool border = true;
    bool titleBar = true;
    bool statusBar = true;
    bool tabBar = false;

    bool browserShown() const { return browserWidth > 0; }
};

constexpr int kUsableCols = 24;
constexpr int kUsableRows = 6;
// Any narrower and it's all truncation, so the browser gives up its space.
constexpr int kUsableBrowserCols = 22;

Panes compute(const Request& request);

int maxBrowserWidth(const Request& request);

} // namespace apollo::layout
