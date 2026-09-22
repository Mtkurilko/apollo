#include "ui/App.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <utility>

#include <ftxui/component/event.hpp>
#include <ftxui/screen/terminal.hpp>

#include "core/Layout.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "net/Ssh.h"
#include "ui/TerminalView.h"
#include "ui/Widgets.h"

namespace fs = std::filesystem;

namespace apollo::ui {

using namespace ftxui;

namespace {

// Posted only when a beat found something worth showing. FTXUI invalidates
// the frame for every event it handles, which is the whole point of it.
const Event kRepaint = Event::Special("apollo:repaint");
const Event kControl = Event::Special("apollo:control");
const Event kRemote = Event::Special("apollo:remote");
const Event kTransfer = Event::Special("apollo:transfer");

// A leader held this long gets a list of what can follow it.
constexpr auto kLeaderKeysDelay = std::chrono::milliseconds(450);

// The two answers to a confirmation. Shared by the renderer and the click handler.
constexpr int kConfirmYes = 1;
constexpr int kConfirmNo = 2;

// Two or three words per action, for the list a held leader shows. The
// palette and F1 have room for the full sentence.
std::string shortLabel(const Bind& bind) {
    static const std::map<std::string, std::string> labels = {
        {"command_palette", "commands"},     {"focus_terminal", "terminal"},
        {"focus_browser", "browser"},        {"focus_next", "other pane"},
        {"toggle_browser", "toggle browser"},  {"toggle_layout", "stack / split"},
        {"toggle_hidden", "dotfiles"},       {"browser_back", "back"},
        {"browser_forward", "forward"},      {"browser_up", "up a folder"},
        {"browser_filter", "filter"},        {"browser_sort", "sort"},
        {"browser_reverse", "reverse sort"}, {"copy_path", "copy path"},
        {"paste_path", "go to copied path"}, {"grow_pane", "wider"},
        {"shrink_pane", "narrower"},         {"new_tab", "new tab"},
        {"close_tab", "close tab"},          {"next_tab", "next tab"},
        {"prev_tab", "previous tab"},        {"scroll_up", "scroll up"},
        {"scroll_down", "scroll down"},      {"scroll_top", "top"},
        {"scroll_bottom", "bottom"},         {"prev_prompt", "previous command"},
        {"next_prompt", "next command"},     {"copy", "copy"},
        {"paste", "paste"},                  {"clear", "clear"},
        {"search", "search"},                {"open_config", "settings"},
        {"edit_config", "edit config"},      {"setup", "setup"},
        {"reload_config", "reload config"},  {"help", "all keys"},
        {"connect", "connect"},              {"disconnect", "disconnect"},
        {"open_with", "open with…"},         {"transfer", "send across ⇅"},
        {"quit", "quit"},
    };
    if (!bind.args.empty()) return bind.describeAction(); // run deploy, exec make, ...
    const auto it = labels.find(bind.action);
    return it != labels.end() ? it->second : bind.action;
}

// Just enough base64 to read an OSC 52 clipboard payload.
std::string decodeBase64(const std::string& text) {
    static const std::string alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    int accumulator = 0;
    int bits = 0;
    for (const char c : text) {
        if (c == '=') break;
        const auto at = alphabet.find(c);
        if (at == std::string::npos) continue; // whitespace and padding
        accumulator = (accumulator << 6) | static_cast<int>(at);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((accumulator >> bits) & 0xFF));
        }
    }
    return out;
}

} // namespace

App::App(Config& config, Options options)
    : config_(config),
      options_(std::move(options)),
      screen_(ScreenInteractive::Fullscreen()),
      remoteLister_([this] { screen_.PostEvent(kRemote); }),
      configView_(config),
      onboard_(config),
      transfers_([this] { screen_.PostEvent(kTransfer); }) {
    applyConfig();

    browser_.onEnterDirectory = [this](const fs::path& path) {
        if (browser_.remote()) {
            const std::string connection = browser_.connection();
            askRemote(connection, path.string());
            // Move the shell too, or the panes disagree about where we are.
            syncShell(connection, path.string());
            return;
        }
        browser_.setPath(path);
        browser_.refresh(config_.browser());
        syncShell("", browser_.path().string());
    };
    browser_.onOpenFile = [this](const fs::path& path) { openFile(path); };

    browser_.onNeedRemote = [this](const std::string& connection, const std::string& path) {
        askRemote(connection, path, false); // the step is already in the history
    };
    browser_.onMoved = [this](const std::string& connection, const std::string& path) {
        syncShell(connection, path);
    };
    browser_.onCopyPath = [this] {
        // Copy remote paths in scp form. That's what you'd actually paste.
        if (const Connection* conn = config_.connection(browser_.connection())) {
            copyToClipboard(conn->label() + ":" + browser_.where());
            return;
        }
        copyToClipboard(browser_.path().string());
    };
    browser_.onCycleSort = [this] { act("browser_sort", {}); };
    browser_.onSend = [this] { transferSelected(); };

    configView_.onChanged = [this] { applyConfig(); };
    configView_.onEditExternally = [this] { act("edit_config", {}); };
    onboard_.onChanged = [this] { applyConfig(); };
    onboard_.onFinished = [this] {
        applyConfig();
        say("Leader is " + config_.general().leader.describe() + " — " +
            keyHintFor("command_palette") + " for commands, F1 for every key");
    };
}

App::~App() {
    ticking_ = false;
    if (ticker_.joinable()) ticker_.join();
}

// --- configuration ---------------------------------------------------------

bool App::browserVisible() const { return config_.browser().show; }
bool App::stacked() const { return config_.browser().layout == "stacked"; }
int App::browserWidth() const {
    return dragWidth_ >= 0 ? dragWidth_ : config_.browser().width;
}

int App::dividerColumn(const Layout& layout) const {
    if (layout.browserWidth == 0 || layout.stacked) return -1;
    return config_.browser().position == "right"
               ? layout.width - layout.browserWidth - config_.decoration().gaps
               : layout.browserWidth - 1;
}

bool App::put(const std::string& path, const std::string& value) {
    std::string problem;
    if (!config_.set(path, value, &problem) || !config_.save(&problem)) {
        say(problem.empty() ? "could not write " + paths::contractUser(config_.path()) : problem,
            true);
        return false;
    }
    applyConfig();
    return true;
}

void App::applyConfig() {
    theme_ = &config_.theme();

    for (auto& session : tabs_) {
        session->screen().setScrollbackLimit(config_.terminal().scrollback);
    }
    rebuildCommands();
}

void App::rebuildCommands() {
    registry_.clear();

    for (const auto& declared : config_.commands()) {
        registry_.addDeclared(declared.name, declared.exec, declared.summary,
                              "apollo.conf:" + std::to_string(declared.line + 1),
                              declared.interactive);
    }
    registry_.scanDirectory(paths::commandsDir());
}

// --- tabs ------------------------------------------------------------------

term::Session* App::active() {
    if (tab_ < 0 || tab_ >= static_cast<int>(tabs_.size())) return nullptr;
    return tabs_[static_cast<std::size_t>(tab_)].get();
}

const term::Session* App::active() const {
    if (tab_ < 0 || tab_ >= static_cast<int>(tabs_.size())) return nullptr;
    return tabs_[static_cast<std::size_t>(tab_)].get();
}

std::unique_ptr<term::Session> App::makeSession(const Connection* connection,
                                               const std::string& initialCommand,
                                               std::string* error) {
    const Layout layout = measure();
    auto session = std::make_unique<term::Session>(layout.terminalRows, layout.terminalCols,
                                                   config_.terminal().scrollback);

    term::Session::Options options;
    options.scrollback = config_.terminal().scrollback;
    // Can't start a local shell in a directory that only exists on the remote.
    options.cwd = browser_.remote() ? paths::expandUser(config_.general().workspace)
                                    : browser_.path().string();
    if (control_.running()) options.env.push_back("APOLLO_SOCKET=" + control_.path());

    if (connection) {
        const ssh::Invocation call = ssh::interactive(*connection);
        options.argv = call.argv;
        options.env = call.env;
        options.title = connection->name;
        options.connection = connection->name;
    } else {
        const std::string shell = config_.general().shell.empty() ? process::userShell()
                                                                  : config_.general().shell;
        options.argv = {shell, "-l"};
        options.title = fs::path(shell).filename().string();
    }

    if (!session->start(options, error)) return nullptr;

    session->setWakeup([this] {
        screen_.Post([this] {
            bool changed = false;
            for (auto& tab : tabs_) changed |= tab->pump();
            if (changed) screen_.PostEvent(kRepaint);
        });
    });
    session->onClipboard = [this](const std::string& base64) {
        // OSC 52. The program wants to set the clipboard.
        if (!config_.terminal().osc52Clipboard) return;
        const std::string payload = decodeBase64(base64);
        if (payload.empty()) return;
        if (process::clipboardWrite(payload)) say("clipboard set by the terminal");
    };
    if (!initialCommand.empty()) session->sendText(initialCommand + "\r");
    return session;
}

bool App::newTab(const Connection* connection, const std::string& initialCommand) {
    std::string error;
    auto session = makeSession(connection, initialCommand, &error);
    if (!session) {
        say(error, true);
        return false;
    }

    tabs_.push_back(std::move(session));
    tab_ = static_cast<int>(tabs_.size()) - 1;
    focus_ = Focus::Terminal;
    return true;
}

