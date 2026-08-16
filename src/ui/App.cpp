#include "ui/App.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

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

// Two events Apollo posts to itself. Their payloads cannot collide with
// anything a terminal sends, which is why they are spelled out in full.
const Event kTick = Event::Special("apollo:tick");
const Event kOutput = Event::Special("apollo:output");

// Just enough base64 to receive an OSC 52 clipboard payload.
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
      configView_(config),
      onboard_(config) {
    applyConfig();

    browser_.onEnterDirectory = [this](const fs::path& path) {
        browser_.setPath(path);
        browser_.refresh(config_.browser());
        // Keep the shell in step, so the two panes never disagree about where
        // "here" is.
        if (term::Session* session = active(); session && session->connection().empty()) {
            session->sendText("cd " + ssh::quoteRemote(path.string()) + "\r");
        }
    };
    browser_.onOpenFile = [this](const fs::path& path) {
        const std::string editor = config_.general().editor.empty()
                                       ? (std::getenv("EDITOR") ? std::getenv("EDITOR") : "")
                                       : config_.general().editor;
        term::Session* session = active();
        if (!session) return;

        if (!editor.empty()) {
            session->sendText(editor + " " + ssh::quoteRemote(path.string()) + "\r");
            focus_ = Focus::Terminal;
            return;
        }
        // No editor configured: hand it to the desktop rather than guessing.
        if (!process::openWithDesktop(path.string())) {
            say("could not open " + path.filename().string(), true);
        } else {
            say("opened " + path.filename().string());
        }
    };

    configView_.onChanged = [this] { applyConfig(); };
    configView_.onEditExternally = [this] { act("edit_config", {}); };
    onboard_.onChanged = [this] { applyConfig(); };
    onboard_.onFinished = [this] {
        applyConfig();
        say("Leader is " + config_.general().leader.describe() + " — press it, then Space");
    };
}

App::~App() {
    ticking_ = false;
    if (ticker_.joinable()) ticker_.join();
}

// --- configuration ---------------------------------------------------------

bool App::browserVisible() const { return config_.browser().show; }
bool App::stacked() const { return config_.browser().layout == "stacked"; }
int App::browserWidth() const { return config_.browser().width; }

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

    // Only the user's own commands live here. Everything Apollo does itself is
    // an action, so the palette and the bind table cannot drift apart or list
    // the same thing twice.
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

bool App::newTab(const Connection* connection, const std::string& initialCommand) {
    const Layout layout = measure();
    auto session = std::make_unique<term::Session>(layout.terminalRows, layout.terminalCols,
                                                   config_.terminal().scrollback);

    term::Session::Options options;
    options.scrollback = config_.terminal().scrollback;
    options.cwd = browser_.path().string();

    if (connection) {
        // A remote tab is a real ssh session in a pty. Nothing about it is
        // simulated, which is why everything works over it.
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

    std::string error;
    if (!session->start(options, &error)) {
        say(error, true);
        return false;
    }

    session->setWakeup([this] { screen_.PostEvent(kOutput); });
    session->onClipboard = [this](const std::string& base64) {
        // OSC 52: the program running in this terminal asking to put something
        // on the clipboard. This is how `vim` yanks to the system clipboard
        // over ssh. It is off unless the user turns it on, because the request
        // can come from anything the shell runs.
        if (!config_.terminal().osc52Clipboard) return;
        const std::string payload = decodeBase64(base64);
        if (payload.empty()) return;
        if (process::clipboardWrite(payload)) say("clipboard set by the terminal");
    };

    tabs_.push_back(std::move(session));
    tab_ = static_cast<int>(tabs_.size()) - 1;
    focus_ = Focus::Terminal;

    if (!initialCommand.empty()) tabs_.back()->sendText(initialCommand + "\r");
    return true;
}

void App::closeTab(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) return;

    const std::string connection = tabs_[static_cast<std::size_t>(index)]->connection();
    tabs_[static_cast<std::size_t>(index)]->close();
    tabs_.erase(tabs_.begin() + index);

    if (!connection.empty()) {
        if (const Connection* conn = config_.connection(connection)) ssh::closeMaster(*conn);
    }
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

void App::say(const std::string& message, bool isError) {
    status_ = message;
    statusIsError_ = isError;
    statusUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(isError ? 8 : 4);
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
        if (bind.action == action) return bind.chord.describe();
    }
    return "";
}

