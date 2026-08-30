// The application: layout, focus, keys, tabs, overlays.
#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "core/Commands.h"
#include "core/Config.h"
#include "core/Control.h"
#include "net/RemoteFs.h"
#include "term/Session.h"
#include "ui/Boot.h"
#include "ui/BrowserView.h"
#include "ui/ConfigView.h"
#include "ui/Onboard.h"
#include "ui/Palette.h"

namespace apollo::ui {

class App {
public:
    struct Options {
        bool runSetup = false;      // start in the wizard
        bool openConfig = false;    // start with the config screen up
        std::string connect;        // an SSH destination to open in the first tab
        bool chooseConnection = false; // several are configured and none was named
        std::string workspace;      // overrides general.workspace
        std::string command;        // run this, then hand over to the shell
    };

    App(Config& config, Options options);
    ~App();

    int run();

private:
    enum class Focus { Terminal, Browser };

    // --- layout -----------------------------------------------------------
    struct Layout {
        int width = 80, height = 24;
        int browserWidth = 0;      // 0 when hidden
        int terminalRows = 20, terminalCols = 80;
        int browserRows = 20;
        bool stacked = false;
        DecorationSettings decoration;
    };
    Layout measure() const;

    // --- lifecycle --------------------------------------------------------
    void rebuildCommands();
    void applyConfig();
    std::unique_ptr<term::Session> makeSession(const Connection* connection,
                                               const std::string& initialCommand,
                                               std::string* error);
    bool newTab(const Connection* connection, const std::string& initialCommand = "");
    // Swaps what a tab is connected to without losing the tab itself.
    bool retarget(int index, const Connection* connection);
    void closeTab(int index);
    // Drops the shared ssh master once the last tab using it has gone.
    void releaseConnection(const std::string& name);
    term::Session* active();
    const term::Session* active() const;

    // --- events -----------------------------------------------------------
    bool onEvent(const ftxui::Event& event);
    bool onMouse(const ftxui::Event& event);
    void handleControl(const std::string& message);
    // First column of the grab zone, or -1 when the panes are not side by side.
    int dividerColumn(const Layout& layout) const;
    bool runBind(const KeyChord& chord);
    void act(const std::string& action, const std::vector<std::string>& args);
    void runCommand(const Command& command);
    void openPalette();
    // The list of destinations, when `apollo connect` was not told which.
    void pickConnection();
    void tick();

    // --- rendering --------------------------------------------------------
    ftxui::Element render();
    ftxui::Element renderStatusBar(const Layout& layout);
    ftxui::Element renderTabs();
    std::vector<int> tabEdges() const;
    std::string whereLabel() const;
    ftxui::Element renderHelp(int width, int height);
    ftxui::Element renderSearch();
    ftxui::Element renderHints(int room);

    void say(const std::string& message, bool isError = false);
    void copyToClipboard(const std::string& text);
    std::string clipboard() const;
    std::string keyHintFor(const std::string& action) const;

    Config& config_;
    Options options_;
    const Theme* theme_ = nullptr;
    Theme previewTheme_;

    ftxui::ScreenInteractive screen_;
    CommandRegistry registry_;

    std::vector<std::unique_ptr<term::Session>> tabs_;
    int tab_ = 0;
    Focus focus_ = Focus::Terminal;

    // --- the browser, which follows whichever machine the active tab is on --
    void followActiveMachine();
    void askRemote(const std::string& connection, const std::string& path, bool record = true);
    // Takes the shell to wherever the pane just went, and stops follow_cwd
    // from dragging the pane back before the shell has caught up.
    void syncShell(const std::string& connection, const std::string& path);

    BrowserView browser_;
    remote::Lister remoteLister_;
    // The remote directory the shell last reported, so a listing that fails is
    // not asked for again every tick.
    std::string followedCwd_;
    // A connection a tab is still on but the config no longer knows about, so
    // the pane gives up on it once rather than every tick.
    std::string abandonedConnection_;
    // The connection the pane was last pointed at for the active tab, so
    // browsing somewhere else is not undone on the next tick.
    std::string shownMachine_;
    std::chrono::steady_clock::time_point remoteRefreshed_{};
    // Set when the connected terminal produces output, so the shell's
    // directory is checked once it goes quiet rather than mid-command.
    std::chrono::steady_clock::time_point remoteSettleAt_{};
    std::uint64_t lastRevision_ = 0;
    Palette palette_;
    ConfigView configView_;
    Onboard onboard_;
    Boot boot_;
    ControlServer control_;

    int dragWidth_ = -1;
    bool draggingDivider_ = false;

    std::filesystem::path pendingCwd_;
    std::chrono::steady_clock::time_point pendingCwdUntil_{};

    // The arrangement lives in the config, so a key and the file cannot disagree.
    bool browserVisible() const;
    bool stacked() const;
    int browserWidth() const;
    bool put(const std::string& path, const std::string& value);

    bool leaderArmed_ = false;
    bool helpOpen_ = false;

    bool searching_ = false;
    LineEdit searchQuery_;
    std::vector<int> searchHits_;
    int searchAt_ = 0;

    std::string status_;
    bool statusIsError_ = false;
    std::chrono::steady_clock::time_point statusUntil_{};
    std::chrono::steady_clock::time_point lastClick_{};
    int lastClickRow_ = -1;

    std::atomic<bool> ticking_{false};
    std::thread ticker_;
    bool quitting_ = false;
    int exitCode_ = 0;
};

} // namespace apollo::ui