bool App::retarget(int index, const Connection* connection) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) return false;

    std::string error;
    auto session = makeSession(connection, "", &error);
    if (!session) {
        say(error, true);
        return false;
    }

    const std::string was = tabs_[static_cast<std::size_t>(index)]->connection();
    tabs_[static_cast<std::size_t>(index)]->close();
    tabs_[static_cast<std::size_t>(index)] = std::move(session);
    tab_ = index;
    focus_ = Focus::Terminal;
    releaseConnection(was);
    return true;
}

void App::syncShell(const std::string& connection, const std::string& path) {
    term::Session* session = active();
    if (!session) return;
    // Only cd a shell that's on the same machine as the pane.
    if (session->connection() != connection) return;

    if (!connection.empty()) {
        session->sendText("cd " + ssh::quoteRemotePath(path) + "\r");
        return;
    }
    session->sendText("cd " + process::shellQuote(path) + "\r");
    pendingCwd_ = browser_.path();
    pendingCwdUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(3);
}

void App::askRemote(const std::string& connection, const std::string& path, bool record) {
    const Connection* conn = config_.connection(connection);
    if (!conn) { say("no connection called '" + connection + "'", true); return; }

    const term::Session* session = active();
    const int shellPid =
        session && session->connection() == connection ? session->shellPid() : 0;

    browser_.expectRemote(connection, path, record);
    remoteLister_.request(*conn, path, shellPid);
    invalidate();
}

// Pane shows whatever machine the active tab is on.
// Tab switches, connects and disconnects all funnel through here.
void App::followActiveMachine() {
    const term::Session* session = active();
    const std::string wanted = session ? session->connection() : std::string();
    if (wanted == shownMachine_) return;
    shownMachine_ = wanted;
    followedCwd_.clear();
    invalidate(); // past here the pane always gets repointed

    if (wanted.empty()) {
        const fs::path home(paths::expandUser(config_.general().workspace));
        std::error_code ec;
        browser_.backToLocal(fs::is_directory(home, ec) ? home : paths::home(),
                             config_.browser());
        return;
    }

    const Connection* conn = config_.connection(wanted);
    if (!conn) {
        // Connection got edited out of the config while a tab was still on it.
        if (abandonedConnection_ != wanted) {
            abandonedConnection_ = wanted;
            browser_.backToLocal(paths::home(), config_.browser());
            say("'" + wanted + "' is no longer in the config", true);
        }
        return;
    }
    abandonedConnection_.clear();
    std::string start = session->cwd();
    if (start.empty()) start = conn->remoteDir.empty() ? "~" : conn->remoteDir;
    askRemote(wanted, start, false);
}

void App::releaseConnection(const std::string& name) {
    if (name.empty()) return;
    // Another tab might still be riding the same master.
    for (const auto& session : tabs_) {
        if (session->connection() == name) return;
    }
    if (const Connection* conn = config_.connection(name)) ssh::closeMaster(*conn);
}

void App::closeTab(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) return;

    const std::string connection = tabs_[static_cast<std::size_t>(index)]->connection();
    tabs_[static_cast<std::size_t>(index)]->close();
    tabs_.erase(tabs_.begin() + index);

    releaseConnection(connection);
    if (tabs_.empty()) { quitting_ = true; screen_.Exit(); return; }
    tab_ = std::clamp(tab_, 0, static_cast<int>(tabs_.size()) - 1);
}

// --- layout ----------------------------------------------------------------

App::Layout App::measure() const {
    const Dimensions size = Terminal::Size();
    const auto& decoration = config_.decoration();

    layout::Request request;
    request.width = size.dimx;
    request.height = size.dimy;
    request.border = decoration.border;
    request.titleBar = decoration.titleBar;
    request.statusBar = decoration.statusBar;
    request.tabBar = tabs_.size() > 1;
    request.gaps = decoration.gaps;
    request.browserVisible = browserVisible();
    request.stacked = stacked();
    request.browserWidth = browserWidth();

    const layout::Panes panes = layout::compute(request);

    Layout out;
    out.width = std::max(1, size.dimx);
    out.height = std::max(1, size.dimy);
    out.stacked = stacked();
    out.browserWidth = panes.browserWidth;
    out.browserRows = panes.browserRows;
    out.terminalCols = panes.terminalCols;
    out.terminalRows = panes.terminalRows;

    out.decoration = decoration;
    if (!panes.border) out.decoration.border = "none";
    out.decoration.titleBar = panes.titleBar;
    out.decoration.statusBar = panes.statusBar;
    return out;
}

// --- actions ---------------------------------------------------------------

const term::Session* App::busyTab() const {
    for (const auto& session : tabs_) {
        if (session->running() && session->busy()) return session.get();
    }
    return nullptr;
}

void App::askConfirm(Confirmation question) { confirm_ = std::move(question); }

void App::say(const std::string& message, bool isError) {
    status_ = message;
    statusIsError_ = isError;
    statusUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(isError ? 8 : 4);
    invalidate();
}

void App::copyToClipboard(const std::string& text) {
    if (text.empty()) return;
    if (process::clipboardWrite(text)) say("copied " + std::to_string(text.size()) + " bytes");
    else say("no clipboard tool found (pbcopy, wl-copy, xclip or xsel)", true);
}

std::string App::clipboard() const {
    return process::clipboardRead().value_or("");
}

std::string App::keyHintFor(const std::string& action) const {
    for (const auto& bind : config_.binds()) {
        if (bind.action != action) continue;
        if (!(bind.chord.mods & ModLeader)) return bind.chord.describe();
        // "Space ," reads better than "Leader ," once you know what the leader is.
        KeyChord bare = bind.chord;
        bare.mods &= static_cast<std::uint8_t>(~ModLeader);
        return config_.general().leader.describe() + " " + bare.describe();
    }
    return "";
}

void App::runCommand(const Command& command) {
    if (term::Session* session = active()) {
        session->sendText(command.exec + "\r");
        focus_ = Focus::Terminal;
    }
}

