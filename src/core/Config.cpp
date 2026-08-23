#include "core/Config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

#include "core/Paths.h"

namespace fs = std::filesystem;

namespace apollo {
namespace {

using Type = Config::Setting::Type;

Config::Setting str(std::string path, std::string def, std::string summary,
                    Type type = Type::String) {
    Config::Setting s;
    s.path = std::move(path);
    s.type = type;
    s.defaultValue = std::move(def);
    s.summary = std::move(summary);
    return s;
}

Config::Setting flag(std::string path, bool def, std::string summary) {
    Config::Setting s = str(std::move(path), def ? "true" : "false", std::move(summary),
                            Type::Bool);
    s.choices = {"true", "false"};
    return s;
}

Config::Setting number(std::string path, int def, int min, int max, std::string summary) {
    Config::Setting s = str(std::move(path), std::to_string(def), std::move(summary),
                            Type::Int);
    s.min = min;
    s.max = max;
    return s;
}

Config::Setting choice(std::string path, std::string def, std::vector<std::string> options,
                       std::string summary) {
    Config::Setting s = str(std::move(path), std::move(def), std::move(summary), Type::Enum);
    s.choices = std::move(options);
    return s;
}

} // namespace

std::string Config::Setting::typeName() const {
    switch (type) {
        case Type::String: return "text";
        case Type::Bool:   return "yes/no";
        case Type::Int:    return "number";
        case Type::Enum:   return "choice";
        case Type::Color:  return "colour";
        case Type::Path:   return "path";
    }
    return "";
}

const std::vector<Config::Setting>& Config::schema() {
    static const std::vector<Setting> settings = [] {
        std::vector<Setting> s;

        s.push_back(str("general.workspace", "~", "Directory Apollo opens in", Type::Path));
        s.push_back(str("general.shell", "", "Shell to run; empty means your login shell", Type::Path));
        s.push_back(str("general.editor", "", "Editor for `apollo config edit`; empty means $EDITOR", Type::Path));
        s.push_back(str("general.default_connection", "", "Used by a bare `apollo connect`"));
        s.push_back(flag("general.follow_cwd", true, "The browser follows the shell's directory"));
        s.push_back(flag("general.confirm_quit", false, "Ask before leaving with a command still running"));

        Setting theme = choice("decoration.theme", "apollo", Theme::builtinNames(),
                               "Colour scheme, built in or a file in ~/.apollo/themes");
        theme.openChoices = true;
        s.push_back(std::move(theme));
        s.push_back(choice("decoration.border", "rounded",
                           {"rounded", "light", "heavy", "double", "none"}, "Pane border style"));
        s.push_back(number("decoration.gaps", 1, 0, 4, "Blank columns between panes"));
        s.push_back(flag("decoration.animate", true, "Animate panel transitions and spinners"));
        s.push_back(flag("decoration.boot", true, "Show the splash on the way in"));
        s.push_back(flag("decoration.dim_inactive", true, "Dim the pane that does not have focus"));
        s.push_back(flag("decoration.status_bar", true, "Show the bar along the bottom"));
        s.push_back(flag("decoration.title_bar", true, "Show pane titles"));

        s.push_back(number("terminal.scrollback", 10000, 100, 500000, "Lines of history to keep"));
        s.push_back(number("terminal.scroll_lines", 3, 1, 20, "Lines moved per scroll step"));
        s.push_back(choice("terminal.cursor", "block", {"block", "bar", "underline"}, "Cursor shape"));
        s.push_back(flag("terminal.cursor_blink", true, "Blink the cursor"));
        s.push_back(flag("terminal.bell", false, "Ring the terminal bell"));
        s.push_back(flag("terminal.copy_on_select", true, "Copy as soon as text is selected"));
        s.push_back(flag("terminal.shell_integration", true,
                         "Track the shell's directory and mark prompts (OSC 7/133)"));
        s.push_back(flag("terminal.osc52_clipboard", false,
                         "Let programs set the system clipboard (OSC 52)"));
        s.push_back(str("terminal.word_chars", "_-./@~", "Extra characters counted as part of a word"));

        s.push_back(flag("browser.show", true, "Show the file browser"));
        s.push_back(number("browser.width", 42, 16, 160, "Browser width, in columns"));
        s.push_back(choice("browser.position", "left", {"left", "right"},
                           "Which side the browser sits on"));
        s.push_back(choice("browser.layout", "split", {"split", "stacked"},
                           "Side by side, or the browser above the terminal"));
        s.push_back(flag("browser.show_hidden", false, "List dotfiles"));
        s.push_back(choice("browser.sort", "name", {"name", "size", "modified", "type"},
                           "Sort order"));
        s.push_back(flag("browser.sort_reverse", false, "Sort the other way round"));
        s.push_back(flag("browser.dirs_first", true, "Group directories above files"));
        s.push_back(flag("browser.icons", true, "Show file type icons"));
        s.push_back(flag("browser.git_status", true, "Mark files changed since the last commit"));
        s.push_back(flag("browser.toolbar", true, "Show the browser's toolbar"));
        s.push_back(flag("browser.details", true, "Describe the selected entry underneath"));

        for (const auto& key : Theme::colorKeys()) {
            s.push_back(str("colors." + key, "", "Override the theme's " + key + " colour",
                            Type::Color));
        }
        return s;
    }();
    return settings;
}

const Config::Setting* Config::setting(const std::string& path) {
    for (const auto& s : schema()) {
        if (s.path == path) return &s;
    }
    return nullptr;
}

std::string Config::validate(const Setting& s, const std::string& value) {
    switch (s.type) {
        case Type::Bool: {
            const std::string v = ConfigFile::trim(value);
            static const std::vector<std::string> yes = {"true", "yes", "on", "1",
                                                         "false", "no", "off", "0"};
            std::string lowered = v;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (std::find(yes.begin(), yes.end(), lowered) == yes.end()) {
                return "expected true or false";
            }
            return "";
        }
        case Type::Int: {
            try {
                std::size_t used = 0;
                const int n = std::stoi(ConfigFile::trim(value), &used);
                if (used != ConfigFile::trim(value).size()) return "expected a whole number";
                if (n < s.min || n > s.max) {
                    return "expected " + std::to_string(s.min) + " to " + std::to_string(s.max);
                }
            } catch (...) {
                return "expected a whole number";
            }
            return "";
        }
        case Type::Enum: {
            if (s.openChoices) return "";
            if (std::find(s.choices.begin(), s.choices.end(), ConfigFile::trim(value)) ==
                s.choices.end()) {
                std::string options;
                for (const auto& c : s.choices) options += (options.empty() ? "" : ", ") + c;
                return "expected one of: " + options;
            }
            return "";
        }
        case Type::Color:
            if (value.empty()) return "";
            return Rgb::parse(value) ? "" : "expected #rrggbb, rgb(r,g,b) or a colour name";
        case Type::Path:
        case Type::String:
            return "";
    }
    return "";
}

std::string Config::valueOf(const Setting& s) const {
    return file_.get(s.path, s.defaultValue);
}

// --- defaults --------------------------------------------------------------

const std::vector<Bind>& Config::defaultBinds() {
    static const std::vector<Bind> binds = [] {
        const std::vector<std::pair<std::string, std::vector<std::string>>> table = {
            {", F1",              {"help"}},
            {", F10",             {"quit"}},
            {"SHIFT, PAGEUP",     {"scroll_up"}},
            {"SHIFT, PAGEDOWN",   {"scroll_down"}},

            {"LEADER, SPACE",     {"command_palette"}},
            {"LEADER, P",         {"command_palette"}},
            {"LEADER, B",         {"toggle_browser"}},
            {"LEADER, E",         {"focus_browser"}},
            {"LEADER, T",         {"focus_terminal"}},
            {"LEADER, TAB",       {"focus_next"}},
            {"LEADER, C",         {"new_tab"}},
            {"LEADER, X",         {"close_tab"}},
            {"LEADER, N",         {"next_tab"}},
            {"LEADER, O",         {"prev_tab"}},
            {"LEADER, S",         {"toggle_layout"}},
            {"LEADER, H",         {"toggle_hidden"}},
            {"LEADER, SLASH",     {"search"}},
            {"LEADER, COMMA",     {"open_config"}},
            {"LEADER, R",         {"reload_config"}},
            {"LEADER, L",         {"clear"}},
            {"LEADER, Q",         {"quit"}},
            {"LEADER, LEFT",      {"shrink_pane"}},
            {"LEADER, RIGHT",     {"grow_pane"}},
            {"LEADER, UP",        {"prev_prompt"}},
            {"LEADER, DOWN",      {"next_prompt"}},
            {"LEADER, HOME",      {"scroll_top"}},
            {"LEADER, END",       {"scroll_bottom"}},
            {"LEADER, D",         {"disconnect"}},
            {"LEADER, F",         {"browser_filter"}},
            {"LEADER, BRACKETLEFT",  {"browser_back"}},
            {"LEADER, BRACKETRIGHT", {"browser_forward"}},
            {"LEADER, U",         {"browser_up"}},
            {"LEADER, Y",         {"copy_path"}},
            {"LEADER, V",         {"paste_path"}},
        };

        std::vector<Bind> out;
        for (const auto& [spec, action] : table) {
            Bind bind;
            if (const auto chord = KeyChord::parse(spec)) bind.chord = *chord;
            bind.action = action[0];
            bind.args.assign(action.begin() + 1, action.end());
            out.push_back(std::move(bind));
        }
        return out;
    }();
    return binds;
}

std::string Config::defaultText() {
    return R"(# Apollo
#
# Read live: save and the running Apollo picks it up. `apollo config` edits
# this file in place and leaves the comments alone.
#
# Run `apollo config` with no arguments for an editor with every setting.

# Variables, defined once and used anywhere below.
$accent = #7aa2f7

general {
    workspace   = ~
    follow_cwd  = true          # the browser follows your shell's directory
    # shell     = /bin/zsh      # empty means your login shell
    # editor    = nvim
}

decoration {
    theme         = apollo      # apollo, midnight, nord, gruvbox, catppuccin, solarized, paper
    border        = rounded
    gaps          = 1
    animate       = true
    dim_inactive  = true
}

# Override individual colours from whichever theme is selected.
colors {
    accent = $accent
}

terminal {
    scrollback        = 10000
    cursor            = block
    cursor_blink      = true
    copy_on_select    = true
    shell_integration = true    # OSC 7/133: directory tracking and prompt jumps
}

browser {
    show        = true
    width       = 42
    position    = left          # or right
    layout      = split         # or stacked, with the browser on top
    show_hidden = false
    sort        = name
    icons       = true
    toolbar     = true          # back, forward, up, the path, sort, filter
    details     = true          # what the selected entry is, underneath
}

# ---------------------------------------------------------------------------
# Keys
#
#   bind = <modifiers>, <key>, <action>, <argument>
#
# LEADER means "after the leader key", Ctrl+Space unless you change it.
# Apollo's keys sit behind the leader so Ctrl-A, Ctrl-C, Ctrl-K and the rest
# stay with the program running in your terminal.
#
# `apollo config` lists every action, and `unbind` removes one of the defaults.
# ---------------------------------------------------------------------------

leader = CTRL, SPACE

# bind   = LEADER, G, exec, git status
# unbind = LEADER, Q

# ---------------------------------------------------------------------------
# Commands
#
#   command = <name>, <what to run>, "<description>"
#
# Anything here becomes `apollo <name>`, appears in the palette, and can be
# bound to a key. Executables in ~/.apollo/commands work the same way with no
# entry needed.
# ---------------------------------------------------------------------------

# command = deploy, ./scripts/deploy.sh, "Ship the current branch"

# ---------------------------------------------------------------------------
# SSH destinations
#
# `apollo connect` with one connection configured uses it. With several, name
# the one you want — `apollo connect lab` — or set general.default_connection.
# ---------------------------------------------------------------------------

# connection lab {
#     host       = 10.0.0.5
#     user       = alice
#     key        = ~/.ssh/id_ed25519
#     remote_dir = ~/work
# }
)";
}

