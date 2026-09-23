// The app itself. Layout/focus/keys/tabs/overlays all live here.
#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "core/Commands.h"
#include "core/Config.h"
#include "core/Control.h"
#include "net/RemoteFs.h"
#include "net/Transfer.h"
#include "term/Session.h"
#include "ui/Boot.h"
#include "ui/BrowserView.h"
#include "ui/ConfigView.h"
#include "ui/Onboard.h"
#include "ui/Palette.h"
#include "ui/TransferForm.h"

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
    // Changes what a tab is connected to. Keeps the tab.
    bool retarget(int index, const Connection* connection);
    void closeTab(int index);
    // Drops the shared ssh master once the last tab using it is gone.
    void releaseConnection(const std::string& name);
    term::Session* active();
    const term::Session* active() const;

    // --- events -----------------------------------------------------------
    bool onEvent(const ftxui::Event& event);
    bool onMouse(const ftxui::Event& event);
    void handleControl(const std::string& message);
    // First column you can grab to drag. -1 if the panes aren't side by side.
    int dividerColumn(const Layout& layout) const;
    bool runBind(const KeyChord& chord);
    // A key that isn't Apollo's: the browser if it has focus, else the shell.
    void deliver(const KeyChord& chord, const std::string& raw);

    // --- the leader -------------------------------------------------------
    // Soft: the plain leader key, taken because typing it would do nothing
    // here. Hard: a press that can only mean the leader.
    enum class LeaderPress { None, Soft, Hard };
    LeaderPress leaderPress(const KeyChord& chord) const;
    // Space at an empty prompt, or in the browser when it isn't mid-search.
    bool leaderIsFree() const;
    ftxui::Element renderLeaderKeys(int width);

    void act(const std::string& action, const std::vector<std::string>& args);
    void runCommand(const Command& command);
    void openPalette();
    // --- opening a file ---------------------------------------------------
    // Uses the `open` rule for the file type. alwaysAsk re-asks even if we
    // already remembered an answer.
    void openFile(const std::filesystem::path& path, bool alwaysAsk = false);
    void askHowToOpen(const std::filesystem::path& path);
    void runOpener(const std::string& how, const std::filesystem::path& path);
    // The destination list, for when `apollo connect` wasn't told which one.
    void pickConnection();

    // --- moving files between machines -------------------------------------
    // Sends whatever the browser has selected to the other machine: down to
    // this one from a remote listing, up to a connection from a local one.
    void transferSelected();
    // Where an upload to `conn` lands unless you say otherwise: its remote_dir,
    // or home.
    static std::string uploadDirFor(const Connection& conn);
    // Destinations an upload could go to, likeliest first.
    std::vector<std::string> uploadChoices() const;
    void openTransfer(transfer::Job job, bool directory);
    // Enter in the form: check this side now, the other side in the background.
    void submitTransfer();
    void finishTransfers();

    // Housekeeping beat. Returns true when something on screen actually moved,
    // so the caller can skip the repaint when nothing did.
    bool tick();
    // How long until the next beat. Only fast while something is animating.
    std::chrono::milliseconds tickInterval() const;
    // Marks the frame as worth redrawing on the next beat.
    void invalidate() { dirty_ = true; }

    // --- rendering --------------------------------------------------------
    ftxui::Element render();
    ftxui::Element renderStatusBar(const Layout& layout);
    ftxui::Element renderTabs();
    std::vector<int> tabEdges() const;
    std::string whereLabel() const;
    ftxui::Element renderHelp(int width, int height);
    ftxui::Element renderSearch();
    ftxui::Element renderHints(int room);

    // A yes/no question on top of everything. For quitting with something
    // still running, and for copying files onto another machine.
    struct Confirmation {
        std::string question;
        std::string detail;
        std::function<void()> onYes;
        std::string yes = "yes";
        std::string no = "cancel";
    };
    void askConfirm(Confirmation question);
    const term::Session* busyTab() const;
    ftxui::Element renderConfirm(int width, int height);

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

    // --- browser. Follows whichever machine the active tab is on ----------
    void followActiveMachine();
    void askRemote(const std::string& connection, const std::string& path, bool record = true);
    // Takes the shell wherever the pane just went. Also stops follow_cwd from
    // yanking the pane back before the shell has caught up.
    void syncShell(const std::string& connection, const std::string& path);

    BrowserView browser_;
    remote::Lister remoteLister_;
    // Last remote directory the shell reported. Stops a failed listing from
    // being asked for again every tick.
    std::string followedCwd_;
    // A connection a tab is on but the config no longer has. Give up once,
    // not every tick.
    std::string abandonedConnection_;
    // What the pane was last pointed at for this tab. Without it, browsing
    // somewhere else gets undone on the next tick.
    std::string shownMachine_;
    std::chrono::steady_clock::time_point remoteRefreshed_{};
    // Set when the connected terminal prints something. Check the shell's
    // directory once it goes quiet, not in the middle of a command.
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

    // Layout lives in the config so a keybind and the file can't disagree.
    bool browserVisible() const;
    bool stacked() const;
    int browserWidth() const;
    bool put(const std::string& path, const std::string& value);

    bool leaderArmed_ = false;
    bool leaderSoft_ = false;
    KeyChord leaderChord_;   // what armed it, sent through if it wasn't meant
    std::string leaderRaw_;
    std::chrono::steady_clock::time_point leaderArmedAt_{};

    transfer::Runner transfers_;
    TransferForm transferForm_;
    // The machine the pane last showed. Goes first when asking where to send.
    std::string lastRemote_;
    bool helpOpen_ = false;
    std::optional<Confirmation> confirm_;
    Hotspots confirmSpots_;

    bool searching_ = false;
    LineEdit searchQuery_;
    std::vector<int> searchHits_;
    int searchAt_ = 0;

    std::string status_;
    bool statusIsError_ = false;
    std::chrono::steady_clock::time_point statusUntil_{};
    std::chrono::steady_clock::time_point lastClick_{};
    int lastClickRow_ = -1;

    // Set by anything that changes what is on screen outside of an event.
    bool dirty_ = false;
    // The shell's cwd, and what it resolved to. Resolving is several syscalls,
    // so it only happens when the shell reports somewhere new.
    std::string lastShellCwd_;
    std::filesystem::path lastShellCwdResolved_;
    std::chrono::steady_clock::time_point configCheckedAt_{};

    std::atomic<bool> ticking_{false};
    std::atomic<int> tickMs_{100}; // the splash runs before the first beat
    std::thread ticker_;
    bool quitting_ = false;
    int exitCode_ = 0;
};

} // namespace apollo::ui
