// The application: layout, focus, keys, tabs, and everything the panes and
// overlays hang off.
#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "core/Commands.h"
#include "core/Config.h"
#include "term/Session.h"
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
    };
    Layout measure() const;

    // --- lifecycle --------------------------------------------------------
    void rebuildCommands();
    void applyConfig();
    bool newTab(const Connection* connection, const std::string& initialCommand = "");
    void closeTab(int index);
    term::Session* active();
    const term::Session* active() const;

    // --- events -----------------------------------------------------------
    bool onEvent(const ftxui::Event& event);
    bool onMouse(const ftxui::Event& event);
    bool runBind(const KeyChord& chord);
    void act(const std::string& action, const std::vector<std::string>& args);
    void runCommand(const Command& command);
    void openPalette();
    void tick();

    // --- rendering --------------------------------------------------------
    ftxui::Element render();
    ftxui::Element renderStatusBar(const Layout& layout);
    ftxui::Element renderTabs();
    ftxui::Element renderHelp(int width, int height);
    ftxui::Element renderSearch();

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

    BrowserView browser_;
    Palette palette_;
    ConfigView configView_;
    Onboard onboard_;

    bool browserVisible_ = true;
    bool stacked_ = false;
    int browserWidth_ = 34;
    bool leaderArmed_ = false;
    bool helpOpen_ = false;

    bool searching_ = false;
    LineEdit searchQuery_;
    std::vector<int> searchHits_;
    int searchAt_ = 0;

    std::string status_;
    bool statusIsError_ = false;
    std::chrono::steady_clock::time_point statusUntil_{};
    bool cursorPhase_ = true;
    std::chrono::steady_clock::time_point lastBlink_{};

    std::atomic<bool> ticking_{false};
    std::thread ticker_;
    bool quitting_ = false;
    int exitCode_ = 0;
};

} // namespace apollo::ui