bool Config::writeDefault(std::string* error) {
    if (!paths::ensureDir(paths::configDir(), error)) return false;

    std::error_code ec;
    if (fs::exists(paths::configFile(), ec)) {
        if (error) *error = paths::configFile().string() + " already exists";
        return false;
    }

    file_.parse(defaultText(), paths::configFile().string());
    path_ = paths::configFile();
    if (!file_.save(path_, error)) return false;

    stamp_ = fs::last_write_time(path_, ec);
    derive();
    return true;
}

// --- loading ---------------------------------------------------------------

bool Config::exists() const {
    std::error_code ec;
    return fs::exists(path_.empty() ? paths::configFile() : path_, ec);
}

bool Config::loadText(const std::string& text) {
    file_.parse(text, "<memory>");
    derive();
    return file_.ok();
}

bool Config::loadFrom(const fs::path& p) {
    path_ = p;
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        file_.parse("", p.string());
        derive();
        return false;
    }
    const bool parsed = file_.load(p);
    stamp_ = fs::last_write_time(p, ec);
    derive();
    return parsed;
}

bool Config::load() { return loadFrom(paths::configFile()); }

bool Config::reloadIfChanged() {
    std::error_code ec;
    if (path_.empty() || !fs::exists(path_, ec)) return false;

    const auto now = fs::last_write_time(path_, ec);
    if (ec || now == stamp_) return false;

    stamp_ = now;
    file_.load(path_);
    derive();
    return true;
}

