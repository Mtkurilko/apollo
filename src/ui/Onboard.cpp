#include "ui/Onboard.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "core/ConfigFile.h"
#include "core/Paths.h"
#include "core/Process.h"

namespace fs = std::filesystem;

namespace apollo::ui {

using namespace ftxui;

namespace {

const char* kShellIntegration = R"(# Apollo shell integration.
# Reports the working directory (OSC 7) and marks prompts and commands
# (OSC 133), so Apollo's browser follows along, Leader+Up/Down jumps between
# commands, and a Space leader knows exactly when the prompt is empty.
# Harmless in any other terminal.

__apollo_osc7() { printf '\033]7;file://%s%s\033\\' "${HOSTNAME:-$(hostname)}" "$PWD"; }
__apollo_mark() { printf '\033]133;A\033\\'; }
__apollo_run() { printf '\033]133;C\033\\'; }

if [ -n "$ZSH_VERSION" ]; then
    precmd_functions+=(__apollo_osc7 __apollo_mark)
    preexec_functions+=(__apollo_run)
elif [ -n "$BASH_VERSION" ]; then
    PROMPT_COMMAND="__apollo_osc7; __apollo_mark${PROMPT_COMMAND:+; $PROMPT_COMMAND}"
    # Printed after a command is read, before it runs. bash 4.4 and later.
    case "$PS0" in *133\;C*) ;; *) PS0="${PS0}"'\033]133;C\033\\' ;; esac
fi
)";

std::string shellRcFile() {
    const std::string shell = process::userShell();
    if (shell.find("zsh") != std::string::npos) return (paths::home() / ".zshrc").string();
    if (shell.find("bash") != std::string::npos) return (paths::home() / ".bashrc").string();
    return "";
}

fs::path integrationScript() { return paths::configDir() / "shell-integration.sh"; }

struct LeaderChoice {
    const char* spec;
    const char* title;
    const char* detail;
};
const std::vector<LeaderChoice>& leaderChoices() {
    static const std::vector<LeaderChoice> choices = {
        {"space", "Space", "at an empty prompt and in the browser"},
        {"ctrl+space", "Ctrl+Space", "everywhere, unless the system uses it for input sources"},
        {"ctrl+a", "Ctrl+A", "like screen; the shell loses start-of-line"},
        {"ctrl+b", "Ctrl+B", "like tmux"},
    };
    return choices;
}
// The row after the presets: press whatever key you like.
int customLeaderRow() { return static_cast<int>(leaderChoices().size()); }

// A name for a connection nobody named. "lab.example.com" gives "lab",
// "10.0.0.5" gives "10-0-0-5".
std::string nameFor(const std::string& host) {
    const bool hasLetters = std::any_of(host.begin(), host.end(), [](unsigned char c) {
        return std::isalpha(c);
    });
    std::string name = hasLetters && host.find(':') == std::string::npos
                           ? host.substr(0, host.find('.'))
                           : host;
    for (char& c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '-';
    }
    return name.empty() ? "remote" : name;
}

std::string defaultKey() {
    for (const char* name : {"id_ed25519", "id_ecdsa", "id_rsa"}) {
        std::error_code ec;
        if (fs::exists(paths::home() / ".ssh" / name, ec)) return std::string("~/.ssh/") + name;
    }
    return "";
}

} // namespace

// --- lifecycle -------------------------------------------------------------

void Onboard::start() {
    open_ = true;
    step_ = Step::Workspace;
    error_.clear();
    capturingLeader_ = false;

    workspace_.set(config_.general().workspace);

    const auto names = Theme::builtinNames();
    const auto at = std::find(names.begin(), names.end(), config_.decoration().theme);
    themeIndex_ = at == names.end() ? 0 : static_cast<int>(at - names.begin());
    previewTheme_ = names[static_cast<std::size_t>(themeIndex_)];

    leaderIndex_ = 0;
    customLeader_.clear();
    const KeyChord& leader = config_.general().leader;
    bool preset = false;
    for (std::size_t i = 0; i < leaderChoices().size(); ++i) {
        if (KeyChord::parse(leaderChoices()[i].spec) == leader) {
            leaderIndex_ = static_cast<int>(i);
            preset = true;
        }
    }
    if (!preset) {
        leaderIndex_ = customLeaderRow();
        customLeader_ = leader.spec();
    }

    if (connectionKey_.text.empty()) connectionKey_.set(defaultKey());
}