void App::act(const std::string& action, const std::vector<std::string>& args) {
    term::Session* session = active();
    const std::string argument = args.empty() ? "" : args[0];

    if (action == "command_palette") { openPalette(); return; }
    if (action == "focus_terminal") { focus_ = Focus::Terminal; return; }
    if (action == "focus_browser") {
        if (!browserVisible()) put("browser.show", "true");
        focus_ = Focus::Browser;
        return;
    }
    if (action == "focus_next") {
        focus_ = focus_ == Focus::Terminal && browserVisible() ? Focus::Browser : Focus::Terminal;
        return;
    }
    if (action == "toggle_browser") {
        put("browser.show", browserVisible() ? "false" : "true");
        if (!browserVisible()) focus_ = Focus::Terminal;
        return;
    }
    if (action == "toggle_layout") {
        put("browser.layout", stacked() ? "split" : "stacked");
        return;
    }
    if (action == "toggle_hidden") {
        if (put("browser.show_hidden", config_.browser().showHidden ? "false" : "true")) {
            browser_.refresh(config_.browser());
            say(config_.browser().showHidden ? "showing hidden files" : "hiding hidden files");
        }
        return;
    }
    if (action == "browser_back") {
        if (!browser_.goBack(config_.browser())) say("nowhere to go back to");
        focus_ = Focus::Browser;
        return;
    }
    if (action == "browser_forward") {
        if (!browser_.goForward(config_.browser())) say("nowhere to go forward to");
        focus_ = Focus::Browser;
        return;
    }
    if (action == "browser_up") {
        browser_.goUp(config_.browser());
        focus_ = Focus::Browser;
        return;
    }
    if (action == "browser_filter") {
        if (!browserVisible()) put("browser.show", "true");
        focus_ = Focus::Browser;
        browser_.beginFilter();
        return;
    }
    if (action == "browser_sort") {
        static const std::vector<std::string> order = {"name", "size", "modified", "type"};
        const auto at = std::find(order.begin(), order.end(), config_.browser().sort);
        const std::size_t next = at == order.end() ? 0 : (at - order.begin() + 1) % order.size();
        if (put("browser.sort", order[next])) {
            browser_.refresh(config_.browser());
            say("sorted by " + order[next]);
        }
        return;
    }
    if (action == "browser_reverse") {
        if (put("browser.sort_reverse", config_.browser().sortReverse ? "false" : "true")) {
            browser_.refresh(config_.browser());
            say(config_.browser().sortReverse ? "reversed" : "back to ascending");
        }
        return;
    }
    if (action == "copy_path") {
        if (const Connection* conn = config_.connection(browser_.connection())) {
            copyToClipboard(conn->label() + ":" + browser_.where());
        } else {
            copyToClipboard(browser_.path().string());
        }
        return;
    }
    if (action == "paste_path") {
        std::string wanted = clipboard();
        while (!wanted.empty() && (wanted.back() == '\n' || wanted.back() == '\r')) {
            wanted.pop_back();
        }
        if (wanted.empty()) { say("the clipboard is empty"); return; }

        // copy_path writes scp form, so take it back that way too.
        if (const Connection* conn = config_.connection(browser_.connection())) {
            const std::string prefix = conn->label() + ":";
            if (wanted.rfind(prefix, 0) == 0) wanted.erase(0, prefix.size());
            act("cd", {wanted});
            say("went to " + wanted);
            return;
        }

        fs::path target = paths::expandUser(wanted);
        std::error_code ec;
        if (fs::is_regular_file(target, ec)) target = target.parent_path();
        if (!fs::is_directory(target, ec)) {
            say("not a directory: " + elide(wanted, 40), true);
            return;
        }
        act("cd", {target.string()});
        say("went to " + paths::contractUser(target));
        return;
    }
    if (action == "grow_pane") {
        const Layout layout = measure();
        layout::Request request;
        request.width = layout.width;
        request.gaps = config_.decoration().gaps;
        request.border = config_.decoration().border;
        put("browser.width",
            std::to_string(std::min(browserWidth() + 4, layout::maxBrowserWidth(request))));
        return;
    }
    if (action == "shrink_pane") {
        put("browser.width", std::to_string(std::max(16, browserWidth() - 4)));
        return;
    }

    if (action == "new_tab") { newTab(nullptr); return; }
    if (action == "close_tab") { closeTab(tab_); return; }
    if (action == "next_tab") {
        if (!tabs_.empty()) tab_ = (tab_ + 1) % static_cast<int>(tabs_.size());
        return;
    }
    if (action == "prev_tab") {
        if (!tabs_.empty()) {
            tab_ = (tab_ + static_cast<int>(tabs_.size()) - 1) % static_cast<int>(tabs_.size());
        }
        return;
    }

    if (action == "open_config") { configView_.open(); return; }
    if (action == "setup") { onboard_.start(); return; }
    if (action == "help") { helpOpen_ = true; return; }
    if (action == "quit") {
        if (config_.general().confirmQuit) {
            if (const term::Session* running = busyTab()) {
                askConfirm({"Something is still running in " + running->title() + ".",
                            "Leaving now stops it.",
                            [this] { quitting_ = true; screen_.Exit(); }, "quit anyway",
                            "stay"});
                return;
            }
        }
        quitting_ = true;
        screen_.Exit();
        return;
    }
    if (action == "reload_config") {
        config_.load();
        applyConfig();
        say(config_.issues().empty()
                ? "reloaded " + paths::contractUser(config_.path())
                : std::to_string(config_.issues().size()) + " problems — see the config screen",
            !config_.issues().empty());
        return;
    }

    if (!session) return;

    if (action == "scroll_up") { session->scrollBy(config_.terminal().scrollLines); return; }
    if (action == "scroll_down") { session->scrollBy(-config_.terminal().scrollLines); return; }
    if (action == "scroll_top") { session->scrollToTop(); return; }
    if (action == "scroll_bottom") { session->scrollToBottom(); return; }
    if (action == "prev_prompt" || action == "next_prompt") {
        if (!session->jumpPrompt(action == "prev_prompt" ? -1 : 1)) {
            say("no prompt marks — run `apollo setup` to add shell integration");
        }
        return;
    }
    if (action == "copy") {
        const std::string text = session->selectedText();
        if (text.empty()) say("nothing selected");
        else copyToClipboard(text);
        return;
    }
    if (action == "paste") { session->paste(clipboard()); return; }
    if (action == "clear") { session->sendText("\f"); return; }
    if (action == "search") {
        searching_ = true;
        searchQuery_.clear();
        searchHits_.clear();
        return;
    }

    if (action == "edit_config") {
        const char* fromEnv = std::getenv("EDITOR");
        const std::string editor = !config_.general().editor.empty() ? config_.general().editor
                                   : (fromEnv && *fromEnv)          ? fromEnv
                                                                    : "";
        if (editor.empty()) { say("set general.editor, or $EDITOR, first", true); return; }
        session->sendText(editor + " " + process::shellQuote(config_.path().string()) + "\r");
        focus_ = Focus::Terminal;
        return;
    }
    if (action == "connect") {
        std::string error;
        const auto conn = config_.resolveConnection(argument, error);
        if (!conn) {
            // Several configured and none named. Show them instead of telling
            // you to go look them up.
            if (argument.empty() && config_.connections().size() > 1) {
                pickConnection();
                return;
            }
            say(error.substr(0, error.find('\n')), true);
            return;
        }
        if (!newTab(&*conn)) return;
        say("connecting to " + conn->describe());
        askRemote(conn->name, conn->remoteDir.empty() ? "~" : conn->remoteDir);
        return;
    }
    if (action == "disconnect") {
        if (session->connection().empty()) { say("this tab is already local"); return; }
        const std::string was = session->connection();

        // Tab opened for a connection goes away with it. Unless it's the only
        // tab, since that would quit Apollo out from under you.
        if (config_.general().disconnectClosesTab && tabs_.size() > 1) {
            closeTab(tab_);
        } else if (!retarget(tab_, nullptr)) {
            return;
        }
        followActiveMachine();
        say("disconnected from " + was);
        return;
    }

    if (action == "transfer") { transferSelected(); return; }

    if (action == "open_with") {
        const BrowserView::Entry* entry = browser_.selected();
        if (!entry || entry->directory) { say("select a file first"); return; }
        openFile(fs::path(browser_.where()) / entry->name, true);
        return;
    }

    if (action == "run") {
        if (const Command* command = registry_.find(argument)) runCommand(*command);
        else say("no command called '" + argument + "'", true);
        return;
    }
    if (action == "exec") {
        std::string line = argument;
        for (std::size_t i = 1; i < args.size(); ++i) line += " " + args[i];
        session->sendText(line + "\r");
        focus_ = Focus::Terminal;
        return;
    }
    if (action == "cd") {
        if (!session->connection().empty()) {
            const std::string where = argument.empty() ? "~" : argument;
            askRemote(session->connection(), where);
            syncShell(session->connection(), where);
            return;
        }
        browser_.setPath(paths::expandUser(argument));
        browser_.refresh(config_.browser());
        syncShell("", browser_.path().string());
        return;
    }
}

void App::handleControl(const std::string& message) {
    const auto space = message.find(' ');
    const std::string verb = message.substr(0, space);
    const std::string argument = space == std::string::npos ? "" : message.substr(space + 1);

    if (verb == "home") {
        act("cd", {config_.general().workspace});
        say("home");
        return;
    }
    if (verb == "open" && !argument.empty()) { act("cd", {argument}); return; }
    if (verb == "quit") { act("quit", {}); return; }
    if (verb == "config") { configView_.open(); return; }
    if (verb == "setup") { onboard_.start(); return; }
    if (verb == "help") { helpOpen_ = true; return; }
    if (verb == "new-tab") { act("new_tab", {}); return; }
    if (verb == "connect") { act("connect", {argument}); return; }
    if (verb == "disconnect") { act("disconnect", {}); return; }
    if (verb == "reload") { act("reload_config", {}); return; }

    say("Apollo did not understand '" + elide(message, 40) + "'", true);
}

bool App::runBind(const KeyChord& chord) {
    for (const auto& bind : config_.binds()) {
        if (bind.chord == chord) {
            act(bind.action, bind.args);
            return true;
        }
    }
    return false;
}

// --- opening a file --------------------------------------------------------

void App::runOpener(const std::string& how, const fs::path& path) {
    term::Session* session = active();
    if (!session) return;

    const bool remote = browser_.remote();
    const std::string quoted =
        remote ? ssh::quoteRemotePath(path.string()) : process::shellQuote(path.string());

    if (how == "desktop") {
        // This machine can't open a file that lives on another one.
        if (remote) {
            say("that file is on " + browser_.connection() + " — opening it there instead");
            session->sendText("${EDITOR:-vi} " + quoted + "\r");
            focus_ = Focus::Terminal;
            return;
        }
        if (process::openWithDesktop(path.string())) say("opened " + path.filename().string());
        else say("could not open " + path.filename().string(), true);
        return;
    }

    std::string program = how;
    if (how == "editor") {
        const char* fromEnv = std::getenv("EDITOR");
        program = !config_.general().editor.empty() ? config_.general().editor
                  : (fromEnv && *fromEnv)           ? fromEnv
                  : remote                          ? "${EDITOR:-vi}"
                                                    : "vi";
    }

    session->sendText(program + " " + quoted + "\r");
    focus_ = Focus::Terminal;
}

void App::openFile(const fs::path& path, bool alwaysAsk) {
    const OpenRule* rule = config_.openRuleFor(path.filename().string());
    const std::string how = rule ? rule->how : "ask";
    if (alwaysAsk || how == "ask") { askHowToOpen(path); return; }
    runOpener(how, path);
}

void App::askHowToOpen(const fs::path& path) {
    const std::string name = path.filename().string();
    const std::string key = Config::openKeyFor(name);

    struct Choice {
        std::string title;
        std::string how;
        std::string subtitle;
    };
    std::vector<Choice> choices;

    const char* fromEnv = std::getenv("EDITOR");
    const std::string configured = config_.general().editor;
    if (!configured.empty()) choices.push_back({configured, "editor", "your general.editor"});
    else if (fromEnv && *fromEnv) choices.push_back({fromEnv, "editor", "your $EDITOR"});

    for (const auto& [program, what] : std::vector<std::pair<std::string, std::string>>{
             {"vim", "in the terminal"},
             {"nvim", "in the terminal"},
             {"nano", "in the terminal"},
             {"less", "page through it"},
         }) {
        if (program == configured) continue;
        // Locally we can check what's installed. Over ssh we can't.
        if (!browser_.remote() && !process::which(program)) continue;
        choices.push_back({program, program, what});
    }

    if (!browser_.remote()) {
        choices.push_back({"Desktop", "desktop", "whatever this machine uses"});
    }

    std::vector<Palette::Item> items;
    for (const auto& choice : choices) {
        std::string subtitle = choice.subtitle;
        if (!key.empty()) subtitle += " — and for ." + key + " from now on";
        items.push_back({choice.title, subtitle, "open", "",
                         [this, path, how = choice.how, key] {
                             if (!key.empty()) {
                                 std::string problem;
                                 if (config_.setOpenRule(key, how) && config_.save(&problem)) {
                                     say("." + key + " files open with " + how +
                                         " — " + keyHintFor("open_config") + " to change it");
                                 } else if (!problem.empty()) {
                                     say(problem, true);
                                 }
                             }
                             runOpener(how, path);
                         }});
    }

    if (!key.empty()) {
        items.push_back({"Ask every time", "keep choosing for ." + key + " files", "open", "",
                         [this, key] {
                             std::string problem;
                             config_.setOpenRule(key, "ask");
                             config_.save(&problem);
                         }});
    }

    palette_.open(std::move(items), "Open " + elide(name, 28) + " with");
}