bool Config::save(std::string* error) {
    if (path_.empty()) path_ = paths::configFile();
    if (!file_.save(path_, error)) return false;

    std::error_code ec;
    stamp_ = fs::last_write_time(path_, ec);
    return true;
}

// --- deriving --------------------------------------------------------------

void Config::note(const std::string& issue) { issues_.push_back(issue); }

void Config::derive() {
    issues_.clear();
    for (const auto& d : file_.diagnostics()) note(d.format());

    const auto text = [&](const char* path, const std::string& fallback) {
        return file_.get(path, fallback);
    };
    const auto yes = [&](const char* path, bool fallback) {
        const auto value = file_.get(path);
        return value ? ConfigFile::asBool(*value, fallback) : fallback;
    };
    const auto num = [&](const char* path, int fallback) {
        const auto value = file_.get(path);
        return value ? ConfigFile::asInt(*value, fallback) : fallback;
    };

    general_ = GeneralSettings{};
    general_.workspace = text("general.workspace", general_.workspace);
    general_.shell = text("general.shell", "");
    general_.editor = text("general.editor", "");
    general_.defaultConnection = text("general.default_connection", "");
    general_.followCwd = yes("general.follow_cwd", true);
    general_.confirmQuit = yes("general.confirm_quit", false);

    if (const auto spec = file_.get("leader")) {
        if (const auto chord = KeyChord::parse(*spec)) general_.leader = *chord;
        else note("leader: '" + *spec + "' is not a key Apollo knows");
    }

    decoration_ = DecorationSettings{};
    decoration_.theme = text("decoration.theme", "apollo");
    decoration_.border = text("decoration.border", "rounded");
    decoration_.gaps = std::clamp(num("decoration.gaps", 1), 0, 4);
    decoration_.animate = yes("decoration.animate", true);
    decoration_.boot = yes("decoration.boot", true);
    decoration_.dimInactive = yes("decoration.dim_inactive", true);
    decoration_.statusBar = yes("decoration.status_bar", true);
    decoration_.titleBar = yes("decoration.title_bar", true);

    terminal_ = TerminalSettings{};
    terminal_.scrollback = std::clamp(num("terminal.scrollback", 10000), 100, 500000);
    terminal_.scrollLines = std::clamp(num("terminal.scroll_lines", 3), 1, 20);
    terminal_.cursor = text("terminal.cursor", "block");
    terminal_.cursorBlink = yes("terminal.cursor_blink", true);
    terminal_.bell = yes("terminal.bell", false);
    terminal_.copyOnSelect = yes("terminal.copy_on_select", true);
    terminal_.shellIntegration = yes("terminal.shell_integration", true);
    terminal_.osc52Clipboard = yes("terminal.osc52_clipboard", false);
    terminal_.wordChars = text("terminal.word_chars", terminal_.wordChars);

    browser_ = BrowserSettings{};
    browser_.show = yes("browser.show", true);
    browser_.width = std::clamp(num("browser.width", 42), 16, 160);
    browser_.position = text("browser.position", "left");
    browser_.layout = text("browser.layout", "split");
    browser_.showHidden = yes("browser.show_hidden", false);
    browser_.sort = text("browser.sort", "name");
    browser_.sortReverse = yes("browser.sort_reverse", false);
    browser_.dirsFirst = yes("browser.dirs_first", true);
    browser_.icons = yes("browser.icons", true);
    browser_.gitStatus = yes("browser.git_status", true);
    browser_.toolbar = yes("browser.toolbar", true);
    browser_.details = yes("browser.details", true);

    for (const auto& section : file_.root().children) {
        static const std::vector<std::string> checked = {"general", "decoration", "terminal",
                                                         "browser", "colors"};
        if (std::find(checked.begin(), checked.end(), section.name) == checked.end()) continue;
        for (const auto& entry : section.entries) {
            if (!setting(section.name + "." + entry.key)) {
                note("unknown setting '" + section.name + "." + entry.key + "'" +
                     (entry.line >= 0 ? " (line " + std::to_string(entry.line + 1) + ")" : ""));
            }
        }
    }

    for (const auto& s : schema()) {
        const auto value = file_.get(s.path);
        if (!value || value->empty()) continue;
        if (const std::string problem = validate(s, *value); !problem.empty()) {
            note(s.path + ": " + problem);
        }
    }

    deriveTheme();
    deriveBinds();

    // --- commands ---
    commands_.clear();
    for (const auto* entry : file_.root().entriesNamed("command")) {
        const auto parts = ConfigFile::split(entry->value);
        if (parts.size() < 2 || parts[0].empty() || parts[1].empty()) {
            note("command needs at least a name and something to run (line " +
                 std::to_string(entry->line + 1) + ")");
            continue;
        }
        DeclaredCommand command;
        command.name = parts[0];
        command.exec = parts[1];
        command.summary = parts.size() > 2 ? parts[2] : "";
        command.interactive = parts.size() <= 3 || ConfigFile::asBool(parts[3], true);
        command.line = entry->line;
        commands_.push_back(std::move(command));
    }

    // --- connections ---
    connections_.clear();
    for (const auto* node : file_.labelled("connection")) {
        Connection conn;
        conn.name = node->label;
        if (const auto* e = node->entry("host")) conn.host = e->value;
        if (const auto* e = node->entry("user")) conn.user = e->value;
        if (const auto* e = node->entry("key")) conn.keyPath = e->value;
        if (const auto* e = node->entry("password")) conn.password = e->value;
        if (const auto* e = node->entry("remote_dir")) conn.remoteDir = e->value;
        if (const auto* e = node->entry("jump")) conn.jump = e->value;
        if (const auto* e = node->entry("port")) conn.port = ConfigFile::asInt(e->value, 22);
        if (const auto* e = node->entry("forward_agent")) {
            conn.forwardAgent = ConfigFile::asBool(e->value, false);
        }

        if (!conn.valid()) {
            note("connection '" + conn.name + "' needs both a host and a user");
        }
        connections_.push_back(std::move(conn));
    }

    if (!general_.defaultConnection.empty() && !connection(general_.defaultConnection)) {
        note("general.default_connection names '" + general_.defaultConnection +
             "', which is not configured");
    }
}