void App::runCommand(const Command& command) {
    // Commands run in the terminal, where their output belongs and where they
    // can ask questions.
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
    if (action == "grow_pane") {
        put("browser.width", std::to_string(std::min(browserWidth() + 4, measure().width / 2)));
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
    if (action == "quit") { quitting_ = true; screen_.Exit(); return; }
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
        session->sendText(editor + " " + config_.path().string() + "\r");
        focus_ = Focus::Terminal;
        return;
    }
    if (action == "connect") {
        std::string error;
        const auto conn = config_.resolveConnection(argument, error);
        if (!conn) {
            // The error carries the full explanation; the status bar gets its
            // first line and the palette can show the rest.
            say(error.substr(0, error.find('\n')), true);
            return;
        }
        if (newTab(&*conn)) say("connecting to " + conn->describe());
        return;
    }
    if (action == "disconnect") {
        if (session->connection().empty()) { say("this tab is already local"); return; }
        closeTab(tab_);
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
        const fs::path target = paths::expandUser(argument);
        browser_.setPath(target);
        browser_.refresh(config_.browser());
        session->sendText("cd " + ssh::quoteRemote(target.string()) + "\r");
        return;
    }
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
        // Title first, identifier second: a palette should read as a list of
        // things to do, but still be searchable by the name in the config.
        items.push_back({action.summary, action.name, "", keyHintFor(action.name),
                         [this, name = action.name] { act(name, {}); }});
    }

    for (const auto& conn : config_.connections()) {
        items.push_back({"Connect to " + conn.name, conn.describe(), "ssh", "",
                         [this, name = conn.name] { act("connect", {name}); }});
    }

    for (const auto& name : Config::availableThemes()) {
        items.push_back({"Theme: " + name, "Switch the colour scheme", "theme", "",
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

void App::tick() {
    // Blink, notice a config the user saved in their editor, and re-read the
    // directory if something outside Apollo changed it.
    const auto now = std::chrono::steady_clock::now();
    if (config_.reloadIfChanged()) {
        applyConfig();
        say("reloaded " + paths::contractUser(config_.path()));
    }
    if (browserVisible()) browser_.refreshIfStale(config_.browser());

    for (auto& session : tabs_) {
        if (!session->screen().bellPending) continue;
        session->screen().bellPending = false;
        if (config_.terminal().bell) say("bell — " + session->title());
    }

    for (auto& session : tabs_) session->pump();

    // The shell told us where it is; follow it.
    if (config_.general().followCwd) {
        if (term::Session* session = active(); session && !session->cwd().empty()) {
            const fs::path reported = session->cwd();
            if (reported != browser_.path() && fs::is_directory(reported)) {
                browser_.setPath(reported);
                browser_.refresh(config_.browser());
            }
        }
    }

    if (!status_.empty() && now > statusUntil_) status_.clear();

    // A session that exited cleanly closes its tab, the way a terminal window
    // closes when you type `exit`. One that failed stays put with its output
    // on screen — an ssh that could not connect has something to say, and
    // vanishing would take the message with it.
    for (int i = static_cast<int>(tabs_.size()) - 1; i >= 0; --i) {
        auto& session = tabs_[static_cast<std::size_t>(i)];
        if (session->running()) continue;

        if (session->exitCode() == 0 || tabs_.size() > 1) {
            closeTab(i);
        } else if (status_.empty()) {
            say(session->title() + " exited " + std::to_string(session->exitCode()) +
                    " — Leader C for a shell, Leader Q to leave",
                true);
        }
    }
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

    // Scroll wheel: the browser moves its selection, the terminal its history.
    if (mouse.button == Mouse::WheelUp || mouse.button == Mouse::WheelDown) {
        const int direction = mouse.button == Mouse::WheelUp ? 1 : -1;
        if (layout.browserWidth > 0 && !layout.stacked && mouse.x >= browserLeft &&
            mouse.x < browserRight) {
            browser_.moveSelection(-direction);
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

    if (mouse.button != Mouse::Left) return true;

    // The tab strip, when there is one.
    if (tabs_.size() > 1 && mouse.y == 0) {
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

    const bool inBrowser = layout.browserWidth > 0 && !layout.stacked && mouse.x >= browserLeft &&
                           mouse.x < browserRight;
    if (inBrowser) {
        focus_ = Focus::Browser;
        if (mouse.motion == Mouse::Pressed) {
            // Terminals do not report double clicks, so time them here: a
            // second press on the same row inside 400ms opens the entry.
            const int row = mouse.y - topOffset;
            const auto now = std::chrono::steady_clock::now();
            const bool doubleClick = row == lastClickRow_ &&
                                     now - lastClick_ < std::chrono::milliseconds(400);
            browser_.onClick(row, doubleClick);
            // Reset after acting, or a third click would count as another pair.
            lastClick_ = doubleClick ? std::chrono::steady_clock::time_point{} : now;
            lastClickRow_ = doubleClick ? -1 : row;
        }
        return true;
    }

    term::Session* session = active();
    if (!session) return true;
    focus_ = Focus::Terminal;

    const int terminalLeft = browserOnLeft ? browserRight + decoration.gaps + frame : frame;
    const int column = mouse.x - terminalLeft;
    const int row = mouse.y - topOffset;
    if (column < 0 || row < 0 || row >= layout.terminalRows) return true;

    // A program that asked for mouse reporting gets the event; otherwise the
    // drag is a text selection.
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
    if (event == kTick) { tick(); return true; }
    if (event == kOutput) {
        for (auto& session : tabs_) session->pump();
        return true;
    }
    if (event.is_mouse()) return onMouse(event);

    const std::string raw = event.input();
    const KeyChord chord = decodeKey(raw);

    // Overlays, in the order they sit on top of each other.
    if (onboard_.isOpen()) {
        onboard_.onKey(chord, raw);
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

    // The leader. Pressing it arms the next key; pressing it twice sends it on
    // to the terminal, which is the escape hatch if you actually wanted it.
    if (leaderArmed_) {
        leaderArmed_ = false;
        if (chord == config_.general().leader) {
            if (term::Session* session = active()) session->sendKey(chord, raw);
            return true;
        }
        KeyChord withLeader = chord;
        withLeader.mods |= ModLeader;
        if (!runBind(withLeader)) {
            say("nothing bound to " + withLeader.describe());
        }
        return true;
    }
    if (!chord.empty() && chord == config_.general().leader) {
        leaderArmed_ = true;
        return true;
    }

    if (!chord.empty() && runBind(chord)) return true;

    if (focus_ == Focus::Browser) {
        if (chord.key == "tab") { focus_ = Focus::Terminal; return true; }
        if (browser_.onKey(chord, config_.browser())) return true;
        // Anything the browser does not want goes to the terminal, so typing
        // never disappears.
        focus_ = Focus::Terminal;
    }

    if (term::Session* session = active()) {
        if (!session->selection.empty()) session->selection = term::Selection{};
        session->sendKey(chord, raw);
        return true;
    }
    return true;
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

    // The path gets whatever the fixed pieces leave. Everything optional is
    // dropped as the window narrows, rather than every piece being squeezed
    // until none of them is readable.
    const bool showLeaderHint = width >= 76;
    const bool showBrowserStats = width >= 92 && status_.empty();
    int reserved = 2;
    // A message on the right takes room from the path, not from the gap
    // between them.
    if (!status_.empty()) {
        reserved += std::min(static_cast<int>(status_.size()), width / 2) + 3;
    }
    if (showLeaderHint) reserved += static_cast<int>(
        config_.general().leader.describe().size()) + 8;
    if (showBrowserStats) reserved += static_cast<int>(browser_.statusLine().size()) + 2;
    if (session && !session->connection().empty()) {
        reserved += static_cast<int>(session->connection().size()) + 2;
    } else if (width >= 60) {
        reserved += 7;
    }

    left.push_back(text(" " + elidePath(paths::contractUser(browser_.path()),
                                        std::max(8, width - reserved)) +
                        " ") |
                   color(toFtx(theme_->fg)));

    if (leaderArmed_) {
        left.push_back(text(" " + config_.general().leader.describe() + "… ") |
                       bgcolor(toFtx(theme_->warning)) | color(toFtx(theme_->bg)) | bold);
    }

    if (session && session->commandRunning()) {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(session->commandElapsed()).count();
        // The shell only reports this when shell integration is installed, so
        // it is quietly absent rather than wrong when it is not.
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

    if (session && session->scrolled()) {
        const int history = session->screen().historyLines();
        const int percent = history > 0 ? (history - session->scrollOffset()) * 100 / history : 100;
        left.push_back(text("scrollback " + std::to_string(percent) + "% ") |
                       color(toFtx(theme_->warning)));
    }

    left.push_back(filler());

    if (!status_.empty()) {
        left.push_back(text(elide(status_, std::max(10, width / 2)) + "  ") |
                       color(toFtx(statusIsError_ ? theme_->error : theme_->success)));
    } else if (!config_.issues().empty()) {
        left.push_back(text(std::to_string(config_.issues().size()) + " config problems  ") |
                       color(toFtx(theme_->warning)));
    } else if (showBrowserStats) {
        left.push_back(text(browser_.statusLine() + "  ") | color(toFtx(theme_->muted)));
    }

    if (showLeaderHint) {
        left.push_back(text(config_.general().leader.describe() + " Space ") |
                       color(toFtx(theme_->muted)));
    }

    return hbox(std::move(left)) | bgcolor(toFtx(theme_->bg)) | size(WIDTH, EQUAL, width);
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

Element App::renderHelp(int width, int height) {
    const int panelWidth = std::clamp(width - 6, 56, 100);
    const int inner = panelWidth - 2;
    // Two columns when there is room for both, one when there is not.
    const int columns = inner >= 84 ? 2 : 1;
    const int columnWidth = inner / columns;
    const int chordWidth = 15;

    Elements rows;
    rows.push_back(text(" Keys") | bold | color(toFtx(theme_->accent)));
    rows.push_back(text(""));
    rows.push_back(text(elide("  Apollo's keys hide behind the leader, currently " +
                                  config_.general().leader.describe() +
                                  ". Press it, let go, then the key.",
                              inner)) |
                   color(toFtx(theme_->muted)));
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

            // Fixed widths rather than flex: letting FTXUI shrink these cuts
            // words in half and the columns stop lining up.
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
    // While the wizard is previewing a theme, the whole interface shows it.
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
    Element body;
    if (layout.browserWidth == 0) {
        body = std::move(terminalPane);
    } else {
        Element browserPane =
            panel(elidePath(paths::contractUser(browser_.path()), layout.browserWidth - 6),
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

    // Overlays. The interface behind them dims, so the eye lands on the thing
    // asking for attention.
    const bool hasOverlay = onboard_.isOpen() || configView_.isOpen() || palette_.isOpen() ||
                            helpOpen_;
    if (hasOverlay && decoration.dimInactive) root = dim(std::move(root));

    if (onboard_.isOpen()) {
        root = dbox({std::move(root),
                     onboard_.render(*theme_, decoration, layout.width, layout.height)});
    } else if (configView_.isOpen()) {
        root = dbox({std::move(root),
                     configView_.render(*theme_, decoration, layout.width, layout.height)});
    } else if (palette_.isOpen()) {
        root = dbox({std::move(root),
                     palette_.render(*theme_, decoration, layout.width, layout.height)});
    } else if (helpOpen_) {
        root = dbox({std::move(root), renderHelp(layout.width, layout.height)});
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
    if (!newTab(connection)) return 1;
    if (connection) say("connecting to " + connection->describe() + "…");

    if (options_.runSetup) onboard_.start();
    if (options_.openConfig) configView_.open();
    if (!options_.command.empty()) tabs_.front()->sendText(options_.command + "\r");

    // Ctrl-C and Ctrl-Z belong to whatever is running in the terminal, not to
    // Apollo; without this FTXUI would raise the signals itself.
    screen_.ForceHandleCtrlC(false);
    screen_.ForceHandleCtrlZ(false);
    screen_.TrackMouse(true);

    ticking_ = true;
    ticker_ = std::thread([this] {
        while (ticking_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (ticking_) screen_.PostEvent(kTick);
        }
    });

    Component root = Renderer([this] { return render(); }) |
                     CatchEvent([this](const Event& event) { return onEvent(event); });
    screen_.Loop(root);

    ticking_ = false;
    if (ticker_.joinable()) ticker_.join();

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
