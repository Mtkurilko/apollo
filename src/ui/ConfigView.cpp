#include "ui/ConfigView.h"

#include <algorithm>
#include <map>

#include "core/Paths.h"

namespace apollo::ui {

using namespace ftxui;

namespace {

std::string prettyLabel(const std::string& path) {
    static const std::map<std::string, std::string> overrides = {
        {"general.follow_cwd", "Follow the shell"},
        {"general.default_connection", "Default destination"},
        {"decoration.dim_inactive", "Dim inactive pane"},
        {"terminal.osc52_clipboard", "Clipboard writes (OSC 52)"},
        {"terminal.shell_integration", "Shell integration"},
        {"terminal.word_chars", "Word characters"},
        {"terminal.scroll_lines", "Lines per scroll"},
        {"browser.dirs_first", "Directories first"},
        {"browser.git_status", "Git status"},
        {"browser.show_hidden", "Show dotfiles"},
    };
    if (const auto it = overrides.find(path); it != overrides.end()) return it->second;

    std::string label = path.substr(path.find('.') + 1);
    std::replace(label.begin(), label.end(), '_', ' ');
    if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
    return label;
}

} // namespace

void ConfigView::open() {
    open_ = true;
    page_ = 0;
    row_ = 0;
    scroll_ = 0;
    editing_ = false;
    prompting_ = false;
    error_.clear();
    flash_.clear();
}

void ConfigView::close() {
    open_ = false;
    editing_ = false;
    prompting_ = false;
}

std::vector<ConfigView::Page> ConfigView::pages() const {
    std::vector<Page> list = {Page::General,  Page::Appearance,  Page::Terminal,
                              Page::Browser,  Page::Keys,        Page::Commands,
                              Page::Connections, Page::Openers};
    if (!config_.issues().empty()) list.push_back(Page::Problems);
    list.push_back(Page::About);
    return list;
}

std::string ConfigView::pageName(Page page) {
    switch (page) {
        case Page::General:     return "General";
        case Page::Appearance:  return "Appearance";
        case Page::Terminal:    return "Terminal";
        case Page::Browser:     return "Browser";
        case Page::Keys:        return "Keys";
        case Page::Commands:    return "Commands";
        case Page::Connections: return "Connections";
        case Page::Openers:     return "Open with";
        case Page::Problems:    return "Problems";
        case Page::About:       return "About";
    }
    return "";
}

std::vector<const Config::Setting*> ConfigView::settingsFor(Page page) const {
    std::string prefix;
    switch (page) {
        case Page::General:    prefix = "general."; break;
        case Page::Appearance: prefix = "decoration."; break;
        case Page::Terminal:   prefix = "terminal."; break;
        case Page::Browser:    prefix = "browser."; break;
        default: return {};
    }

    std::vector<const Config::Setting*> found;
    for (const auto& setting : Config::schema()) {
        if (setting.path.rfind(prefix, 0) == 0) found.push_back(&setting);
    }
    // Colour overrides sit with the rest of the appearance settings.
    if (page == Page::Appearance) {
        for (const auto& setting : Config::schema()) {
            if (setting.path.rfind("colors.", 0) == 0) found.push_back(&setting);
        }
    }
    return found;
}

int ConfigView::rowCount() const {
    const Page page = pages()[static_cast<std::size_t>(page_)];
    switch (page) {
        case Page::Keys:        return static_cast<int>(config_.binds().size());
        case Page::Commands:    return static_cast<int>(config_.commands().size());
        case Page::Connections: return static_cast<int>(config_.connections().size());
        case Page::Openers:     return static_cast<int>(config_.openRules().size());
        case Page::Problems:    return static_cast<int>(config_.issues().size());
        case Page::About:       return 0;
        default:                return static_cast<int>(settingsFor(page).size());
    }
}

// --- editing ---------------------------------------------------------------

void ConfigView::applyChange(const std::string& path, const std::string& value) {
    std::string problem;
    const bool changed = value.empty() ? config_.unset(path)
                                       : config_.set(path, value, &problem);
    if (!changed && !problem.empty()) {
        error_ = problem;
        return;
    }

    error_.clear();
    if (!config_.save(&problem)) {
        error_ = problem;
        return;
    }
    flash_ = "saved";
    if (onChanged) onChanged();
}

void ConfigView::beginEdit() {
    const Page page = pages()[static_cast<std::size_t>(page_)];
    const auto settings = settingsFor(page);
    if (row_ < 0 || row_ >= static_cast<int>(settings.size())) return;

    const Config::Setting& setting = *settings[static_cast<std::size_t>(row_)];
    if (setting.type == Config::Setting::Type::Bool) { toggleBool(); return; }
    if (setting.type == Config::Setting::Type::Enum) { cycleChoice(1); return; }

    editing_ = true;
    editingPath_ = setting.path;
    editor_.set(config_.file().get(setting.path, ""));
    error_.clear();
}

void ConfigView::commitEdit() {
    if (!editing_) return;
    applyChange(editingPath_, editor_.text);
    if (error_.empty()) editing_ = false;
}

void ConfigView::cancelEdit() {
    editing_ = false;
    error_.clear();
}

void ConfigView::toggleBool() {
    const auto settings = settingsFor(pages()[static_cast<std::size_t>(page_)]);
    if (row_ < 0 || row_ >= static_cast<int>(settings.size())) return;

    const Config::Setting& setting = *settings[static_cast<std::size_t>(row_)];
    const bool current = ConfigFile::asBool(config_.valueOf(setting), false);
    applyChange(setting.path, current ? "false" : "true");
}

void ConfigView::cycleChoice(int direction) {
    const auto settings = settingsFor(pages()[static_cast<std::size_t>(page_)]);
    if (row_ < 0 || row_ >= static_cast<int>(settings.size())) return;

    const Config::Setting& setting = *settings[static_cast<std::size_t>(row_)];
    if (setting.type == Config::Setting::Type::Bool) { toggleBool(); return; }

    if (setting.type == Config::Setting::Type::Int) {
        const int value = ConfigFile::asInt(config_.valueOf(setting), setting.min);
        applyChange(setting.path, std::to_string(std::clamp(value + direction, setting.min, setting.max)));
        return;
    }
    const std::vector<std::string> choices = setting.path == "decoration.theme"
                                                 ? Config::availableThemes()
                                                 : setting.choices;
    if (choices.empty()) return;

    const std::string current = config_.valueOf(setting);
    const auto at = std::find(choices.begin(), choices.end(), current);
    int index = at == choices.end() ? 0 : static_cast<int>(at - choices.begin());
    const int count = static_cast<int>(choices.size());
    index = ((index + direction) % count + count) % count;
    applyChange(setting.path, choices[static_cast<std::size_t>(index)]);
}

void ConfigView::remove() {
    const Page page = pages()[static_cast<std::size_t>(page_)];
    std::string problem;

    if (page == Page::Connections) {
        const auto& connections = config_.connections();
        if (row_ < 0 || row_ >= static_cast<int>(connections.size())) return;
        const std::string name = connections[static_cast<std::size_t>(row_)].name;
        if (config_.removeConnection(name) && config_.save(&problem)) {
            flash_ = "removed " + name;
            row_ = std::max(0, row_ - 1);
            if (onChanged) onChanged();
        } else if (!problem.empty()) {
            error_ = problem;
        }
        return;
    }

    if (page == Page::Openers) {
        const auto& rules = config_.openRules();
        if (row_ < 0 || row_ >= static_cast<int>(rules.size())) return;
        const std::string match = rules[static_cast<std::size_t>(row_)].match;
        if (config_.removeOpenRule(match) && config_.save(&problem)) {
            flash_ = "forgot " + (match == "*" ? std::string("the catch-all") : "." + match);
            row_ = std::max(0, row_ - 1);
            if (onChanged) onChanged();
        } else if (!problem.empty()) {
            error_ = problem;
        }
        return;
    }

    if (page == Page::Commands) {
        const auto& commands = config_.commands();
        if (row_ < 0 || row_ >= static_cast<int>(commands.size())) return;
        const std::string name = commands[static_cast<std::size_t>(row_)].name;
        if (config_.removeCommand(name) && config_.save(&problem)) {
            flash_ = "removed " + name;
            row_ = std::max(0, row_ - 1);
            if (onChanged) onChanged();
        }
        return;
    }

    if (page == Page::Keys) {
        const auto& binds = config_.binds();
        if (row_ < 0 || row_ >= static_cast<int>(binds.size())) return;
        const Bind& bind = binds[static_cast<std::size_t>(row_)];

        std::string mods;
        if (bind.chord.mods & ModLeader) mods += "LEADER ";
        if (bind.chord.mods & ModCtrl) mods += "CTRL ";
        if (bind.chord.mods & ModAlt) mods += "ALT ";
        if (bind.chord.mods & ModShift) mods += "SHIFT ";
        if (!mods.empty()) mods.pop_back();

        if (config_.removeBind(mods + ", " + bind.chord.key) && config_.save(&problem)) {
            flash_ = "unbound " + bind.chord.describe();
            row_ = std::max(0, row_ - 1);
            if (onChanged) onChanged();
        }
        return;
    }

    const auto settings = settingsFor(page);
    if (row_ < 0 || row_ >= static_cast<int>(settings.size())) return;
    const Config::Setting& setting = *settings[static_cast<std::size_t>(row_)];
    config_.unset(setting.path);
    if (config_.save(&problem)) {
        flash_ = "reset to default";
        if (onChanged) onChanged();
    }
}

void ConfigView::beginAdd() {
    const Page page = pages()[static_cast<std::size_t>(page_)];

    if (page == Page::Connections) {
        prompting_ = true;
        promptInput_.clear();
        prompt_ = Prompt{};
        prompt_.title = "Add an SSH destination";
        prompt_.fields = {
            {"Name", "a short name, e.g. lab", false, false, nullptr},
            {"Address", "user@host", false, false, nullptr},
            {"Port", "22", false, true, nullptr},
            {"Key", "~/.ssh/id_ed25519 — leave blank to use a password", false, true, nullptr},
            // Only worth asking when there is no key to use instead.
            {"Password", "needs sshpass; a key is safer", true, true,
             [](const std::vector<std::string>& so_far) { return !so_far[3].empty(); }},
        };
        prompt_.finish = [this](const std::vector<std::string>& answers) {
            Connection conn;
            conn.name = answers[0];
            const std::string address = answers[1];
            if (const auto at = address.find('@'); at != std::string::npos) {
                conn.user = address.substr(0, at);
                conn.host = address.substr(at + 1);
            } else {
                error_ = "Expected user@host, got: " + address;
                return;
            }
            if (!answers[2].empty()) conn.port = ConfigFile::asInt(answers[2], 22);
            conn.keyPath = answers[3];
            conn.password = answers[4];

            std::string problem;
            if (!config_.addConnection(conn, &problem)) { error_ = problem; return; }
            if (!config_.save(&problem)) { error_ = problem; return; }
            flash_ = "added " + conn.name;
            if (onChanged) onChanged();
        };
        return;
    }

    if (page == Page::Commands) {
        prompting_ = true;
        promptInput_.clear();
        prompt_ = Prompt{};
        prompt_.title = "Add a command";
        prompt_.fields = {
            {"Name", "what you will type after `apollo`", false, false, nullptr},
            {"Runs", "a shell command or a script path", false, false, nullptr},
            {"Description", "shown in the palette", false, true, nullptr},
        };
        prompt_.finish = [this](const std::vector<std::string>& answers) {
            std::string problem;
            if (!config_.addCommand(answers[0], answers[1], answers[2], &problem)) {
                error_ = problem;
                return;
            }
            if (!config_.save(&problem)) { error_ = problem; return; }
            flash_ = "added " + answers[0];
            if (onChanged) onChanged();
        };
        return;
    }
}

void ConfigView::advancePrompt() {
    const Field& field = prompt_.fields[prompt_.at];
    const std::string answer = promptInput_.text;
    if (answer.empty() && !field.optional) {
        error_ = field.label + " is required";
        return;
    }

    error_.clear();
    prompt_.answers.push_back(answer);
    promptInput_.clear();
    ++prompt_.at;

    // A field the earlier answers made pointless is answered as blank.
    while (prompt_.at < prompt_.fields.size() &&
           prompt_.fields[prompt_.at].skip && prompt_.fields[prompt_.at].skip(prompt_.answers)) {
        prompt_.answers.emplace_back();
        ++prompt_.at;
    }

    if (prompt_.at >= prompt_.fields.size()) {
        prompting_ = false;
        if (prompt_.finish) prompt_.finish(prompt_.answers);
    }
}

// --- keys ------------------------------------------------------------------

bool ConfigView::onKey(const KeyChord& chord, const std::string& raw) {
    if (!open_) return false;
    flash_.clear();

    if (prompting_) {
        if (chord.key == "escape") { prompting_ = false; error_.clear(); return true; }
        if (chord.key == "enter") { advancePrompt(); return true; }
        promptInput_.onKey(chord, raw);
        return true;
    }

    if (editing_) {
        if (chord.key == "escape") { cancelEdit(); return true; }
        if (chord.key == "enter") { commitEdit(); return true; }
        editor_.onKey(chord, raw);
        return true;
    }

    const auto list = pages();
    const int pageCount = static_cast<int>(list.size());

    if (chord.key == "escape" || (chord.mods == ModCtrl && chord.key == "c")) {
        close();
        return true;
    }
    // Shift+Tab must not fall into the forward branch: the key is still "tab".
    if ((chord.mods == ModNone && chord.key == "tab") ||
        (chord.mods == ModNone && chord.key == "right")) {
        page_ = (page_ + 1) % pageCount;
        row_ = 0;
        scroll_ = 0;
        return true;
    }
    if ((chord.mods == ModShift && chord.key == "tab") ||
        (chord.mods == ModNone && chord.key == "left")) {
        page_ = (page_ + pageCount - 1) % pageCount;
        row_ = 0;
        scroll_ = 0;
        return true;
    }
    if (chord.key == "up" || (chord.mods == ModNone && chord.key == "k")) {
        row_ = std::max(0, row_ - 1);
        return true;
    }
    if (chord.key == "down" || (chord.mods == ModNone && chord.key == "j")) {
        row_ = std::min(std::max(0, rowCount() - 1), row_ + 1);
        return true;
    }
    if (chord.key == "home") { row_ = 0; return true; }
    if (chord.key == "end") { row_ = std::max(0, rowCount() - 1); return true; }
    if (chord.key == "enter" || chord.key == "space") { beginEdit(); return true; }
    if (chord.key == "a") { beginAdd(); return true; }
    if (chord.key == "d" || chord.key == "delete" || chord.key == "backspace") {
        remove();
        return true;
    }
    if (chord.key == "e") {
        if (!onEditExternally) return true;
        close();
        onEditExternally();
        return true;
    }
    return true; // the config screen owns every key while it is up
}

namespace {
constexpr int kRowBase = 0;    // + the index of the row on the current page
constexpr int kTabBase = 1000; // + the index of the section tab
constexpr int kChange = 2000;
constexpr int kEditor = 2001;
constexpr int kClose = 2002;
} // namespace

bool ConfigView::onMouse(const Mouse& mouse) {
    if (!open_) return false;

    if (mouse.button == Mouse::WheelUp || mouse.button == Mouse::WheelDown) {
        const int delta = mouse.button == Mouse::WheelUp ? -3 : 3;
        row_ = std::clamp(row_ + delta, 0, std::max(0, rowCount() - 1));
        return true;
    }
    if (mouse.button != Mouse::Left || mouse.motion != Mouse::Pressed) return true;

    const int hit = spots_.at(mouse.x, mouse.y);
    if (hit < 0) return true;

    if (hit == kClose) {
        if (prompting_) { prompting_ = false; error_.clear(); }
        else if (editing_) cancelEdit();
        else close();
        return true;
    }
    if (hit == kEditor) {
        if (!onEditExternally) return true;
        close();
        onEditExternally();
        return true;
    }
    if (hit == kChange) {
        if (prompting_) advancePrompt();
        else if (editing_) commitEdit();
        else beginEdit();
        return true;
    }
    if (hit >= kTabBase) {
        const int wanted = hit - kTabBase;
        if (wanted < static_cast<int>(pages().size())) {
            if (editing_) cancelEdit();
            page_ = wanted;
            row_ = 0;
            scroll_ = 0;
        }
        return true;
    }

    // A row: the first click selects, a second one on the same row opens it.
    const int wanted = hit - kRowBase;
    if (wanted < 0 || wanted >= rowCount()) return true;
    if (editing_ && wanted != row_) cancelEdit();
    if (wanted == row_ && !editing_ && !prompting_) beginEdit();
    else row_ = wanted;
    return true;
}

// --- rendering -------------------------------------------------------------

Element ConfigView::renderSettings(Page page, const Theme& theme, int width, int height) {
    const auto settings = settingsFor(page);
    const int visible = std::max(3, height - 3);

    if (row_ < scroll_) scroll_ = row_;
    if (row_ >= scroll_ + visible) scroll_ = row_ - visible + 1;
    scroll_ = std::clamp(scroll_, 0, std::max(0, static_cast<int>(settings.size()) - visible));

    const int labelWidth = 22;
    Elements rows;
    for (int i = scroll_; i < std::min(scroll_ + visible, static_cast<int>(settings.size())); ++i) {
        const Config::Setting& setting = *settings[static_cast<std::size_t>(i)];
        const bool isSelected = i == row_;
        const std::string current = config_.valueOf(setting);
        const bool isDefault = current == setting.defaultValue;

        std::string label = prettyLabel(setting.path);
        if (setting.path.rfind("colors.", 0) == 0) label = "Colour: " + label;
        label.resize(static_cast<std::size_t>(labelWidth), ' ');

        Element value;
        if (isSelected && editing_ && editingPath_ == setting.path) {
            value = editor_.render(theme, setting.defaultValue, true);
        } else if (setting.type == Config::Setting::Type::Bool) {
            const bool on = ConfigFile::asBool(current, false);
            value = hbox({
                text(on ? "● " : "○ ") | color(on ? toFtx(theme.success) : toFtx(theme.muted)),
                text(on ? "on" : "off") | color(toFtx(theme.fg)),
            });
        } else if (setting.type == Config::Setting::Type::Color) {
            const auto parsed = Rgb::parse(current);
            value = hbox({
                text(parsed ? "██ " : "   ") | color(parsed ? toFtx(*parsed) : toFtx(theme.muted)),
                text(current.empty() ? "(theme default)" : current) |
                    color(current.empty() ? toFtx(theme.muted) : toFtx(theme.fg)),
            });
        } else {
            const std::string shown =
                current.empty() ? "(unset)" : elide(current, std::max(8, width - labelWidth - 10));
            value = text(shown) | color(current.empty() ? toFtx(theme.muted) : toFtx(theme.fg));
        }

        Elements cells{
            text(isSelected ? " ▸ " : "   ") | color(toFtx(theme.accent)),
            text(label) | color(toFtx(isSelected ? theme.fg : theme.muted)),
            std::move(value),
            filler(),
        };
        if (!isDefault) cells.push_back(text("• ") | color(toFtx(theme.accentAlt)));

        Element row = hbox(std::move(cells));
        if (isSelected) row = std::move(row) | bgcolor(toFtx(theme.selection));
        rows.push_back(spots_.track(kRowBase + i, std::move(row)));
    }
    while (static_cast<int>(rows.size()) < visible) rows.push_back(text(""));

    Element explanation = text("");
    if (row_ >= 0 && row_ < static_cast<int>(settings.size())) {
        const Config::Setting& setting = *settings[static_cast<std::size_t>(row_)];
        std::string detail = setting.summary;
        if (setting.type == Config::Setting::Type::Enum) {
            const std::vector<std::string> choices = setting.path == "decoration.theme"
                                                         ? Config::availableThemes()
                                                         : setting.choices;
            std::string options;
            for (const auto& choice : choices) {
                options += (options.empty() ? "" : " · ") + choice;
            }
            detail += "  —  " + options;
        } else if (setting.type == Config::Setting::Type::Int) {
            detail += "  —  " + std::to_string(setting.min) + " to " + std::to_string(setting.max);
        }
        explanation = vbox({
            text(elide(detail, std::max(10, width - 6))) | color(toFtx(theme.fg)),
            text(elide("  " + setting.path + "   default: " +
                           (setting.defaultValue.empty() ? "unset" : setting.defaultValue) +
                           "   d resets it",
                       std::max(10, width - 6))) |
                color(toFtx(theme.muted)),
        });
    }

    return vbox({
        vbox(std::move(rows)),
        separator() | color(toFtx(theme.border)),
        std::move(explanation),
    });
}

Element ConfigView::renderKeys(const Theme& theme, int height) {
    Elements rows;
    const auto& binds = config_.binds();
    const int visible = std::max(3, height - 3);

    if (row_ < scroll_) scroll_ = row_;
    if (row_ >= scroll_ + visible) scroll_ = row_ - visible + 1;
    scroll_ = std::clamp(scroll_, 0, std::max(0, static_cast<int>(binds.size()) - visible));

    for (int i = scroll_; i < std::min(scroll_ + visible, static_cast<int>(binds.size())); ++i) {
        const Bind& bind = binds[static_cast<std::size_t>(i)];
        const ActionInfo* action = findAction(bind.action);

        std::string chord = bind.chord.describe();
        chord.resize(std::max<std::size_t>(chord.size(), 18), ' ');

        Element row = hbox({
            text(i == row_ ? " ▸ " : "   ") | color(toFtx(theme.accent)),
            text(chord) | color(toFtx(theme.accent)),
            text(bind.describeAction()) | color(toFtx(theme.fg)),
            filler(),
            text(action ? action->summary + " " : "") | color(toFtx(theme.muted)),
        });
        if (i == row_) row = std::move(row) | bgcolor(toFtx(theme.selection));
        rows.push_back(spots_.track(kRowBase + i, std::move(row)));
    }
    while (static_cast<int>(rows.size()) < visible) rows.push_back(text(""));

    return vbox({
        vbox(std::move(rows)),
        separator() | color(toFtx(theme.border)),
        vbox({
            text("Leader is " + config_.general().leader.describe() +
                 ". Press it, release, then the key.") | color(toFtx(theme.fg)),
            text("  d removes a bind. Add your own with `bind = MODS, KEY, action` in the config.") |
                color(toFtx(theme.muted)),
        }),
    });
}

Element ConfigView::renderOpeners(const Theme& theme, int height) {
    const auto& rules = config_.openRules();
    const int visible = std::max(3, height - 3);

    Elements rows;
    for (int i = 0; i < static_cast<int>(rules.size()) && i < visible; ++i) {
        const OpenRule& rule = rules[static_cast<std::size_t>(i)];
        std::string what = rule.match == "*" ? "everything else" : "." + rule.match;
        what.resize(std::max<std::size_t>(what.size(), 18), ' ');

        const std::string how = rule.how == "desktop" ? "the desktop opener"
                                : rule.how == "editor" ? "your editor"
                                : rule.how == "ask"    ? "ask each time"
                                                       : rule.how;

        Element row = hbox({
            text(i == row_ ? " ▸ " : "   ") | color(toFtx(theme.accent)),
            text(what) | color(toFtx(theme.fg)),
            text(how) | color(toFtx(theme.muted)),
            filler(),
        });
        if (i == row_) row = std::move(row) | bgcolor(toFtx(theme.selection));
        rows.push_back(spots_.track(kRowBase + i, std::move(row)));
    }
    if (rules.empty()) {
        rows.push_back(text("   nothing remembered yet — Apollo asks the first time")
                       | color(toFtx(theme.muted)));
    }
    while (static_cast<int>(rows.size()) < visible) rows.push_back(text(""));

    return vbox({
        vbox(std::move(rows)),
        separator() | color(toFtx(theme.border)),
        vbox({
            text("What opening a file from the browser does. d forgets one.") |
                color(toFtx(theme.fg)),
            text("  Apollo asks the first time it sees a type, and remembers the answer.") |
                color(toFtx(theme.muted)),
        }),
    });
}

Element ConfigView::renderCommands(const Theme& theme, int height) {
    const auto& commands = config_.commands();
    const int visible = std::max(3, height - 3);

    Elements rows;
    for (int i = 0; i < static_cast<int>(commands.size()) && i < visible; ++i) {
        const DeclaredCommand& command = commands[static_cast<std::size_t>(i)];
        std::string name = command.name;
        name.resize(std::max<std::size_t>(name.size(), 16), ' ');

        Element row = hbox({
            text(i == row_ ? " ▸ " : "   ") | color(toFtx(theme.accent)),
            text(name) | color(toFtx(theme.fg)),
            text(elide(command.exec, 40)) | color(toFtx(theme.muted)),
            filler(),
            text(command.summary.empty() ? "" : command.summary + " ") | color(toFtx(theme.muted)),
        });
        if (i == row_) row = std::move(row) | bgcolor(toFtx(theme.selection));
        rows.push_back(spots_.track(kRowBase + i, std::move(row)));
    }
    if (commands.empty()) {
        rows.push_back(text("   no commands defined yet") | color(toFtx(theme.muted)));
    }
    while (static_cast<int>(rows.size()) < visible) rows.push_back(text(""));

    return vbox({
        vbox(std::move(rows)),
        separator() | color(toFtx(theme.border)),
        vbox({
            text("a adds a command, d removes one.") | color(toFtx(theme.fg)),
            text("  Any executable in " + paths::contractUser(paths::commandsDir()) +
                 " is picked up automatically.") | color(toFtx(theme.muted)),
        }),
    });
}

Element ConfigView::renderConnections(const Theme& theme, int height) {
    const auto& connections = config_.connections();
    const int visible = std::max(3, height - 3);
    const std::string preferred = config_.general().defaultConnection;

    Elements rows;
    for (int i = 0; i < static_cast<int>(connections.size()) && i < visible; ++i) {
        const Connection& conn = connections[static_cast<std::size_t>(i)];
        std::string name = conn.name;
        name.resize(std::max<std::size_t>(name.size(), 14), ' ');

        std::string auth = "no credentials";
        Color authColor = toFtx(theme.warning);
        if (!conn.keyPath.empty()) { auth = "key"; authColor = toFtx(theme.success); }
        else if (!conn.password.empty()) { auth = "password"; authColor = toFtx(theme.warning); }

        Element row = hbox({
            text(i == row_ ? " ▸ " : "   ") | color(toFtx(theme.accent)),
            text(name) | color(toFtx(theme.fg)),
            text(conn.describe()) | color(toFtx(theme.muted)),
            filler(),
            text(auth + " ") | color(authColor),
            text(conn.name == preferred ? "default " : "        ") | color(toFtx(theme.accentAlt)),
        });
        if (i == row_) row = std::move(row) | bgcolor(toFtx(theme.selection));
        rows.push_back(spots_.track(kRowBase + i, std::move(row)));
    }
    if (connections.empty()) {
        rows.push_back(text("   nothing configured — press a to add one") |
                       color(toFtx(theme.muted)));
    }
    while (static_cast<int>(rows.size()) < visible) rows.push_back(text(""));

    std::string rule = "One destination: `apollo connect` uses it with no argument.";
    if (connections.size() > 1) {
        rule = preferred.empty()
                   ? "Several destinations: name one — `apollo connect " +
                         connections.front().name + "` — or set a default."
                   : "Several destinations: a bare `apollo connect` uses '" + preferred + "'.";
    }

    return vbox({
        vbox(std::move(rows)),
        separator() | color(toFtx(theme.border)),
        vbox({
            text(rule) | color(toFtx(theme.fg)),
            text("  a adds, d removes. Prefer a key: a password has to be passed to sshpass.") |
                color(toFtx(theme.muted)),
        }),
    });
}

Element ConfigView::renderProblems(const Theme& theme, int height) {
    Elements rows;
    for (const auto& issue : config_.issues()) {
        rows.push_back(hbox({
            text("  ! ") | color(toFtx(theme.warning)),
            text(issue) | color(toFtx(theme.fg)),
        }));
    }
    while (static_cast<int>(rows.size()) < std::max(3, height)) rows.push_back(text(""));
    return vbox(std::move(rows));
}

Element ConfigView::renderAbout(const Theme& theme) {
    return vbox({
        text(""),
        hbox({text("  Apollo ") | bold | color(toFtx(theme.accent)),
              text(APOLLO_VERSION) | color(toFtx(theme.muted))}),
        text(""),
        hbox({text("  config   ") | color(toFtx(theme.muted)),
              text(paths::contractUser(config_.path()))}),
        hbox({text("  commands ") | color(toFtx(theme.muted)),
              text(paths::contractUser(paths::commandsDir()))}),
        hbox({text("  themes   ") | color(toFtx(theme.muted)),
              text(paths::contractUser(paths::themesDir()))}),
        text(""),
        text("  Everything on these pages is that one file. Edit it by hand if you") |
            color(toFtx(theme.muted)),
        text("  prefer — Apollo re-reads it the moment you save.") | color(toFtx(theme.muted)),
        filler(),
    });
}

Element ConfigView::renderPrompt(const Theme& theme) {
    const Field& field = prompt_.fields[prompt_.at];

    Elements answered;
    for (std::size_t i = 0; i < prompt_.answers.size(); ++i) {
        answered.push_back(hbox({
            text("  ✓ ") | color(toFtx(theme.success)),
            text(prompt_.fields[i].label + ": ") | color(toFtx(theme.muted)),
            text(prompt_.answers[i].empty() ? "(skipped)" : prompt_.answers[i]),
        }));
    }

    return vbox({
        text(" " + prompt_.title) | bold | color(toFtx(theme.accent)),
        text(""),
        vbox(std::move(answered)),
        hbox({
            text("  " + field.label + ": ") | color(toFtx(theme.fg)),
            promptInput_.render(theme, field.placeholder, true, field.mask),
        }),
        text(""),
        text(error_.empty() ? "  Enter to continue, Esc to cancel"
                            : "  " + error_) |
            color(error_.empty() ? toFtx(theme.muted) : toFtx(theme.error)),
    });
}

Element ConfigView::render(const Theme& theme, const DecorationSettings& decoration,
                           int width, int height) {
    spots_.clear();
    const auto list = pages();
    page_ = std::clamp(page_, 0, static_cast<int>(list.size()) - 1);
    const Page page = list[static_cast<std::size_t>(page_)];
    row_ = std::clamp(row_, 0, std::max(0, rowCount() - 1));

    const int panelWidth = std::clamp(width - 6, 60, 104);
    const int panelHeight = std::clamp(height - 4, 16, 40);
    const int bodyHeight = panelHeight - 9;

    Elements tabs;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const bool active = static_cast<int>(i) == page_;
        Element tab = text(" " + pageName(list[i]) + " ");
        if (active) tab = std::move(tab) | bgcolor(toFtx(theme.accent)) | color(toFtx(theme.bg)) | bold;
        else tab = std::move(tab) | color(toFtx(theme.muted));
        if (list[i] == Page::Problems && !active) tab = std::move(tab) | color(toFtx(theme.warning));
        tabs.push_back(spots_.track(kTabBase + static_cast<int>(i), std::move(tab)));
    }