std::optional<Theme> Config::loadThemeFile(const std::string& name) {
    if (name.empty() || name.find('/') != std::string::npos) return std::nullopt;

    const fs::path file = paths::themesDir() / (name + ".conf");
    std::error_code ec;
    if (!fs::exists(file, ec)) return std::nullopt;

    ConfigFile source;
    if (!source.load(file)) {
        for (const auto& d : source.diagnostics()) note(d.format());
    }

    const std::string base = source.get("base", "apollo");
    Theme theme = Theme::builtin(base).value_or(*Theme::builtin("apollo"));
    if (!Theme::builtin(base)) {
        note(paths::contractUser(file) + ": unknown base theme '" + base + "'");
    }

    if (const ConfigNode* colors = source.section("colors")) {
        for (const auto& entry : colors->entries) {
            if (!theme.setColor(entry.key, entry.value)) {
                note(paths::contractUser(file) + ": '" + entry.key + "' is not a colour Apollo sets");
            }
        }
    }
    theme.name = name;
    theme.rebuildRamp();
    return theme;
}

std::vector<std::string> Config::availableThemes() {
    std::vector<std::string> names = Theme::builtinNames();

    std::error_code ec;
    if (fs::is_directory(paths::themesDir(), ec)) {
        std::vector<std::string> custom;
        for (const auto& entry : fs::directory_iterator(paths::themesDir(), ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec) || entry.path().extension() != ".conf") continue;
            const std::string name = entry.path().stem().string();
            if (std::find(names.begin(), names.end(), name) == names.end()) custom.push_back(name);
        }
        std::sort(custom.begin(), custom.end());
        names.insert(names.end(), custom.begin(), custom.end());
    }
    return names;
}

