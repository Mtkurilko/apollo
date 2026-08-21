#include "core/Layout.h"

#include <algorithm>

namespace apollo::layout {

int maxBrowserWidth(const Request& request) {
    const int width = std::max(1, request.width);
    const int frameH = request.border == "none" ? 0 : 2;
    return std::max(16, width - std::max(0, request.gaps) - frameH - kUsableCols);
}

Panes compute(const Request& request) {
    const int width = std::max(1, request.width);
    const int height = std::max(1, request.height);

    Panes panes;
    panes.border = request.border != "none";
    panes.titleBar = request.titleBar && panes.border;
    panes.statusBar = request.statusBar;
    panes.tabBar = request.tabBar;

    // Shed decoration until the terminal has at least one row and one column
    // to live in. Anything else claims space that does not exist, and the
    // renderer resolves that by silently clipping.
    const auto frameV = [&] { return (panes.border ? 2 : 0) + (panes.titleBar ? 2 : 0); };
    const auto frameH = [&] { return panes.border ? 2 : 0; };
    const auto rowsLeft = [&] {
        return height - (panes.statusBar ? 1 : 0) - (panes.tabBar ? 1 : 0) - frameV();
    };

    while (rowsLeft() < 1 || width - frameH() < 1) {
        if (panes.titleBar) { panes.titleBar = false; continue; }
        if (panes.border) { panes.border = false; continue; }
        if (panes.statusBar) { panes.statusBar = false; continue; }
        if (panes.tabBar) { panes.tabBar = false; continue; }
        break;
    }

    const int available =
        std::max(1, height - (panes.statusBar ? 1 : 0) - (panes.tabBar ? 1 : 0));
    const int gaps = std::max(0, request.gaps);
    // The browser is shown only if it can have a width worth having: the one
    // that was asked for, or the usable minimum, whichever is smaller.
    const int floorWidth = std::min(std::max(1, request.browserWidth), kUsableBrowserCols);
    const int wantedBrowser =
        std::clamp(request.browserWidth, floorWidth, std::max(floorWidth, maxBrowserWidth(request)));

    const bool roomBeside = width - floorWidth - gaps - frameH() >= kUsableCols;
    const bool roomAbove = available >= 2 * frameV() + kUsableRows + 3;
    const bool showBrowser = request.browserVisible && (request.stacked ? roomAbove : roomBeside);

    if (!showBrowser) {
        panes.terminalCols = std::max(1, width - frameH());
        panes.terminalRows = std::max(1, available - frameV());
        return panes;
    }

    if (request.stacked) {
        // A third of the height, but never so little that the browser is only
        // chrome, and never so much that the terminal is.
        int browserHeight = std::clamp(available / 3, frameV() + 6, frameV() + 20);
        browserHeight = std::min(browserHeight, available - gaps - frameV() - kUsableRows);
        browserHeight = std::max(browserHeight, frameV() + 1);

        panes.browserWidth = width;
        panes.browserRows = std::max(1, browserHeight - frameV());
        panes.terminalCols = std::max(1, width - frameH());
        panes.terminalRows = std::max(1, available - browserHeight - gaps - frameV());
        return panes;
    }

    panes.browserWidth = wantedBrowser;
    panes.terminalCols = std::max(1, width - wantedBrowser - gaps - frameH());
    panes.terminalRows = std::max(1, available - frameV());
    panes.browserRows = std::max(1, available - frameV());
    return panes;
}

} // namespace apollo::layout