void App::pickConnection() {
    std::vector<Palette::Item> items;
    for (const auto& conn : config_.connections()) {
        std::string detail = conn.describe();
        if (!conn.remoteDir.empty()) detail += "  " + elidePath(conn.remoteDir, 24);
        detail += conn.keyPath.empty() ? (conn.password.empty() ? "  agent" : "  password")
                                       : "  key";
        items.push_back({conn.name, detail, "ssh", "",
                         [this, name = conn.name] { act("connect", {name}); }});
    }
    palette_.open(std::move(items), "Connect to");
}

void App::openPalette() {
    std::vector<Palette::Item> items;

    for (const auto& command : registry_.all()) {
        items.push_back({command.name, command.summary, command.kindLabel(), "",
                         [this, name = command.name] {
                             if (const Command* found = registry_.find(name)) runCommand(*found);
                         }});
    }

    for (const auto& action : knownActions()) {
        if (action.takesArgument) continue; // those need a target, offered below
        items.push_back({action.summary, action.name, "", keyHintFor(action.name),
                         [this, name = action.name] { act(name, {}); }});
    }

    for (const auto& conn : config_.connections()) {
        items.push_back({"Connect to " + conn.name, conn.describe(), "ssh", "",
                         [this, name = conn.name] { act("connect", {name}); }});
    }

    for (const auto& name : Config::availableThemes()) {
        items.push_back({"Theme: " + name, "Switch the color scheme", "theme", "",
                         [this, name] {
                             std::string problem;
                             if (config_.set("decoration.theme", name, &problem) &&
                                 config_.save(&problem)) {
                                 applyConfig();
                                 say("theme: " + name);
                             } else {
                                 say(problem, true);
                             }
                         }});
    }

    palette_.open(std::move(items));
}

// --- events ----------------------------------------------------------------

std::chrono::milliseconds App::tickInterval() const {
    // The only reasons to beat quickly are things that move on their own.
    if (boot_.running() || browser_.loading() || leaderArmed_) {
        return std::chrono::milliseconds(100);
    }
    if (transfers_.running() > 0) return std::chrono::milliseconds(500);

    const term::Session* session = active();
    if (session && session->commandRunning()) {
        // The spinner needs a frame rate. Without it there is only the elapsed
        // seconds to keep up with.
        return std::chrono::milliseconds(config_.decoration().animate ? 100 : 500);
    }
    return std::chrono::milliseconds(250);
}

bool App::tick() {
    const auto now = std::chrono::steady_clock::now();

    // Two stats. Worth doing often enough to feel live, not ten times a second.
    if (now - configCheckedAt_ > std::chrono::seconds(1)) {
        configCheckedAt_ = now;
        if (config_.reloadIfChanged()) {
            applyConfig();
            say("reloaded " + paths::contractUser(config_.path()));
        }
    }
    if (browserVisible() && browser_.refreshIfStale(config_.browser())) invalidate();

    // A remote listing is a round trip, so it gets a slow beat instead of the
    // local mtime poll. Plus one right after the terminal settles, which is
    // when a cd has usually just happened.
    if (const term::Session* session = active(); session && !session->connection().empty()) {
        const std::uint64_t revision = session->screen().revision();
        if (revision != lastRevision_) {
            lastRevision_ = revision;
            remoteSettleAt_ = now + std::chrono::milliseconds(400);
        }
    }
    const bool settled = remoteSettleAt_.time_since_epoch().count() != 0 && now > remoteSettleAt_;
    if (browser_.remote() && browserVisible() && !browser_.loading() &&
        (settled || now - remoteRefreshed_ > std::chrono::seconds(5))) {
        remoteSettleAt_ = {};
        remoteRefreshed_ = now;
        askRemote(browser_.connection(), browser_.where(), false);
    }

    for (auto& session : tabs_) {
        if (!session->screen().bellPending) continue;
        session->screen().bellPending = false;
        if (config_.terminal().bell) say("bell — " + session->title());
    }

    for (auto& session : tabs_) {
        if (session->pump()) invalidate();
    }

    followActiveMachine();

    if (config_.general().followCwd) {
        if (term::Session* session = active(); session && !session->cwd().empty()) {
            // A connected shell reports a remote path. Treating that as local
            // is meaningless, or worse, accidentally a real local directory.
            // Only follow a shell the pane is actually pointed at. Browsing
            // somewhere else shouldn't get yanked back.
            if (!session->connection().empty()) {
                if (browser_.connection() == session->connection() &&
                    session->cwd() != browser_.where() && session->cwd() != followedCwd_ &&
                    !browser_.loading()) {
                    followedCwd_ = session->cwd();
                    askRemote(session->connection(), session->cwd());
                }
            } else if (!browser_.remote()) {
                std::error_code ec;
                if (session->cwd() != lastShellCwd_) {
                    lastShellCwd_ = session->cwd();
                    lastShellCwdResolved_ = fs::weakly_canonical(lastShellCwd_, ec);
                }
                const fs::path& reported = lastShellCwdResolved_;

                // Ignore the shell's cwd until it catches up with our cd.
                if (!pendingCwd_.empty()) {
                    if (reported == pendingCwd_ ||
                        std::chrono::steady_clock::now() > pendingCwdUntil_) {
                        pendingCwd_.clear();
                    }
                } else if (reported != browser_.path() && fs::is_directory(reported, ec)) {
                    browser_.setPath(reported);
                    browser_.refresh(config_.browser());
                    invalidate();
                }
            }
        }
    }

    if (!status_.empty() && now > statusUntil_) {
        status_.clear();
        invalidate();
    }

    for (int i = static_cast<int>(tabs_.size()) - 1; i >= 0; --i) {
        auto& session = tabs_[static_cast<std::size_t>(i)];
        if (session->running()) continue;

        if (session->exitCode() == 0 || tabs_.size() > 1) {
            closeTab(i);
            invalidate();
        } else if (status_.empty()) {
            say(session->title() + " exited " + std::to_string(session->exitCode()) +
                    " — " + keyHintFor("new_tab") + " for a shell, " + keyHintFor("quit") +
                    " to leave",
                true);
        }
    }

    tickMs_ = static_cast<int>(tickInterval().count());

    // The splash, and the spinner and elapsed seconds of a running command, are
    // the only things that move on their own. Everything else waits for news.
    const term::Session* session = active();
    // A held leader grows its list of keys after a moment; a transfer counts seconds.
    const bool leaderKeysDue =
        leaderArmed_ && std::chrono::steady_clock::now() - leaderArmedAt_ >= kLeaderKeysDelay &&
        std::chrono::steady_clock::now() - leaderArmedAt_ < kLeaderKeysDelay * 2;
    const bool animating = boot_.running() || (session && session->commandRunning()) ||
                           transfers_.running() > 0 || leaderKeysDue;
    return std::exchange(dirty_, false) || animating;
}