void Config::deriveTheme() {
    if (auto found = Theme::builtin(decoration_.theme)) {
        theme_ = *found;
    } else if (auto custom = loadThemeFile(decoration_.theme)) {
        theme_ = *custom;
    } else {
        theme_ = *Theme::builtin("apollo");
        note("unknown theme '" + decoration_.theme + "'; using apollo. Built-in themes: " +
             [] {
                 std::string list;
                 for (const auto& name : Theme::builtinNames()) {
                     list += (list.empty() ? "" : ", ") + name;
                 }
                 return list;
             }());
    }

    if (const ConfigNode* colors = file_.section("colors")) {
        for (const auto& entry : colors->entries) {
            if (!theme_.setColor(entry.key, entry.value)) {
                note("colors." + entry.key + ": not a colour Apollo knows (" + entry.value + ")");
            }
        }
        // Colours changed, so rebuild the ansi ramp derived from them.
        theme_.rebuildRamp();
    }
}

void Config::deriveBinds() {
    binds_ = defaultBinds();

    for (const auto* entry : file_.root().entriesNamed("bind")) {
        const auto parts = ConfigFile::split(entry->value);
        if (parts.size() < 3) {
            note("bind needs modifiers, a key and an action (line " +
                 std::to_string(entry->line + 1) + ")");
            continue;
        }
        const auto chord = KeyChord::parse(parts[0], parts[1]);
        if (!chord) {
            note("bind: '" + parts[0] + ", " + parts[1] + "' is not a key Apollo knows (line " +
                 std::to_string(entry->line + 1) + ")");
            continue;
        }
        if (!findAction(parts[2])) {
            note("bind: no action called '" + parts[2] + "' (line " +
                 std::to_string(entry->line + 1) + ")");
            continue;
        }

        Bind bind;
        bind.chord = *chord;
        bind.action = parts[2];
        bind.args.assign(parts.begin() + 3, parts.end());
        bind.line = entry->line;

        const auto same = std::find_if(binds_.begin(), binds_.end(), [&](const Bind& b) {
            return b.chord == bind.chord;
        });
        if (same != binds_.end()) *same = std::move(bind);
        else binds_.push_back(std::move(bind));
    }

    for (const auto* entry : file_.root().entriesNamed("unbind")) {
        const auto parts = ConfigFile::split(entry->value);
        if (parts.size() < 2) {
            note("unbind needs modifiers and a key (line " + std::to_string(entry->line + 1) + ")");
            continue;
        }
        const auto chord = KeyChord::parse(parts[0], parts[1]);
        if (!chord) {
            note("unbind: '" + parts[0] + ", " + parts[1] + "' is not a key Apollo knows");
            continue;
        }
        binds_.erase(std::remove_if(binds_.begin(), binds_.end(),
                                    [&](const Bind& b) { return b.chord == *chord; }),
                     binds_.end());
    }
}

