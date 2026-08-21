// Pane geometry.
//
// This is arithmetic with no screen attached, which is exactly why it lives
// here: the terminal's size has to be *precisely* the space it will be drawn
// in. One column too many and the renderer clips the last column of every row,
// which shows up as a character going missing at each line wrap — subtle,
// intermittent, and miserable to chase from a screenshot.
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

    // What survived. In a window too small for the full chrome these come back
    // false, in this order: the title bars first, then the borders, then the
    // status bar. A tiny window is still a working terminal, just a bare one.
    bool border = true;
    bool titleBar = true;
    bool statusBar = true;
    bool tabBar = false;

    bool browserShown() const { return browserWidth > 0; }
};

// Below these the second pane is more chrome than content, and the browser
// steps aside on its own rather than squeezing both into uselessness.
constexpr int kUsableCols = 24;
constexpr int kUsableRows = 6;
// A browser narrower than this shows almost nothing but truncation, so rather
// than shrink past it the browser gives up its place. Someone who has asked
// for a narrower one than this gets what they asked for.
constexpr int kUsableBrowserCols = 22;

Panes compute(const Request& request);

// The widest the browser may be: whatever leaves the terminal usable. The
// divider drag clamps to this too, so dragging never records a width the
// layout will then refuse to draw.
int maxBrowserWidth(const Request& request);

} // namespace apollo::layout