void Onboard::goTo(Step step) {
    step_ = step;
    capturingLeader_ = false;
    error_.clear();
}

void Onboard::back() {
    switch (step_) {
        case Step::Workspace:        break;
        case Step::Appearance:       goTo(Step::Workspace); break;
        case Step::Leader:           goTo(Step::Appearance); break;
        case Step::Connection:       goTo(Step::Leader); break;
        case Step::ShellIntegration: goTo(Step::Connection); break;
        case Step::Done:             goTo(Step::ShellIntegration); break;
    }
}

bool Onboard::saveConnection() {
    if (!wantsConnection_ || connectionAddress_.text.empty()) return true;

    Connection conn;
    std::string address = ConfigFile::trim(connectionAddress_.text);
    const auto at = address.find('@');
    if (at == std::string::npos || at == 0 || at + 1 >= address.size()) {
        error_ = "Expected user@host, or user@host:port";
        connectionField_ = Address;
        return false;
    }
    conn.user = address.substr(0, at);
    conn.host = address.substr(at + 1);
    // One colon is a port. More than one is an IPv6 address, which has no room for one.
    if (const auto colon = conn.host.find(':');
        colon != std::string::npos && conn.host.find(':', colon + 1) == std::string::npos) {
        const int port = ConfigFile::asInt(conn.host.substr(colon + 1), 0);
        if (port < 1 || port > 65535) {
            error_ = "A port is a number between 1 and 65535";
            connectionField_ = Address;
            return false;
        }
        conn.port = port;
        conn.host = conn.host.substr(0, colon);
    }

    conn.name = ConfigFile::trim(connectionName_.text);
    if (conn.name.empty()) conn.name = nameFor(conn.host);
    conn.keyPath = ConfigFile::trim(connectionKey_.text);
    // One or the other. A key makes the password dead weight.
    if (conn.keyPath.empty()) conn.password = connectionPassword_.text;

    // Coming back through this step replaces what it saved last time.
    if (!savedConnection_.empty()) config_.removeConnection(savedConnection_);

    std::string problem;
    if (!config_.addConnection(conn, &problem)) {
        error_ = problem;
        connectionField_ = Name;
        return false;
    }
    savedConnection_ = conn.name;
    return true;
}

void Onboard::advance() {
    error_.clear();
    switch (step_) {
        case Step::Workspace: {
            const std::string wanted =
                workspace_.text.empty() ? "~" : ConfigFile::trim(workspace_.text);
            std::error_code ec;
            if (!fs::is_directory(paths::expandUser(wanted), ec)) {
                error_ = "No such directory: " + wanted;
                return;
            }
            config_.set("general.workspace", wanted);
            goTo(Step::Appearance);
            break;
        }

        case Step::Appearance:
            config_.set("decoration.theme", previewTheme_);
            goTo(Step::Leader);
            break;

        case Step::Leader: {
            if (leaderIndex_ == customLeaderRow() && customLeader_.empty()) {
                capturingLeader_ = true;
                return;
            }
            const std::string spec = leaderIndex_ == customLeaderRow()
                                         ? customLeader_
                                         : leaderChoices()[static_cast<std::size_t>(leaderIndex_)].spec;
            std::string problem;
            if (!config_.set("general.leader", spec, &problem)) { error_ = problem; return; }
            goTo(Step::Connection);
            break;
        }

        case Step::Connection:
            if (!saveConnection()) return;
            goTo(Step::ShellIntegration);
            break;

        case Step::ShellIntegration:
            shellIntegrationResult_.clear();
            if (shellIntegrationChoice_) {
                std::string where;
                if (writeShellIntegration(where)) shellIntegrationResult_ = where;
                else { error_ = where; return; }
            }
            goTo(Step::Done);
            break;

        case Step::Done:
            finish();
            return;
    }

    std::string problem;
    if (!config_.save(&problem)) error_ = problem;
    if (onChanged) onChanged();
}