// --- connections -----------------------------------------------------------

const Connection* Config::connection(const std::string& name) const {
    for (const auto& c : connections_) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

std::optional<Connection> Config::resolveConnection(const std::string& requested,
                                                    std::string& error) const {
    const auto listNames = [this] {
        std::string names;
        for (const auto& c : connections_) names += (names.empty() ? "" : ", ") + c.name;
        return names;
    };

    if (!requested.empty()) {
        if (const Connection* found = connection(requested)) return *found;
        error = connections_.empty()
                    ? "No connections are configured. Add one with `apollo config add`."
                    : "No connection named '" + requested + "'. Configured: " + listNames();
        return std::nullopt;
    }

    if (connections_.empty()) {
        error = "No connections are configured. Add one with:\n"
                "  apollo config add <name> <user>@<host>";
        return std::nullopt;
    }
    if (connections_.size() == 1) return connections_.front();

    if (!general_.defaultConnection.empty()) {
        if (const Connection* found = connection(general_.defaultConnection)) return *found;
    }

    error = "Several connections are configured, so name the one you want:\n"
            "  apollo connect <name>\nConfigured: " + listNames() +
            "\nOr choose a default: apollo config default <name>";
    return std::nullopt;
}

// --- writing ---------------------------------------------------------------

bool Config::set(const std::string& path, const std::string& value, std::string* error) {
    if (const Setting* s = setting(path)) {
        if (const std::string problem = validate(*s, value); !problem.empty()) {
            if (error) *error = path + ": " + problem;
            return false;
        }
    } else if (path.rfind("connection.", 0) != 0) {
        if (error) *error = "Unknown setting '" + path + "'. Run `apollo config list`.";
        return false;
    }

    file_.set(path, value);
    derive();
    return true;
}

bool Config::unset(const std::string& path) {
    if (!file_.unset(path)) return false;
    derive();
    return true;
}

bool Config::addConnection(const Connection& conn, std::string* error) {
    if (conn.name.empty()) {
        if (error) *error = "A connection needs a name.";
        return false;
    }
    // The name becomes part of a config path, so it must not contain a dot or whitespace.
    for (const char c : conn.name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
            if (error) *error = "Connection names use letters, digits, - and _ only.";
            return false;
        }
    }
    if (connection(conn.name)) {
        if (error) *error = "A connection named '" + conn.name + "' already exists.";
        return false;
    }
    if (!conn.valid()) {
        if (error) *error = "A connection needs both a user and a host.";
        return false;
    }

    const std::string base = "connection." + conn.name + ".";
    file_.set(base + "host", conn.host);
    file_.set(base + "user", conn.user);
    if (conn.port != 22) file_.set(base + "port", std::to_string(conn.port));
    if (!conn.keyPath.empty()) file_.set(base + "key", conn.keyPath);
    if (!conn.password.empty()) file_.set(base + "password", conn.password);
    if (!conn.remoteDir.empty()) file_.set(base + "remote_dir", conn.remoteDir);
    if (!conn.jump.empty()) file_.set(base + "jump", conn.jump);

    derive();
    if (connections_.size() == 1) setDefaultConnection(conn.name);
    return true;
}