bool App::onMouse(const Event& event) {
    const Layout layout = measure();
    const Mouse& mouse = const_cast<Event&>(event).mouse();

    const DecorationSettings& decoration = layout.decoration;
    const int frame = decoration.border == "none" ? 0 : 1;
    const int titleRows = decoration.titleBar ? 2 : 0;
    const int topOffset =
        (tabs_.size() > 1 && decoration.statusBar ? 1 : 0) + frame + titleRows;

    const bool browserOnLeft = config_.browser().position != "right";
    const int browserLeft = browserOnLeft ? 0 : layout.width - layout.browserWidth;
    const int browserRight = browserLeft + layout.browserWidth;
    const bool inBrowserColumns =
        layout.browserWidth > 0 && !layout.stacked && mouse.x >= browserLeft &&
        mouse.x < browserRight;

    // Covers both borders and the gap so it's still grabbable at gaps = 0.
    const int divider = dividerColumn(layout);
    const bool onDivider =
        divider >= 0 && mouse.x >= divider && mouse.x <= divider + decoration.gaps + 1;

    // --- resizing ---------------------------------------------------------
    if (draggingDivider_) {
        if (mouse.motion == Mouse::Released) {
            draggingDivider_ = false;
            if (dragWidth_ >= 0) {
                const int settled = dragWidth_;
                dragWidth_ = -1;
                put("browser.width", std::to_string(settled));
            }
        } else {
            layout::Request request;
            request.width = layout.width;
            request.gaps = decoration.gaps;
            request.border = decoration.border;

            const int wanted = browserOnLeft ? mouse.x + 1 : layout.width - mouse.x;
            dragWidth_ = std::clamp(wanted, 16, layout::maxBrowserWidth(request));
        }
        return true;
    }

    if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed && onDivider) {
        draggingDivider_ = true;
        dragWidth_ = layout.browserWidth;
        return true;
    }

    // --- the wheel --------------------------------------------------------
    if (mouse.button == Mouse::WheelUp || mouse.button == Mouse::WheelDown) {
        const int direction = mouse.button == Mouse::WheelUp ? 1 : -1;
        if (inBrowserColumns) {
            browser_.scrollBy(-direction * 3);
            return true;
        }
        if (term::Session* session = active()) {
            if (session->screen().mouseTracking != 0 && !session->scrolled()) {
                session->sendMouse(direction > 0 ? 64 : 65, mouse.x, mouse.y - topOffset, true,
                                   false, ModNone);
            } else {
                session->scrollBy(direction * config_.terminal().scrollLines);
            }
        }
        return true;
    }

    // --- hover ------------------------------------------------------------
    if (inBrowserColumns) {
        browser_.hover(mouse.y - topOffset, mouse.x - browserLeft - frame, config_.browser());
    } else {
        browser_.clearHover();
    }

    if (mouse.button != Mouse::Left) return true;

    // --- the tab strip ----------------------------------------------------
    if (tabs_.size() > 1 && decoration.statusBar && mouse.y == 0) {
        if (mouse.motion != Mouse::Pressed) return true;
        const std::vector<int> edges = tabEdges();
        for (std::size_t i = 0; i + 1 < edges.size(); ++i) {
            if (mouse.x >= edges[i] && mouse.x < edges[i + 1]) {
                tab_ = static_cast<int>(i);
                focus_ = Focus::Terminal;
                break;
            }
        }
        return true;
    }

    // --- the browser ------------------------------------------------------
    if (inBrowserColumns) {
        focus_ = Focus::Browser;
        if (mouse.motion == Mouse::Pressed) {
            // Terminals don't report double clicks. Time them ourselves.
            const int row = mouse.y - topOffset;
            const int column = mouse.x - browserLeft - frame;
            const auto now = std::chrono::steady_clock::now();
            const bool doubleClick = row == lastClickRow_ &&
                                     now - lastClick_ < std::chrono::milliseconds(400);
            browser_.onClick(row, column, doubleClick, config_.browser());
            // Reset after acting or a third click counts as another pair.
            lastClick_ = doubleClick ? std::chrono::steady_clock::time_point{} : now;
            lastClickRow_ = doubleClick ? -1 : row;
        }
        return true;
    }

    // --- the terminal -----------------------------------------------------
    term::Session* session = active();
    if (!session) return true;
    focus_ = Focus::Terminal;

    const int terminalLeft = browserOnLeft ? browserRight + decoration.gaps + frame : frame;
    const int column = mouse.x - terminalLeft;
    const int row = mouse.y - topOffset;
    if (column < 0 || row < 0 || row >= layout.terminalRows) return true;

    // Program asked for mouse reporting? It gets the event. Otherwise it's a selection.
    if (session->screen().mouseTracking != 0 && !session->scrolled()) {
        const int button = mouse.motion == Mouse::Released ? 3 : 0;
        session->sendMouse(button, column, row, mouse.motion != Mouse::Released,
                           mouse.motion == Mouse::Moved, ModNone);
        return true;
    }

    const int absolute = session->screen().totalLines() - session->scrollOffset() -
                         layout.terminalRows + row;
    if (mouse.motion == Mouse::Pressed) {
        session->selection.active = true;
        session->selection.anchorLine = absolute;
        session->selection.anchorCol = column;
        session->selection.headLine = absolute;
        session->selection.headCol = column;
    } else if (mouse.motion == Mouse::Moved && session->selection.active) {
        session->selection.headLine = absolute;
        session->selection.headCol = column;
    } else if (mouse.motion == Mouse::Released) {
        session->selection.headLine = absolute;
        session->selection.headCol = column;
        if (config_.terminal().copyOnSelect && !session->selection.empty()) {
            copyToClipboard(session->selectedText());
        }
    }
    return true;
}

bool App::onEvent(const Event& event) {
    // The frame was already invalidated by the event arriving. Nothing to do.
    if (event == kRepaint) return true;
    if (event == kControl) {
        for (const auto& message : control_.take()) handleControl(message);
        return true;
    }
    if (event == kTransfer) {
        finishTransfers();
        return true;
    }
    if (event == kRemote) {
        if (const auto listing = remoteLister_.take()) {
            if (listing->ok) lastRemote_ = listing->connection;
            browser_.showRemote(*listing, config_.browser());
            if (!listing->ok && listing->connection == browser_.connection()) {
                say(listing->error, true);
            }
            // Shell moved, so follow it. Same as OSC 7 does locally.
            // Only a change counts, or browsing elsewhere gets undone.
            if (config_.general().followCwd && !listing->shellCwd.empty() &&
                listing->shellCwd != followedCwd_) {
                const bool alreadyThere = listing->shellCwd == listing->path;
                followedCwd_ = listing->shellCwd;
                if (!alreadyThere) askRemote(listing->connection, listing->shellCwd, false);
            }
        }
        return true;
    }
    // Whatever's on top gets the mouse. Otherwise clicks land on the panes
    // hiding behind it.
    if (event.is_mouse()) {
        const Mouse& mouse = const_cast<Event&>(event).mouse();
        const bool click = mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed;
        if (confirm_) {
            if (click) {
                const int hit = confirmSpots_.at(mouse.x, mouse.y);
                auto yes = confirm_->onYes;
                if (hit == kConfirmYes) { confirm_.reset(); if (yes) yes(); }
                else if (hit == kConfirmNo) { confirm_.reset(); }
            }
            return true;
        }
        if (boot_.running()) {
            if (click) boot_.dismiss();
            return true;
        }
        if (onboard_.isOpen()) return onboard_.onMouse(mouse);
        if (transferForm_.isOpen()) {
            if (transferForm_.onMouse(mouse) == TransferForm::Result::Submit) submitTransfer();
            return true;
        }
        if (configView_.isOpen()) return configView_.onMouse(mouse);
        if (palette_.isOpen()) return palette_.onMouse(mouse);
        if (helpOpen_) {
            if (click) helpOpen_ = false;
            return true;
        }
        return onMouse(event);
    }

    const std::string raw = event.input();
    const KeyChord chord = decodeKey(raw);

    if (confirm_) {
        if (chord.key == "enter" || chord.key == "y") {
            auto yes = confirm_->onYes;
            confirm_.reset();
            if (yes) yes();
        } else if (chord.key == "escape" || chord.key == "n" ||
                   (chord.mods == ModCtrl && chord.key == "c")) {
            confirm_.reset();
        }
        return true; // nothing leaks past a question
    }

    if (boot_.running()) {
        boot_.dismiss();
        return true;
    }

    if (onboard_.isOpen()) {
        onboard_.onKey(chord, raw);
        return true;
    }
    if (transferForm_.isOpen()) {
        if (transferForm_.onKey(chord, raw) == TransferForm::Result::Submit) submitTransfer();
        return true;
    }
    if (configView_.isOpen()) return configView_.onKey(chord, raw);
    if (palette_.isOpen()) return palette_.onKey(chord, raw);

    if (helpOpen_) {
        helpOpen_ = false;
        return true;
    }

    if (searching_) {
        if (chord.key == "escape") { searching_ = false; searchQuery_.clear(); return true; }
        if (chord.key == "enter") {
            if (!searchHits_.empty()) {
                searchAt_ = (searchAt_ + 1) % static_cast<int>(searchHits_.size());
                if (term::Session* session = active()) {
                    session->scrollToLine(searchHits_[static_cast<std::size_t>(searchAt_)]);
                }
            }
            return true;
        }
        if (searchQuery_.onKey(chord, raw)) {
            if (term::Session* session = active()) {
                searchHits_ = session->search(searchQuery_.text);
                searchAt_ = 0;
                if (!searchHits_.empty()) session->scrollToLine(searchHits_.back());
            }
        }
        return true;
    }

    if (leaderArmed_) {
        leaderArmed_ = false;
        if (chord.key == "escape" && chord.mods == ModNone) return true; // never mind

        KeyChord withLeader = chord;
        withLeader.mods |= ModLeader;
        if (runBind(withLeader)) return true;

        // The same key twice sends it through, for the program that wanted it.
        if (chord == leaderChord_) {
            deliver(leaderChord_, leaderRaw_);
            return true;
        }
        // A Space that turned out not to be for Apollo was just a space. Type
        // it, then whatever came after, and nothing is lost.
        if (leaderSoft_) {
            deliver(leaderChord_, leaderRaw_);
            if (!chord.empty() && runBind(chord)) return true;
            deliver(chord, raw);
            return true;
        }
        say("nothing bound to " + withLeader.describe());
        return true;
    }
    if (const LeaderPress press = leaderPress(chord); press != LeaderPress::None) {
        leaderArmed_ = true;
        leaderSoft_ = press == LeaderPress::Soft;
        leaderChord_ = chord;
        leaderRaw_ = raw;
        leaderArmedAt_ = std::chrono::steady_clock::now();
        return true;
    }

    if (!chord.empty() && runBind(chord)) return true;
    deliver(chord, raw);
    return true;
}

void App::deliver(const KeyChord& chord, const std::string& raw) {
    if (focus_ == Focus::Browser) {
        if (browser_.filtering()) {
            browser_.onFilterKey(chord, raw, config_.browser());
            return;
        }
        if (chord.key == "tab") { focus_ = Focus::Terminal; return; }
        if (browser_.onKey(chord, config_.browser())) return;
        // Anything the browser doesn't want goes to the terminal. Typing never vanishes.
        focus_ = Focus::Terminal;
    }

    if (term::Session* session = active()) {
        if (!session->selection.empty()) session->selection = term::Selection{};
        session->sendKey(chord, raw);
    }
}

// --- the leader ------------------------------------------------------------