void Onboard::refreshShellIntegration() {
    const fs::path script = integrationScript();
    std::error_code ec;
    if (!fs::exists(script, ec)) return;

    std::ifstream in(script);
    std::stringstream current;
    current << in.rdbuf();
    if (current.str() == kShellIntegration) return;

    std::ofstream out(script, std::ios::trunc);
    if (out) out << kShellIntegration;
}

bool Onboard::writeShellIntegration(std::string& where) {
    if (!paths::ensureDir(paths::configDir(), &where)) return false;

    const fs::path script = integrationScript();
    {
        std::ofstream out(script, std::ios::trunc);
        if (!out) { where = "cannot write " + script.string(); return false; }
        out << kShellIntegration;
    }

    const std::string rc = shellRcFile();
    if (rc.empty()) {
        where = "wrote " + paths::contractUser(script) + "; source it from your shell";
        return true;
    }

    const std::string marker = script.string();
    {
        std::ifstream existing(rc);
        std::string line;
        while (std::getline(existing, line)) {
            if (line.find(marker) != std::string::npos) {
                where = "already set up in " + paths::contractUser(rc);
                return true;
            }
        }
    }

    std::ofstream append(rc, std::ios::app);
    if (!append) { where = "cannot write " + rc; return false; }
    append << "\n# Added by `apollo setup`.\n"
           << "[ -f \"" << script.string() << "\" ] && . \"" << script.string() << "\"\n";

    where = "added one line to " + paths::contractUser(rc);
    return true;
}

void Onboard::finish() {
    std::string problem;
    config_.save(&problem);
    open_ = false;
    paths::markSetupDone();
    if (onFinished) onFinished();
}

// Tab in the directory field. Finishes the name if only one fits, otherwise
// as much as they all share.
void Onboard::completeWorkspace() {
    const std::string typed = workspace_.text;
    const auto slash = typed.rfind('/');
    const std::string base = slash == std::string::npos ? "" : typed.substr(0, slash + 1);
    const std::string prefix = slash == std::string::npos ? typed : typed.substr(slash + 1);
    if (typed == "~") { workspace_.set("~/"); return; }

    const fs::path dir = paths::expandUser(base.empty() ? "." : base);
    std::vector<std::string> matches;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        const std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) != 0) continue;
        if (name[0] == '.' && (prefix.empty() || prefix[0] != '.')) continue;
        if (!entry.is_directory(ec)) continue;
        matches.push_back(name);
    }
    if (matches.empty()) return;

    std::string shared = matches.front();
    for (const auto& name : matches) {
        std::size_t n = 0;
        while (n < shared.size() && n < name.size() && shared[n] == name[n]) ++n;
        shared.resize(n);
    }
    workspace_.set(base + shared + (matches.size() == 1 ? "/" : ""));
}

// --- keys ------------------------------------------------------------------

bool Onboard::visible(Field field) const {
    if (field == Chip) return true;
    if (!wantsConnection_) return false;
    // A key makes the password pointless, so it only shows up without one.
    if (field == Password) return connectionKey_.text.empty();
    return true;
}

void Onboard::moveField(int direction) {
    int next = connectionField_;
    do {
        next += direction;
    } while (next > Chip && next < FieldCount && !visible(static_cast<Field>(next)));
    if (next < Chip) { back(); return; }
    if (next >= FieldCount) { advance(); return; }
    connectionField_ = next;
}