bool Config::removeConnection(const std::string& name) {
    if (!connection(name)) return false;
    if (!file_.removeSection("connection." + name)) return false;

    if (general_.defaultConnection == name) file_.unset("general.default_connection");
    derive();
    return true;
}

bool Config::setDefaultConnection(const std::string& name, std::string* error) {
    if (!name.empty() && !connection(name)) {
        if (error) *error = "No connection named '" + name + "'.";
        return false;
    }
    if (name.empty()) file_.unset("general.default_connection");
    else file_.set("general.default_connection", name);
    derive();
    return true;
}

bool Config::addBind(const std::string& spec, std::string* error) {
    const auto parts = ConfigFile::split(spec);
    if (parts.size() < 3) {
        if (error) *error = "Expected: <modifiers>, <key>, <action> [, <argument>]";
        return false;
    }
    if (!KeyChord::parse(parts[0], parts[1])) {
        if (error) *error = "'" + parts[0] + ", " + parts[1] + "' is not a key Apollo knows.";
        return false;
    }
    if (!findAction(parts[2])) {
        if (error) *error = "No action called '" + parts[2] + "'.";
        return false;
    }

    file_.append("bind", spec);
    derive();
    return true;
}

bool Config::removeBind(const std::string& spec) {
    const auto parts = ConfigFile::split(spec);
    if (parts.size() < 2) return false;

    const auto chord = KeyChord::parse(parts[0], parts[1]);
    if (!chord) return false;

    // Drop the user's own line if there is one, else record an `unbind`.
    bool removed = false;
    for (const auto* entry : file_.root().entriesNamed("bind")) {
        const auto existing = ConfigFile::split(entry->value);
        if (existing.size() < 2) continue;
        if (const auto other = KeyChord::parse(existing[0], existing[1]); other && *other == *chord) {
            removed = file_.removeMatching("bind", entry->value) || removed;
            break;
        }
    }
    if (!removed) file_.append("unbind", parts[0] + ", " + parts[1]);

    derive();
    return true;
}