App::LeaderPress App::leaderPress(const KeyChord& chord) const {
    const KeyChord& leader = config_.general().leader;
    if (chord.empty()) return LeaderPress::None;

    // These are the leader everywhere, even inside vim.
    for (const auto& anywhere : config_.general().leaderAnywhere) {
        if (chord == anywhere) return LeaderPress::Hard;
    }
    if (!leader.typesText()) return chord == leader ? LeaderPress::Hard : LeaderPress::None;

    // A leader that types something can't take every press of it.
    if (chord != leader) return LeaderPress::None;
    return leaderIsFree() ? LeaderPress::Soft : LeaderPress::None;
}

bool App::leaderIsFree() const {
    if (focus_ == Focus::Browser && browserVisible()) {
        // Mid-filter or mid-name, a space is part of what you're typing.
        return !browser_.filtering() && browser_.findExpired();
    }
    const term::Session* session = active();
    return session && session->atEmptyPrompt();
}

Element App::renderLeaderKeys(int width) {
    std::vector<Bind> binds;
    for (const auto& bind : config_.binds()) {
        if (bind.chord.mods & ModLeader) binds.push_back(bind);
    }
    // Letters and symbols first, then the named keys: the order you'd look.
    std::stable_sort(binds.begin(), binds.end(), [](const Bind& a, const Bind& b) {
        const bool aShort = a.chord.key.size() == 1;
        const bool bShort = b.chord.key.size() == 1;
        if (aShort != bShort) return aShort;
        return a.chord < b.chord;
    });
    if (binds.empty()) return text("");

    const int panelWidth = std::min(width - 2, 100);
    const int columns = std::clamp((panelWidth - 2) / 24, 1, 4);
    const int columnWidth = (panelWidth - 2) / columns;
    const int perColumn = (static_cast<int>(binds.size()) + columns - 1) / columns;

    Elements rows;
    for (int i = 0; i < perColumn; ++i) {
        Elements cells;
        for (int c = 0; c < columns; ++c) {
            const std::size_t index = static_cast<std::size_t>(i + c * perColumn);
            if (index >= binds.size()) {
                cells.push_back(text("") | size(WIDTH, EQUAL, columnWidth));
                continue;
            }
            KeyChord bare = binds[index].chord;
            bare.mods &= static_cast<std::uint8_t>(~ModLeader);
            std::string key = bare.describe();
            key.resize(std::max<std::size_t>(key.size(), 7), ' ');
            const std::string what = shortLabel(binds[index]);
            cells.push_back(hbox({
                text(" " + key) | color(toFtx(theme_->accent)) | bold,
                text(elide(what, columnWidth - 9)) | color(toFtx(theme_->fg)),
                filler(),
            }) | size(WIDTH, EQUAL, columnWidth));
        }
        rows.push_back(hbox(std::move(cells)));
    }

    const std::string title = " " + leaderChord_.describe() + " then… ";
    const std::string footer = leaderSoft_ ? "any other key types " + leaderChord_.describe() +
                                                 " and itself · Esc cancels"
                                           : "Esc cancels";
    Element box = clear_under(window(text(title) | bold | color(toFtx(theme_->accent)),
                         vbox({vbox(std::move(rows)),
                               text(" " + footer) | color(toFtx(theme_->muted))}))) |
                  color(toFtx(theme_->border)) | bgcolor(toFtx(theme_->surface)) |
                  size(WIDTH, EQUAL, panelWidth);
    // Sits above the status bar, out of the way of whatever you were reading.
    return vbox({filler(), hbox({filler(), std::move(box), filler()}), text(""), text("")});
}

// --- moving files between machines -----------------------------------------

std::string App::uploadDirFor(const Connection& conn) {
    return conn.remoteDir.empty() ? "~" : conn.remoteDir;
}

std::vector<std::string> App::uploadChoices() const {
    // The machine you were last looking at, then any others open in a tab,
    // then the rest: most likely first.
    std::vector<std::string> order;
    const auto add = [&](const std::string& name) {
        if (!name.empty() && config_.connection(name) &&
            std::find(order.begin(), order.end(), name) == order.end()) {
            order.push_back(name);
        }
    };
    add(lastRemote_);
    for (const auto& session : tabs_) add(session->connection());
    for (const auto& conn : config_.connections()) add(conn.name);
    return order;
}

void App::transferSelected() {
    const BrowserView::Entry* entry = browser_.selected();
    if (!entry) { say("select a file in the browser first"); return; }
    const std::string source = (fs::path(browser_.where()) / entry->name).string();

    if (browser_.remote()) {
        const Connection* conn = config_.connection(browser_.connection());
        if (!conn) return;
        // Down to wherever this machine's side of the pane was last.
        openTransfer({*conn, transfer::Direction::Download, {source},
                      paths::contractUser(browser_.path())},
                     entry->directory);
        return;
    }

    if (config_.connections().empty()) {
        say("no SSH destinations yet — add one with " + keyHintFor("open_config") +
                ", then Connections",
            true);
        return;
    }
    // One destination is unambiguous. With more, you always say which.
    if (config_.connections().size() == 1) {
        const Connection& conn = config_.connections().front();
        openTransfer({conn, transfer::Direction::Upload, {paths::contractUser(source)},
                      uploadDirFor(conn)},
                     entry->directory);
        return;
    }

    std::vector<Palette::Item> items;
    const bool directory = entry->directory;
    for (const auto& name : uploadChoices()) {
        const Connection conn = *config_.connection(name);
        const std::string where = uploadDirFor(conn);
        std::string detail = conn.describe() + "  " + where;
        for (const auto& session : tabs_) {
            if (session->connection() == name) { detail += "  (open)"; break; }
        }
        items.push_back({conn.name, detail, "ssh", "", [this, conn, source, directory] {
                             openTransfer({conn, transfer::Direction::Upload,
                                           {paths::contractUser(source)}, uploadDirFor(conn)},
                                          directory);
                         }});
    }
    palette_.open(std::move(items), "Send " + elide(entry->name, 28) + " to");
}

void App::openTransfer(transfer::Job job, bool directory) {
    transferForm_.open(job, directory);
    invalidate();
}

void App::submitTransfer() {
    transfer::Job job = transferForm_.job();
    // scp gets this side's paths as they are, with no shell to expand ~.
    if (job.direction == transfer::Direction::Upload) {
        for (auto& source : job.sources) source = paths::expandUser(source);
    } else {
        job.destination = paths::expandUser(job.destination);
    }
    if (const std::string problem = transfer::checkLocal(job); !problem.empty()) {
        transferForm_.fail(problem);
        return;
    }
    transferForm_.checking(transfers_.verify(job));
}

void App::finishTransfers() {
    // Checks first: one that passed starts the copy it was checking.
    for (const auto& verdict : transfers_.takeVerdicts()) {
        if (!transferForm_.isOpen() || verdict.id != transferForm_.checkId()) continue;
        if (!verdict.error.empty()) {
            transferForm_.fail(verdict.error);
            continue;
        }
        transferForm_.close();
        transfers_.start(verdict.job);
        say((verdict.job.direction == transfer::Direction::Upload ? "sending " : "fetching ") +
            transfer::describe(verdict.job) + "…");
    }

    for (const auto& outcome : transfers_.take()) {
        const bool upload = outcome.job.direction == transfer::Direction::Upload;
        const std::string what = transfer::describe(outcome.job);
        if (!outcome.ok) {
            say(std::string(upload ? "could not send " : "could not fetch ") + what + ": " +
                    outcome.error,
                true);
            continue;
        }

        const auto seconds =
            std::chrono::duration_cast<std::chrono::seconds>(outcome.took).count();
        const std::string where =
            upload ? outcome.job.conn.name + ":" +
                         elidePath(outcome.job.destination.empty() ? "~" : outcome.job.destination, 28)
                   : elidePath(paths::contractUser(outcome.job.destination), 28);
        say(std::string(upload ? "sent " : "fetched ") + what + " to " + where +
            (seconds > 0 ? " in " + std::to_string(seconds) + "s" : ""));

        // Show the new arrival if the pane is looking at where it landed.
        if (upload && browser_.connection() == outcome.job.conn.name) {
            askRemote(browser_.connection(), browser_.where(), false);
        } else if (!upload && !browser_.remote()) {
            browser_.refresh(config_.browser());
        }
    }
    invalidate();
}

// --- rendering -------------------------------------------------------------

namespace {

std::string tabLabel(const term::Session& session, std::size_t index) {
    std::string label = " " + std::to_string(index + 1) + " " + session.title();
    if (!session.windowTitle().empty() && session.windowTitle() != session.title()) {
        label += ": " + elide(session.windowTitle(), 24);
    }
    return label + " ";
}

} // namespace

// How the current location should read. Bare path locally, user@host:path
// once the pane is showing a remote.
std::string App::whereLabel() const {
    if (const Connection* conn = config_.connection(browser_.connection())) {
        return conn->label() + ":" + browser_.where();
    }
    return paths::contractUser(browser_.path());
}

std::vector<int> App::tabEdges() const {
    std::vector<int> edges{0};
    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        edges.push_back(edges.back() + displayWidth(tabLabel(*tabs_[i], i)));
    }
    return edges;
}

Element App::renderTabs() {
    Elements tabs;
    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        const auto& session = tabs_[i];
        const bool isActive = static_cast<int>(i) == tab_;
        const std::string label = tabLabel(*session, i);

        Element tab = text(label);
        if (isActive) {
            tab = std::move(tab) | bgcolor(toFtx(theme_->accent)) | color(toFtx(theme_->bg)) | bold;
        } else {
            tab = std::move(tab) | color(toFtx(theme_->muted));
        }
        tabs.push_back(std::move(tab));
    }
    tabs.push_back(filler());
    return hbox(std::move(tabs)) | bgcolor(toFtx(theme_->surface));
}