bool Onboard::onKey(const KeyChord& chord, const std::string& raw) {
    if (!open_) return false;

    if (capturingLeader_) {
        capturingLeader_ = false;
        if (chord.key == "escape" && chord.mods == ModNone) return true;
        if (chord.empty()) { error_ = "That key can't be the leader"; return true; }
        std::string spec = chord.spec();
        std::transform(spec.begin(), spec.end(), spec.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        customLeader_ = spec;
        leaderIndex_ = customLeaderRow();
        return true;
    }

    const bool shiftTab = chord.mods == ModShift && chord.key == "tab";
    if (chord.key == "escape" || (chord.mods == ModCtrl && chord.key == "c")) {
        finish(); // what's been answered so far is kept
        return true;
    }

    switch (step_) {
        case Step::Workspace:
            if (shiftTab) return true; // nothing before this
            if (chord.key == "enter") { advance(); return true; }
            if (chord.mods == ModNone && chord.key == "tab") { completeWorkspace(); return true; }
            workspace_.onKey(chord, raw);
            error_.clear();
            return true;

        case Step::Appearance: {
            const int count = static_cast<int>(Theme::builtinNames().size());
            if (shiftTab) { back(); return true; }
            if (chord.key == "up" || chord.key == "left" || chord.key == "k") {
                themeIndex_ = (themeIndex_ + count - 1) % count;
            } else if (chord.key == "down" || chord.key == "right" || chord.key == "j") {
                themeIndex_ = (themeIndex_ + 1) % count;
            } else if (chord.key == "enter") {
                advance();
                return true;
            } else {
                return true;
            }
            previewTheme_ = Theme::builtinNames()[static_cast<std::size_t>(themeIndex_)];
            return true;
        }

        case Step::Leader: {
            const int count = customLeaderRow() + 1;
            if (shiftTab) { back(); return true; }
            if (chord.key == "up" || chord.key == "k") {
                leaderIndex_ = (leaderIndex_ + count - 1) % count;
            } else if (chord.key == "down" || chord.key == "j") {
                leaderIndex_ = (leaderIndex_ + 1) % count;
            } else if (chord.key == "enter") {
                advance();
            } else if (chord.key == "space" && leaderIndex_ == customLeaderRow()) {
                capturingLeader_ = true;
            }
            return true;
        }

        case Step::Connection: {
            if (shiftTab || (chord.key == "up" && connectionField_ != Chip)) {
                moveField(-1);
                return true;
            }
            if ((chord.mods == ModNone && chord.key == "tab") || chord.key == "down") {
                if (connectionField_ == Chip && !wantsConnection_) { advance(); return true; }
                moveField(1);
                return true;
            }
            if (connectionField_ == Chip) {
                if (chord.key == "y") { wantsConnection_ = true; connectionField_ = Address; }
                else if (chord.key == "n") { wantsConnection_ = false; advance(); }
                else if (chord.key == "space" || chord.key == "left" || chord.key == "right") {
                    wantsConnection_ = !wantsConnection_;
                } else if (chord.key == "enter") {
                    if (wantsConnection_) connectionField_ = Address;
                    else advance();
                }
                return true;
            }
            if (chord.key == "enter") { moveField(1); return true; }
            LineEdit* field = connectionField_ == Address ? &connectionAddress_
                              : connectionField_ == Name  ? &connectionName_
                              : connectionField_ == Key   ? &connectionKey_
                                                          : &connectionPassword_;
            field->onKey(chord, raw);
            error_.clear();
            return true;
        }

        case Step::ShellIntegration:
            if (shiftTab) { back(); return true; }
            if (chord.key == "y") { shellIntegrationChoice_ = true; advance(); return true; }
            if (chord.key == "n") { shellIntegrationChoice_ = false; advance(); return true; }
            if (chord.key == "left" || chord.key == "right" || chord.key == "space") {
                shellIntegrationChoice_ = !shellIntegrationChoice_;
                return true;
            }
            if (chord.key == "enter") advance();
            return true;

        case Step::Done:
            if (shiftTab) { back(); return true; }
            if (chord.key == "enter" || chord.key == "space") finish();
            return true;
    }
    return true;
}

namespace {
// Hotspot ids. Themes, leaders, steps and fields each get a small range.
constexpr int kNext = 1;
constexpr int kBack = 2;
constexpr int kSkip = 3;
constexpr int kThemeBase = 10;
constexpr int kWantYes = 30;
constexpr int kWantNo = 31;
constexpr int kFieldBase = 40; // + the field
constexpr int kShellYes = 50;
constexpr int kShellNo = 51;
constexpr int kLeaderBase = 60;
constexpr int kStepBase = 80;
} // namespace

bool Onboard::onMouse(const Mouse& mouse) {
    if (!open_) return false;
    if (mouse.button != Mouse::Left || mouse.motion != Mouse::Pressed) return true;

    const int hit = spots_.at(mouse.x, mouse.y);
    if (hit < 0) return true;
    capturingLeader_ = false;

    if (hit == kNext) { advance(); return true; }
    if (hit == kBack) { back(); return true; }
    if (hit == kSkip) { finish(); return true; }

    // Steps already answered can be revisited from the bar along the top.
    if (hit >= kStepBase && hit < kStepBase + kSteps) {
        const auto wanted = static_cast<Step>(hit - kStepBase);
        if (static_cast<int>(wanted) < static_cast<int>(step_)) goTo(wanted);
        return true;
    }

    const auto names = Theme::builtinNames();
    if (hit >= kThemeBase && hit < kThemeBase + static_cast<int>(names.size())) {
        themeIndex_ = hit - kThemeBase;
        previewTheme_ = names[static_cast<std::size_t>(themeIndex_)];
        if (onChanged) onChanged();
        return true;
    }

    if (hit >= kLeaderBase && hit <= kLeaderBase + customLeaderRow()) {
        leaderIndex_ = hit - kLeaderBase;
        if (leaderIndex_ == customLeaderRow()) capturingLeader_ = true;
        return true;
    }

    if (hit == kWantYes) { wantsConnection_ = true; connectionField_ = Address; return true; }
    if (hit == kWantNo) { wantsConnection_ = false; connectionField_ = Chip; return true; }
    if (hit > kFieldBase && hit < kFieldBase + FieldCount) {
        connectionField_ = hit - kFieldBase;
        wantsConnection_ = true;
        return true;
    }

    if (hit == kShellYes) { shellIntegrationChoice_ = true; return true; }
    if (hit == kShellNo) { shellIntegrationChoice_ = false; return true; }
    return true;
}

// --- rendering -------------------------------------------------------------

Element Onboard::render(const Theme& theme, const DecorationSettings& decoration,
                        int width, int height) {
    spots_.clear();
    const int panelWidth = std::clamp(width - 8, 56, 80);
    const int room = panelWidth - 6;

    const auto title = [&](const std::string& text_) {
        return text(" " + text_) | bold | color(toFtx(theme.fg));
    };
    const auto note = [&](const std::string& text_) {
        return text(" " + elide(text_, room)) | color(toFtx(theme.muted));
    };
    const auto field = [&](int id, const std::string& label, const LineEdit& edit,
                           bool focused, const std::string& placeholder, bool mask = false) {
        return spots_.track(kFieldBase + id, hbox({
            text(focused ? " ▸ " : "   ") | color(toFtx(theme.accent)),
            text(label) | color(toFtx(focused ? theme.fg : theme.muted)),
            edit.render(theme, placeholder, focused, mask),
            filler(),
        }));
    };
    const auto button = [&](int id, const std::string& keys, const std::string& what) {
        return spots_.track(id, hint(keys, what, theme));
    };
    const auto chip = [&](const std::string& label, bool picked) {
        Element out = text(" " + label + " ");
        if (picked) return std::move(out) | bgcolor(toFtx(theme.accent)) | color(toFtx(theme.bg));
        return std::move(out) | color(toFtx(theme.muted));
    };
    const auto choiceRow = [&](bool chosen, Element content) {
        Element row = hbox({text(chosen ? " ▸ " : "   ") | color(toFtx(theme.accent)),
                            std::move(content), filler()});
        return chosen ? std::move(row) | bgcolor(toFtx(theme.selection)) : row;
    };

    Elements body;
    switch (step_) {
        case Step::Workspace:
            body = {
                text(" A file browser and a real terminal, side by side.") |
                    color(toFtx(theme.muted)),
                text(""),
                title("Where should Apollo open?"),
                text(""),
                field(0, "Folder  ", workspace_, true, "~"),
                text(""),
                note("Both panes start here. Tab completes; ~ is your home."),
            };
            break;

        case Step::Appearance: {
            const auto names = Theme::builtinNames();
            Elements swatches;
            for (std::size_t i = 0; i < names.size(); ++i) {
                const Theme sample = *Theme::builtin(names[i]);
                const bool chosen = static_cast<int>(i) == themeIndex_;
                swatches.push_back(spots_.track(kThemeBase + static_cast<int>(i), choiceRow(chosen, hbox({
                    text("██") | color(toFtx(sample.accent)),
                    text("██") | color(toFtx(sample.accentAlt)),
                    text("██") | color(toFtx(sample.success)),
                    text("██") | color(toFtx(sample.warning)),
                    text("██") | color(toFtx(sample.error)),
                    text("  " + names[i]) | color(toFtx(chosen ? theme.fg : theme.muted)),
                }))));
            }
            body = {
                title("Pick a look"),
                text(""),
                vbox(std::move(swatches)),
                text(""),
                note("The window behind this is already wearing it."),
            };
            break;
        }

        case Step::Leader: {
            Elements rows;
            for (std::size_t i = 0; i < leaderChoices().size(); ++i) {
                const bool chosen = static_cast<int>(i) == leaderIndex_;
                std::string name = leaderChoices()[i].title;
                name.resize(12, ' ');
                rows.push_back(spots_.track(kLeaderBase + static_cast<int>(i), choiceRow(chosen, hbox({
                    text(name) | bold | color(toFtx(chosen ? theme.accent : theme.fg)),
                    text(elide(leaderChoices()[i].detail, room - 16)) | color(toFtx(theme.muted)),
                }))));
            }
            const bool customChosen = leaderIndex_ == customLeaderRow();
            std::string customLabel = "Another key";
            customLabel.resize(12, ' ');
            const std::string customDetail =
                capturingLeader_       ? "press it now…  Esc to cancel"
                : customLeader_.empty() ? "press Space here, then the key"
                                        : KeyChord::parse(customLeader_)->describe() +
                                              "  (Space here to change it)";
            rows.push_back(spots_.track(kLeaderBase + customLeaderRow(), choiceRow(customChosen, hbox({
                text(customLabel) | bold | color(toFtx(customChosen ? theme.accent : theme.fg)),
                text(customDetail) |
                    color(toFtx(capturingLeader_ ? theme.warning : theme.muted)),
            }))));

            const auto& anywhere = config_.general().leaderAnywhere;
            const std::string anywhereNote =
                anywhere.empty() ? ""
                                 : KeyChord::describeList(anywhere) +
                                       (anywhere.size() == 1 ? " is" : " are") +
                                       " the leader everywhere, even mid-command.";
            body = {
                title("Which key goes in front of Apollo's own?"),
                text(""),
                vbox(std::move(rows)),
                text(""),
                note("Everything Apollo does is the leader, then one key: the leader"),
                note("then Space for commands, then , for settings. Every other key goes"),
                note("to your shell untouched."),
                note(anywhereNote),
                note(anywhereNote.empty() ? "" : "Change those with general.leader_anywhere."),
            };
            break;
        }

        case Step::Connection: {
            const bool active = wantsConnection_;
            body = {
                title("Add an SSH destination?"),
                text(""),
                hbox({
                    text("   "),
                    spots_.track(kWantYes, chip("yes", wantsConnection_)),
                    text(" "),
                    spots_.track(kWantNo, chip("not now", !wantsConnection_)),
                    text(connectionField_ == Chip ? "   y / n" : "") | color(toFtx(theme.muted)),
                    filler(),
                }),
                text(""),
            };
            if (active) {
                const std::string guessed = [&] {
                    const std::string address = connectionAddress_.text;
                    const auto at = address.find('@');
                    if (at == std::string::npos || at + 1 >= address.size()) return std::string("lab");
                    std::string host = address.substr(at + 1);
                    if (std::count(host.begin(), host.end(), ':') == 1) host.resize(host.find(':'));
                    return nameFor(host);
                }();
                body.push_back(field(Address, "Address  ", connectionAddress_,
                                     connectionField_ == Address, "alice@10.0.0.5 or alice@host:2222"));
                body.push_back(field(Name, "Name     ", connectionName_, connectionField_ == Name,
                                     guessed));
                body.push_back(field(Key, "Key      ", connectionKey_, connectionField_ == Key,
                                     "none: use the ssh agent or a password"));
                if (visible(Password)) {
                    body.push_back(field(Password, "Password ", connectionPassword_,
                                         connectionField_ == Password,
                                         "optional; needs sshpass, a key is safer", true));
                }
                body.push_back(text(""));
                body.push_back(note("`apollo connect` opens it. " +
                                    config_.general().leader.describe() +
                                    " A and `apollo push`/`pull` copy files"));
                body.push_back(note("over the same connection."));
            } else {
                body.push_back(note("`apollo connect` opens a remote shell and the browser follows"));
                body.push_back(note("it there; files move across with " +
                                    config_.general().leader.describe() +
                                    " A or apollo push/pull."));
                body.push_back(note("Add destinations any time from the Connections page of"));
                body.push_back(note("`apollo config`."));
            }
            break;
        }

        case Step::ShellIntegration:
            body = {
                title("Let the browser follow your shell?"),
                text(""),
                note("One line in your shell's startup file sources a small snippet that"),
                note("reports the directory and marks each prompt. The browser then"),
                note("follows every cd, Leader+↑ jumps between commands, and a Space"),
                note("leader knows exactly when the prompt is empty."),
                text(""),
                hbox({
                    text("   "),
                    spots_.track(kShellYes, chip("add it", shellIntegrationChoice_)),
                    text(" "),
                    spots_.track(kShellNo, chip("skip", !shellIntegrationChoice_)),
                    filler(),
                }),
                text(""),
                note(shellRcFile().empty()
                         ? "Your shell wasn't recognized, so the snippet is only written out."
                         : "It goes in " + paths::contractUser(shellRcFile()) +
                               ". Delete the line to undo it."),
            };
            break;

        case Step::Done: {
            const auto row = [&](const std::string& label, const std::string& value) {
                std::string padded = label;
                padded.resize(10, ' ');
                return hbox({text("   " + padded) | color(toFtx(theme.muted)),
                             text(elide(value, room - 12)) | color(toFtx(theme.fg))});
            };
            const std::string leader = config_.general().leader.describe();
            const auto keyRow = [&](const std::string& keys, const std::string& what) {
                return hbox({text("   "), hint(keys, what, theme)});
            };

            body = {
                title("All set"),
                text(""),
                row("Opens in", config_.general().workspace),
                row("Theme", config_.decoration().theme),
                row("Leader", leader),
                row("SSH", savedConnection_.empty() ? "none yet" : savedConnection_),
                row("Shell", shellIntegrationResult_.empty() ? "not changed"
                                                             : shellIntegrationResult_),
                text(""),
                keyRow(leader + " Space", "every command"),
                keyRow(leader + " ,", "settings, including all of the above"),
                keyRow("F1", "every key"),
                text(""),
                note("Saved to " + paths::contractUser(config_.path()) + "."),
            };
            break;
        }
    }

    if (!error_.empty()) {
        body.push_back(text(""));
        body.push_back(text(" " + error_) | color(toFtx(theme.error)));
    }

    // --- the step bar ---
    static const char* stepNames[kSteps] = {"Folder", "Look", "Leader", "SSH", "Shell"};
    Elements bar{text(" apollo setup ") | bold | color(toFtx(theme.accent)), filler()};
    for (int i = 0; i < kSteps; ++i) {
        const int here = static_cast<int>(step_);
        const bool done = i < here;
        const bool current = i == here;
        Element label = text(std::string(done ? "✓ " : current ? "● " : "○ ") + stepNames[i]);
        label = std::move(label) | color(toFtx(done      ? theme.success
                                               : current ? theme.accent
                                                         : theme.muted));
        if (current) label = std::move(label) | bold;
        bar.push_back(spots_.track(kStepBase + i, std::move(label)));
        bar.push_back(text("  "));
    }

    // --- the footer: the same three keys on every step ---
    Elements footer{text(" ")};
    if (step_ == Step::Done) {
        footer.push_back(button(kNext, "Enter", "start"));
        footer.push_back(text("   "));
        footer.push_back(button(kBack, "Shift+Tab", "back"));
    } else {
        footer.push_back(button(kNext, "Enter", step_ == Step::ShellIntegration ? "finish" : "next"));
        if (step_ != Step::Workspace) {
            footer.push_back(text("   "));
            footer.push_back(button(kBack, "Shift+Tab", "back"));
        }
        footer.push_back(text("   "));
        footer.push_back(button(kSkip, "Esc", "skip the rest"));
    }
    footer.push_back(filler());

    Element content = vbox({
        hbox(std::move(bar)),
        separator() | color(toFtx(theme.border)),
        text(""),
        vbox(std::move(body)) | flex,
        separator() | color(toFtx(theme.border)),
        hbox(std::move(footer)),
    });

    // One size for every step, so the box doesn't jump as you go.
    const int panelHeight = std::max(12, std::min(height - 4, 19));
    return modal(std::move(content) | size(HEIGHT, EQUAL, panelHeight), theme, decoration,
                 panelWidth, height - 2);
}

} // namespace apollo::ui