bool Config::addCommand(const std::string& name, const std::string& exec,
                        const std::string& summary, std::string* error) {
    if (name.empty() || exec.empty()) {
        if (error) *error = "A command needs a name and something to run.";
        return false;
    }
    for (const auto& existing : commands_) {
        if (existing.name == name) {
            if (error) *error = "A command called '" + name + "' is already defined.";
            return false;
        }
    }

    std::string line = name + ", " + exec;
    if (!summary.empty()) line += ", \"" + summary + "\"";
    file_.append("command", line);
    derive();
    return true;
}

bool Config::removeCommand(const std::string& name) {
    if (!file_.removeMatching("command", name + ",")) return false;
    derive();
    return true;
}

// --- migration -------------------------------------------------------------

std::optional<std::string> migrateLegacyConfig(const fs::path& properties) {
    std::ifstream in(properties);
    if (!in) return std::nullopt;

    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '!') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        values[ConfigFile::trim(line.substr(0, eq))] = ConfigFile::trim(line.substr(eq + 1));
    }
    if (values.empty()) return std::nullopt;

    std::ostringstream out;
    out << "# Migrated from " << properties.filename().string() << ".\n"
        << "# Everything below can be edited freely; `apollo config` explains each setting.\n\n";

    out << "general {\n";
    if (const auto it = values.find("apollo.root"); it != values.end() && !it->second.empty()) {
        out << "    workspace = " << it->second << "\n";
    }
    if (const auto it = values.find("apollo.defaultConnection");
        it != values.end() && !it->second.empty()) {
        out << "    default_connection = " << it->second << "\n";
    }
    out << "}\n";

    // 0.2 used connection.<name>.<field>; 0.1 a flat ssh.<field> block.
    std::map<std::string, std::map<std::string, std::string>> hosts;
    for (const auto& [key, value] : values) {
        if (key.rfind("connection.", 0) == 0) {
            const auto rest = key.substr(11);
            const auto dot = rest.find('.');
            if (dot != std::string::npos) hosts[rest.substr(0, dot)][rest.substr(dot + 1)] = value;
        } else if (key.rfind("ssh.", 0) == 0) {
            hosts["default"][key.substr(4)] = value;
        }
    }

    for (const auto& host : hosts) {
        const auto& fields = host.second;
        out << "\nconnection " << host.first << " {\n";
        const auto emit = [&](const char* from, const char* to) {
            if (const auto it = fields.find(from); it != fields.end() && !it->second.empty()) {
                out << "    " << to << " = " << it->second << "\n";
            }
        };
        emit("host", "host");
        emit("user", "user");
        emit("username", "user");
        emit("port", "port");
        emit("key", "key");
        emit("password", "password");
        emit("remoteDir", "remote_dir");
        out << "}\n";
    }
    return out.str();
}

} // namespace apollo