Element App::renderStatusBar(const Layout& layout) {
    const term::Session* session = active();
    const int width = layout.width;

    Elements left;
    if (session && !session->connection().empty()) {
        left.push_back(text(" " + session->connection() + " ") |
                       bgcolor(toFtx(theme_->accentAlt)) | color(toFtx(theme_->bg)) | bold);
    } else if (width >= 60) {
        left.push_back(text(" local ") | bgcolor(toFtx(theme_->surface)) |
                       color(toFtx(theme_->muted)));
    }

    const std::size_t problems = config_.issues().size();
    const std::string problemNote =
        problems == 0 ? ""
                      : std::to_string(problems) +
                            (problems == 1 ? " config problem" : " config problems");
    const bool showBrowserStats = width >= 96 && status_.empty() && config_.issues().empty();

    // One row to work with. The key hints give up whatever the message needs,
    // or a long one (an ssh failure, say) squeezes out all the spaces.
    const std::string note = !status_.empty()          ? status_
                             : !problemNote.empty()    ? problemNote
                             : showBrowserStats        ? browser_.statusLine()
                                                       : "";
    const int noteRoom = note.empty() ? 0 : std::min(displayWidth(note), width / 2) + 2;

    const int chipRoom = session && !session->connection().empty()
                             ? static_cast<int>(session->connection().size()) + 2
                         : width >= 60 ? 7
                                       : 0;

    const int hintRoom = std::max(0, std::min(width / 2, width - 28 - noteRoom));
    Element hints = renderHints(hintRoom);
    const int reserved = 2 + hintRoom + noteRoom + chipRoom;

    left.push_back(text(" " + elidePath(whereLabel(), std::max(8, width - reserved)) + " ") |
                   color(toFtx(theme_->fg)));

    if (leaderArmed_) {
        left.push_back(text(" " + leaderChord_.describe() + "… ") |
                       bgcolor(toFtx(theme_->warning)) | color(toFtx(theme_->bg)) | bold);
    }

    if (session && session->commandRunning()) {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(session->commandElapsed()).count();
        static const char* frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
        const std::string spinner =
            config_.decoration().animate
                ? std::string(frames[(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count() /
                                      100) %
                                     10])
                : "•";
        left.push_back(text(spinner + " running") | color(toFtx(theme_->accent)));
        if (elapsed > 1) {
            left.push_back(text(" " + std::to_string(elapsed) + "s") | color(toFtx(theme_->muted)));
        }
        left.push_back(text(" "));
    }

    if (transfers_.running() > 0) {
        if (const auto job = transfers_.current()) {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(transfers_.currentElapsed())
                    .count();
            std::string label = "⇅ " + elide(transfer::describe(*job), 24) +
                                (job->direction == transfer::Direction::Upload
                                     ? " → " + job->conn.name
                                     : " ← " + job->conn.name);
            if (transfers_.running() > 1) label += " +" + std::to_string(transfers_.running() - 1);
            if (elapsed > 0) label += " " + std::to_string(elapsed) + "s";
            left.push_back(text(" " + label + " ") | color(toFtx(theme_->accentAlt)));
        }
    }

    if (session && session->scrolled()) {
        const int history = session->screen().historyLines();
        const int percent = history > 0 ? (history - session->scrollOffset()) * 100 / history : 100;
        left.push_back(text("scrollback " + std::to_string(percent) + "% ") |
                       color(toFtx(theme_->warning)));
    }

    left.push_back(filler());

    if (!status_.empty()) {
        left.push_back(text(elide(note, noteRoom - 2) + "  ") |
                       color(toFtx(statusIsError_ ? theme_->error : theme_->success)));
    } else if (!problemNote.empty()) {
        left.push_back(text(elide(note, noteRoom - 2) + "  ") | color(toFtx(theme_->warning)));
    } else if (showBrowserStats) {
        left.push_back(text(elide(note, noteRoom - 2) + "  ") | color(toFtx(theme_->muted)));
    }

    left.push_back(std::move(hints));

    return hbox(std::move(left)) | bgcolor(toFtx(theme_->bg)) | size(WIDTH, EQUAL, width);
}

Element App::renderHints(int room) {
    struct Hint { std::string keys; std::string what; };
    std::vector<Hint> all = {
        {keyHintFor("command_palette"), "commands"},
        {keyHintFor("help"), "keys"},
        {keyHintFor("open_config"), "config"},
        {keyHintFor("browser_filter"), "find"},
        {keyHintFor("quit"), "quit"},
    };

    // Worth showing while you're connected. Just clutter the rest of the time.
    if (const term::Session* session = active(); session && !session->connection().empty()) {
        all.insert(all.begin() + 1, {keyHintFor("disconnect"), "disconnect"});
        all.insert(all.begin() + 1, {keyHintFor("transfer"), "send ⇅"});
    }

    Elements parts;
    int used = 0;
    for (const auto& hint : all) {
        if (hint.keys.empty()) continue;
        const int cost = static_cast<int>(hint.keys.size() + hint.what.size()) + 4;
        if (used + cost > room) break;
        if (!parts.empty()) parts.push_back(text(" · ") | color(toFtx(theme_->border)));
        parts.push_back(text(hint.keys + " ") | color(toFtx(theme_->accent)));
        parts.push_back(text(hint.what) | color(toFtx(theme_->muted)));
        used += cost;
    }
    if (parts.empty()) return text("");
    parts.push_back(text(" "));
    return hbox(std::move(parts));
}

Element App::renderSearch() {
    Elements parts{
        text(" search ") | bgcolor(toFtx(theme_->warning)) | color(toFtx(theme_->bg)) | bold,
        text(" "),
        searchQuery_.render(*theme_, "text to find", true),
        filler(),
    };
    if (!searchQuery_.text.empty()) {
        parts.push_back(text(searchHits_.empty()
                                 ? "no matches "
                                 : std::to_string(searchAt_ + 1) + "/" +
                                       std::to_string(searchHits_.size()) + "  Enter for next ") |
                        color(toFtx(searchHits_.empty() ? theme_->error : theme_->muted)));
    }
    return hbox(std::move(parts)) | bgcolor(toFtx(theme_->surface));
}

Element App::renderConfirm(int width, int height) {
    confirmSpots_.clear();
    const int panelWidth = std::clamp(width - 8, 36, 64);

    Elements details;
    std::istringstream lines(confirm_->detail);
    for (std::string line; std::getline(lines, line);) {
        details.push_back(text(" " + (line.rfind("into  ", 0) == 0
                                          ? "into  " + elidePath(line.substr(6), panelWidth - 9)
                                          : elide(line, panelWidth - 3))) |
                          color(toFtx(theme_->muted)));
    }

    Element body = vbox({
        text(""),
        text(" " + elide(confirm_->question, panelWidth - 3)) | color(toFtx(theme_->fg)) | bold,
        vbox(std::move(details)),
        text(""),
        hbox({
            text(" "),
            confirmSpots_.track(kConfirmYes, hint("Enter", confirm_->yes, *theme_)),
            text("   "),
            confirmSpots_.track(kConfirmNo, hint("Esc", confirm_->no, *theme_)),
            filler(),
        }),
    });

    return modal(std::move(body), *theme_, config_.decoration(), panelWidth, height - 4);
}

Element App::renderHelp(int width, int height) {
    const int panelWidth = std::clamp(width - 6, 56, 100);
    const int inner = panelWidth - 2;
    // Two columns if there's room, one if there isn't.
    const int columns = inner >= 84 ? 2 : 1;
    const int columnWidth = inner / columns;
    const int chordWidth = 15;

    Elements rows;
    rows.push_back(text(" Keys") | bold | color(toFtx(theme_->accent)));
    rows.push_back(text(""));
    const KeyChord& leader = config_.general().leader;
    rows.push_back(text(elide("  Apollo's keys hide behind the leader, currently " +
                                  leader.describe() + ". Press it, let go, then the key.",
                              inner)) |
                   color(toFtx(theme_->muted)));
    const auto& anywhere = config_.general().leaderAnywhere;
    if (leader.typesText() || !anywhere.empty()) {
        const std::string where = leader.typesText()
                                      ? "  " + leader.describe() +
                                            " is the leader at an empty prompt and in the browser"
                                      : "  " + leader.describe() + " is the leader";
        rows.push_back(text(elide(where + (anywhere.empty() ? "." : "; " +
                                               KeyChord::describeList(anywhere) +
                                               " everywhere."),
                                  inner)) |
                       color(toFtx(theme_->muted)));
    }
    rows.push_back(text(elide("  Everything else belongs to whatever is running in the terminal.",
                              inner)) |
                   color(toFtx(theme_->muted)));
    rows.push_back(text(""));

    std::vector<Bind> binds = config_.binds();
    std::stable_sort(binds.begin(), binds.end(),
                     [](const Bind& a, const Bind& b) { return a.chord < b.chord; });

    const int perColumn =
        (static_cast<int>(binds.size()) + columns - 1) / std::max(1, columns);
    for (int i = 0; i < perColumn; ++i) {
        Elements cells;
        for (int c = 0; c < columns; ++c) {
            const int index = i + c * perColumn;
            if (index >= static_cast<int>(binds.size())) {
                cells.push_back(text(std::string(static_cast<std::size_t>(columnWidth), ' ')));
                continue;
            }
            const Bind& bind = binds[static_cast<std::size_t>(index)];
            const ActionInfo* action = findAction(bind.action);

            std::string chord = elide(bind.chord.describe(), chordWidth);
            chord.resize(static_cast<std::size_t>(chordWidth) +
                             (chord.size() - displayWidth(chord)),
                         ' ');
            std::string what = action ? action->summary : bind.describeAction();
            what = elide(what, columnWidth - chordWidth - 3);

            cells.push_back(hbox({
                text("  " + chord) | color(toFtx(theme_->accent)),
                text(what) | color(toFtx(theme_->fg)),
                filler(),
            }) | size(WIDTH, EQUAL, columnWidth));
        }
        rows.push_back(hbox(std::move(cells)));
    }

    rows.push_back(text(""));
    rows.push_back(hbox({text("  Mouse ") | color(toFtx(theme_->muted)),
                         text("drag selects, the wheel scrolls, double click opens")}));
    if (!registry_.all().empty()) {
        rows.push_back(text(""));
        rows.push_back(text("  Your commands") | bold | color(toFtx(theme_->accent)));
        for (const auto& command : registry_.all()) {
            std::string name = command.name;
            name.resize(std::max<std::size_t>(name.size(), 14), ' ');
            rows.push_back(hbox({text("  " + name) | color(toFtx(theme_->accent)),
                                 text(elide(command.summary, inner - 18))}));
        }
    }
    rows.push_back(text(""));
    rows.push_back(text("  Any key closes this.") | color(toFtx(theme_->muted)));

    return modal(vbox(std::move(rows)), *theme_, config_.decoration(), panelWidth, height - 2);
}