    Element body;
    switch (page) {
        case Page::Keys:        body = renderKeys(theme, bodyHeight); break;
        case Page::Commands:    body = renderCommands(theme, bodyHeight); break;
        case Page::Connections: body = renderConnections(theme, bodyHeight); break;
        case Page::Openers:     body = renderOpeners(theme, bodyHeight); break;
        case Page::Problems:    body = renderProblems(theme, bodyHeight); break;
        case Page::About:       body = renderAbout(theme); break;
        default:                body = renderSettings(page, theme, panelWidth, bodyHeight); break;
    }
    if (prompting_) body = renderPrompt(theme);

    Elements footer;
    footer.push_back(text(" "));
    if (prompting_) {
        footer.push_back(spots_.track(kChange, hint("Enter", "next", theme)));
        footer.push_back(text("  "));
        footer.push_back(spots_.track(kClose, hint("Esc", "cancel", theme)));
    } else {
        footer.push_back(hint("Tab", "section", theme));
        footer.push_back(text("  "));
        footer.push_back(hint("↑↓", "move", theme));
        footer.push_back(text("  "));
        footer.push_back(spots_.track(kChange, hint("Enter", editing_ ? "save" : "change", theme)));
        footer.push_back(text("  "));
        footer.push_back(spots_.track(kEditor, hint("e", "editor", theme)));
        footer.push_back(text("  "));
        footer.push_back(spots_.track(kClose, hint("Esc", "close", theme)));
    }
    footer.push_back(filler());
    if (!error_.empty()) footer.push_back(text(error_ + " ") | color(toFtx(theme.error)));
    else if (!flash_.empty()) footer.push_back(text(flash_ + " ") | color(toFtx(theme.success)));

    Element content = vbox({
        hbox({
            text(" apollo config ") | bold | color(toFtx(theme.accent)),
            // The file all of these pages write to.
            text(elide(paths::contractUser(config_.path()), std::max(0, panelWidth - 26))) |
                color(toFtx(theme.muted)),
            filler(),
            text(std::string("v") + APOLLO_VERSION + " ") | color(toFtx(theme.muted)),
        }),
        separator() | color(toFtx(theme.border)),
        hbox(std::move(tabs)),
        separator() | color(toFtx(theme.border)),
        std::move(body) | flex,
        separator() | color(toFtx(theme.border)),
        hbox(std::move(footer)),
    });

    return modal(std::move(content), theme, decoration, panelWidth, panelHeight);
}

} // namespace apollo::ui