Element App::render() {
    if (onboard_.isOpen() && !onboard_.previewTheme().empty()) {
        if (auto preview = Theme::builtin(onboard_.previewTheme())) {
            previewTheme_ = *preview;
            theme_ = &previewTheme_;
        }
    } else {
        theme_ = &config_.theme();
    }

    const Layout layout = measure();
    const DecorationSettings& decoration = layout.decoration;

    term::Session* session = active();
    if (session) session->resize(layout.terminalRows, layout.terminalCols);

    // --- terminal pane ---
    TerminalViewOptions viewOptions;
    viewOptions.focused = focus_ == Focus::Terminal && !palette_.isOpen() &&
                          !configView_.isOpen() && !onboard_.isOpen();
    viewOptions.height = layout.terminalRows;
    if (searching_) viewOptions.searchTerm = searchQuery_.text;

    Element terminalBody =
        session ? renderTerminal(*session, *theme_, config_.terminal(), viewOptions)
                : vbox({filler(), text("  no session") | color(toFtx(theme_->muted)), filler()});

    std::string terminalTitle = session ? session->title() : "terminal";
    std::string terminalRight;
    if (session) {
        if (!session->connection().empty()) terminalRight = "ssh " + session->connection();
        else if (!session->windowTitle().empty()) terminalRight = elide(session->windowTitle(), 30);
    }

    Element terminalPane = panel(terminalTitle, std::move(terminalBody),
                                 focus_ == Focus::Terminal, *theme_, decoration, terminalRight);

    // --- browser pane ---
    browser_.setCanSend(browser_.remote() || !config_.connections().empty());
    Element body;
    if (layout.browserWidth == 0) {
        body = std::move(terminalPane);
    } else {
        const bool wide = BrowserSettings::densityFor(layout.browserWidth) ==
                          BrowserSettings::Density::Wide;
        const std::string browserTitle =
            (wide ? (browser_.remote() ? "⇅ " : "◉ ") : "") +
            elidePath(whereLabel(), layout.browserWidth - (wide ? 8 : 6));

        Element browserPane =
            panel(browserTitle,
                  browser_.render(*theme_, config_.browser(), focus_ == Focus::Browser,
                                  layout.browserRows, layout.browserWidth - 2),
                  focus_ == Focus::Browser, *theme_, decoration);

        if (layout.stacked) {
            Elements rows{std::move(browserPane)};
            if (decoration.gaps > 0) rows.push_back(text(""));
            rows.push_back(std::move(terminalPane) | flex);
            body = vbox(std::move(rows));
        } else {
            Element sized = std::move(browserPane) | size(WIDTH, EQUAL, layout.browserWidth);
            Elements columns;
            if (config_.browser().position == "right") {
                columns.push_back(std::move(terminalPane) | flex);
                if (decoration.gaps > 0) {
                    columns.push_back(text(std::string(static_cast<std::size_t>(decoration.gaps), ' ')));
                }
                columns.push_back(std::move(sized));
            } else {
                columns.push_back(std::move(sized));
                if (decoration.gaps > 0) {
                    columns.push_back(text(std::string(static_cast<std::size_t>(decoration.gaps), ' ')));
                }
                columns.push_back(std::move(terminalPane) | flex);
            }
            body = hbox(std::move(columns));
        }
    }

    Elements screen;
    if (tabs_.size() > 1 && decoration.statusBar) screen.push_back(renderTabs());
    screen.push_back(std::move(body) | flex);
    if (searching_) screen.push_back(renderSearch());
    else if (decoration.statusBar) screen.push_back(renderStatusBar(layout));

    Element root = vbox(std::move(screen)) | bgcolor(toFtx(theme_->bg));

    if (boot_.running()) {
        return boot_.render(*theme_, decoration, layout.width, layout.height);
    }

    const bool hasOverlay = onboard_.isOpen() || configView_.isOpen() || palette_.isOpen() ||
                            helpOpen_ || confirm_.has_value() || transferForm_.isOpen();
    if (hasOverlay && decoration.dimInactive) root = dim(std::move(root));

    if (confirm_) {
        root = dbox({std::move(root), renderConfirm(layout.width, layout.height)});
    } else if (onboard_.isOpen()) {
        root = dbox({std::move(root),
                     onboard_.render(*theme_, decoration, layout.width, layout.height)});
    } else if (transferForm_.isOpen()) {
        root = dbox({std::move(root),
                     transferForm_.render(*theme_, decoration, layout.width, layout.height)});
    } else if (configView_.isOpen()) {
        root = dbox({std::move(root),
                     configView_.render(*theme_, decoration, layout.width, layout.height)});
    } else if (palette_.isOpen()) {
        root = dbox({std::move(root),
                     palette_.render(*theme_, decoration, layout.width, layout.height)});
    } else if (helpOpen_) {
        root = dbox({std::move(root), renderHelp(layout.width, layout.height)});
    } else if (leaderArmed_ &&
               std::chrono::steady_clock::now() - leaderArmedAt_ >= kLeaderKeysDelay) {
        root = dbox({std::move(root), renderLeaderKeys(layout.width)});
    }
    return root;
}

// --- running ---------------------------------------------------------------

int App::run() {
    const std::string workspace = options_.workspace.empty()
                                      ? paths::expandUser(config_.general().workspace)
                                      : paths::expandUser(options_.workspace);
    std::error_code ec;
    browser_.setPath(fs::is_directory(workspace, ec) ? fs::path(workspace) : paths::home());
    browser_.refresh(config_.browser());

    const Connection* connection = nullptr;
    Connection resolved;
    if (!options_.connect.empty()) {
        std::string error;
        if (auto found = config_.resolveConnection(options_.connect, error)) {
            resolved = *found;
            connection = &resolved;
        } else {
            say(error.substr(0, error.find('\n')), true);
        }
    }
    std::string controlError;
    if (!control_.start([this] { screen_.PostEvent(kControl); }, &controlError)) {
        // Not fatal. Without it `apollo` inside Apollo just says so.
        controlError = "control socket: " + controlError;
    }

    // Shells set up by an older Apollo get the newer prompt marks.
    Onboard::refreshShellIntegration();

    if (!newTab(connection)) return 1;
    if (connection) say("connecting to " + connection->describe() + "…");
    else if (!controlError.empty()) say(controlError, true);

    if (options_.runSetup) onboard_.start();
    if (options_.openConfig) configView_.open();
    if (!options_.command.empty()) tabs_.front()->sendText(options_.command + "\r");

    if (config_.decoration().boot) {
        std::vector<Boot::Line> lines;
        lines.push_back({"config", paths::contractUser(config_.path())});
        lines.push_back({"opens", whereLabel()});
        if (connection) lines.push_back({"ssh", connection->describe()});
        else lines.push_back({"theme", config_.decoration().theme});
        boot_.start(std::move(lines), config_.decoration().animate);
    }

    // Ctrl-C and Ctrl-Z belong to the terminal. FTXUI would grab them otherwise.
    screen_.ForceHandleCtrlC(false);
    screen_.ForceHandleCtrlZ(false);
    screen_.TrackMouse(true);

    ticking_ = true;
    ticker_ = std::thread([this] {
        while (ticking_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(tickMs_.load()));
            if (!ticking_) break;
            // A closure, not an event. FTXUI redraws the whole screen for every
            // event it handles, so beating with one would repaint several times
            // a second forever, whether or not anything had changed. This runs
            // the housekeeping on the UI thread and asks for a frame only when
            // there is something new to show.
            screen_.Post([this] {
                if (tick()) screen_.PostEvent(kRepaint);
            });
        }
    });

    Component root = Renderer([this] { return render(); }) |
                     CatchEvent([this](const Event& event) { return onEvent(event); });
    screen_.Loop(root);

    ticking_ = false;
    if (ticker_.joinable()) ticker_.join();
    control_.stop();

    for (auto& session : tabs_) {
        if (!session->connection().empty()) {
            if (const Connection* conn = config_.connection(session->connection())) {
                ssh::closeMaster(*conn);
            }
        }
        session->close();
    }
    return exitCode_;
}

} // namespace apollo::ui
