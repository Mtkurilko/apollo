#include "ui/Onboard.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "core/ConfigFile.h"
#include "core/Paths.h"
#include "core/Process.h"

namespace fs = std::filesystem;

namespace apollo::ui {

using namespace ftxui;

namespace {

const char* kShellIntegration = R"(# Apollo shell integration.
# Reports the working directory (OSC 7) and marks prompts (OSC 133) so Apollo's
# file browser follows along and Leader+Up/Down can jump between commands.
# Harmless in any other terminal.

__apollo_osc7() { printf '\033]7;file://%s%s\033\\' "${HOSTNAME:-$(hostname)}" "$PWD"; }
__apollo_mark() { printf '\033]133;A\033\\'; }

if [ -n "$ZSH_VERSION" ]; then
    precmd_functions+=(__apollo_osc7 __apollo_mark)
elif [ -n "$BASH_VERSION" ]; then
    PROMPT_COMMAND="__apollo_osc7; __apollo_mark${PROMPT_COMMAND:+; $PROMPT_COMMAND}"
fi
)";

std::string shellRcFile() {
    const std::string shell = process::userShell();
    if (shell.find("zsh") != std::string::npos) return (paths::home() / ".zshrc").string();
    if (shell.find("bash") != std::string::npos) return (paths::home() / ".bashrc").string();
    return "";
}

} // namespace

void Onboard::start() {
    open_ = true;
    step_ = Step::Welcome;
    error_.clear();

    workspace_.set(config_.general().workspace);

    const auto names = Theme::builtinNames();
    const auto at = std::find(names.begin(), names.end(), config_.decoration().theme);
    themeIndex_ = at == names.end() ? 0 : static_cast<int>(at - names.begin());
    previewTheme_ = names[static_cast<std::size_t>(themeIndex_)];
}

void Onboard::back() {
    switch (step_) {
        case Step::Workspace:        step_ = Step::Welcome; break;
        case Step::Appearance:       step_ = Step::Workspace; break;
        case Step::Connection:       step_ = Step::Appearance; break;
        case Step::ShellIntegration: step_ = Step::Connection; break;
        case Step::Done:             step_ = Step::ShellIntegration; break;
        case Step::Welcome:          break;
    }
    error_.clear();
}

void Onboard::advance() {
    error_.clear();
    switch (step_) {
        case Step::Welcome:
            step_ = Step::Workspace;
            break;

        case Step::Workspace: {
            const std::string wanted = workspace_.text.empty() ? "~" : workspace_.text;
            std::error_code ec;
            if (!fs::is_directory(paths::expandUser(wanted), ec)) {
                error_ = "No such directory: " + wanted;
                return;
            }
            config_.set("general.workspace", wanted);
            step_ = Step::Appearance;
            break;
        }

        case Step::Appearance:
            config_.set("decoration.theme", previewTheme_);
            step_ = Step::Connection;
            break;

        case Step::Connection: {
            if (wantsConnection_ && connectionName_.text.empty() &&
                !connectionAddress_.text.empty()) {
                error_ = "Give the destination a short name, or press n to skip";
                connectionField_ = 1;
                return;
            }
            if (wantsConnection_ && !connectionName_.text.empty()) {
                Connection conn;
                conn.name = connectionName_.text;
                const std::string address = connectionAddress_.text;
                const auto at = address.find('@');
                if (at == std::string::npos || at == 0 || at + 1 >= address.size()) {
                    error_ = "Expected user@host";
                    return;
                }
                conn.user = address.substr(0, at);
                conn.host = address.substr(at + 1);
                if (!connectionPort_.text.empty()) {
                    conn.port = ConfigFile::asInt(connectionPort_.text, 22);
                    if (conn.port < 1 || conn.port > 65535) {
                        error_ = "A port is a number between 1 and 65535";
                        connectionField_ = 3;
                        return;
                    }
                }
                conn.keyPath = connectionKey_.text;
                // Only one of the two: a key makes the password dead weight.
                if (conn.keyPath.empty()) conn.password = connectionPassword_.text;

                std::string problem;
                if (!config_.addConnection(conn, &problem)) { error_ = problem; return; }
            }
            step_ = Step::ShellIntegration;
            break;
        }

        case Step::ShellIntegration:
            if (shellIntegrationChoice_) {
                std::string where;
                if (writeShellIntegration(where)) shellIntegrationResult_ = where;
                else error_ = where;
            }
            step_ = Step::Done;
            break;

        case Step::Done:
            finish();
            break;
    }

    std::string problem;
    if (!config_.save(&problem)) error_ = problem;
    if (onChanged) onChanged();
}

bool Onboard::writeShellIntegration(std::string& where) {
    if (!paths::ensureDir(paths::configDir(), &where)) return false;

    const fs::path script = paths::configDir() / "shell-integration.sh";
    {
        std::ofstream out(script, std::ios::trunc);
        if (!out) { where = "cannot write " + script.string(); return false; }
        out << kShellIntegration;
    }

    const std::string rc = shellRcFile();
    if (rc.empty()) {
        where = "wrote " + paths::contractUser(script) + " — source it from your shell";
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
    open_ = false;
    paths::markSetupDone();
    if (onFinished) onFinished();
}

bool Onboard::onKey(const KeyChord& chord, const std::string& raw) {
    if (!open_) return false;

    if (chord.key == "escape") {
        std::string problem;
        config_.save(&problem);
        finish();
        return true;
    }
    if (chord.mods == ModCtrl && chord.key == "c") { finish(); return true; }

    switch (step_) {
        case Step::Welcome:
            if (chord.key == "enter") advance();
            return true;

        case Step::Workspace:
            if (chord.key == "enter") { advance(); return true; }
            if (chord.key == "up") { back(); return true; }
            workspace_.onKey(chord, raw);
            return true;

        case Step::Appearance: {
            const auto names = Theme::builtinNames();
            if (chord.key == "up" || chord.key == "left") {
                themeIndex_ = (themeIndex_ + static_cast<int>(names.size()) - 1) %
                              static_cast<int>(names.size());
                previewTheme_ = names[static_cast<std::size_t>(themeIndex_)];
                return true;
            }
            if (chord.key == "down" || chord.key == "right") {
                themeIndex_ = (themeIndex_ + 1) % static_cast<int>(names.size());
                previewTheme_ = names[static_cast<std::size_t>(themeIndex_)];
                return true;
            }
            if (chord.key == "enter") advance();
            return true;
        }

        case Step::Connection: {
            if (chord.key == "tab" || chord.key == "down") {
                connectionField_ = (connectionField_ + 1) % kConnectionFields;
                return true;
            }
            if (chord.key == "up") {
                connectionField_ =
                    (connectionField_ + kConnectionFields - 1) % kConnectionFields;
                return true;
            }
            if (connectionField_ == 0) {
                if (chord.key == "y") { wantsConnection_ = true; connectionField_ = 1; return true; }
                if (chord.key == "n") { wantsConnection_ = false; advance(); return true; }
                if (chord.key == "space") { wantsConnection_ = !wantsConnection_; return true; }
                if (chord.key == "enter") {
                    if (wantsConnection_) connectionField_ = 1;
                    else advance();
                    return true;
                }
                return true;
            }
            if (chord.key == "enter") {
                // A key means the password below it will not be used, so
                // Enter on the key field finishes the step.
                if (connectionField_ == 4 && !connectionKey_.text.empty()) { advance(); return true; }
                if (connectionField_ < kConnectionFields - 1) ++connectionField_;
                else advance();
                return true;
            }
            LineEdit* field = connectionField_ == 1   ? &connectionName_
                              : connectionField_ == 2 ? &connectionAddress_
                              : connectionField_ == 3 ? &connectionPort_
                              : connectionField_ == 4 ? &connectionKey_
                                                      : &connectionPassword_;
            if (field->onKey(chord, raw)) wantsConnection_ = true;
            return true;
        }

        case Step::ShellIntegration:
            if (chord.key == "y") { shellIntegrationChoice_ = true; advance(); return true; }
            if (chord.key == "n") { shellIntegrationChoice_ = false; advance(); return true; }
            if (chord.key == "left" || chord.key == "right" || chord.key == "space") {
                shellIntegrationChoice_ = !shellIntegrationChoice_;
                return true;
            }
            if (chord.key == "enter") advance();
            if (chord.key == "up") back();
            return true;

        case Step::Done:
            if (chord.key == "enter" || chord.key == "space") finish();
            return true;
    }
    return true;
}

namespace {
// Hotspot ids. Themes and connection fields take a small range each.
constexpr int kNext = 1;
constexpr int kBack = 2;
constexpr int kSkip = 3;
constexpr int kThemeBase = 10;
constexpr int kWantYes = 30;
constexpr int kWantNo = 31;
constexpr int kFieldBase = 40; // + the field number, 1 to 5
constexpr int kShellYes = 50;
constexpr int kShellNo = 51;
} // namespace

bool Onboard::onMouse(const Mouse& mouse) {
    if (!open_) return false;
    if (mouse.button != Mouse::Left || mouse.motion != Mouse::Pressed) return true;

    const int hit = spots_.at(mouse.x, mouse.y);
    if (hit < 0) return true;

    if (hit == kNext) { advance(); return true; }
    if (hit == kBack) { back(); return true; }
    if (hit == kSkip) {
        std::string problem;
        config_.save(&problem);
        finish();
        return true;
    }

    const auto names = Theme::builtinNames();
    if (hit >= kThemeBase && hit < kThemeBase + static_cast<int>(names.size())) {
        themeIndex_ = hit - kThemeBase;
        previewTheme_ = names[static_cast<std::size_t>(themeIndex_)];
        if (onChanged) onChanged();
        return true;
    }

    if (hit == kWantYes) { wantsConnection_ = true; connectionField_ = 1; return true; }
    if (hit == kWantNo) { wantsConnection_ = false; connectionField_ = 0; return true; }
    if (hit > kFieldBase && hit <= kFieldBase + kConnectionFields - 1) {
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
    const int panelWidth = std::clamp(width - 8, 52, 78);

    const auto title = [&](const std::string& text_) {
        return hbox({text(" " + text_) | bold | color(toFtx(theme.accent))});
    };
    const auto note = [&](const std::string& text_) {
        return text("  " + text_) | color(toFtx(theme.muted));
    };
    const auto field = [&](const std::string& label, const LineEdit& edit, bool focused,
                           const std::string& placeholder, bool mask = false) {
        return hbox({
            text("  " + label + " ") | color(toFtx(focused ? theme.fg : theme.muted)),
            edit.render(theme, placeholder, focused, mask),
            filler(),
        });
    };
    const auto button = [&](int id, const std::string& keys, const std::string& what) {
        return spots_.track(id, hint(keys, what, theme));
    };
    const auto choiceChip = [&](const std::string& label, bool picked) {
        Element chip = text(label);
        if (picked) chip = std::move(chip) | bgcolor(toFtx(theme.accent)) | color(toFtx(theme.bg));
        else chip = std::move(chip) | color(toFtx(theme.muted));
        return chip;
    };

    Elements body;
    Elements footer;
    int stepNumber = 0;

    switch (step_) {
        case Step::Welcome:
            body = {
                text(""),
                hbox({text("  Apollo") | bold | color(toFtx(theme.accent)),
                      text("  " APOLLO_VERSION) | color(toFtx(theme.muted))}),
                text(""),
                text("  A file browser and a real terminal, side by side.") |
                    color(toFtx(theme.fg)),
                text(""),
                note("Four questions. All of it can be changed later with"),
                note("`apollo config`, and Esc skips the rest."),
                text(""),
            };
            footer = {button(kNext, "Enter", "begin"), text("   "), button(kSkip, "Esc", "skip")};
            break;

        case Step::Workspace:
            stepNumber = 1;
            body = {
                title("Where should Apollo open?"),
                text(""),
                field("Directory", workspace_, true, "~"),
                text(""),
                note("Both panes start here. ~ expands to your home directory."),
            };
            footer = {button(kNext, "Enter", "continue"), text("   "), button(kBack, "↑", "back")};
            break;

        case Step::Appearance: {
            stepNumber = 2;
            const auto names = Theme::builtinNames();
            Elements swatches;
            for (std::size_t i = 0; i < names.size(); ++i) {
                const Theme sample = *Theme::builtin(names[i]);
                const bool chosen = static_cast<int>(i) == themeIndex_;
                swatches.push_back(spots_.track(kThemeBase + static_cast<int>(i), hbox({
                    text(chosen ? "  ▸ " : "    ") | color(toFtx(theme.accent)),
                    text("██") | color(toFtx(sample.accent)),
                    text("██") | color(toFtx(sample.accentAlt)),
                    text("██") | color(toFtx(sample.success)),
                    text("██") | color(toFtx(sample.warning)),
                    text("██") | color(toFtx(sample.error)),
                    text("  " + names[i]) |
                        color(toFtx(chosen ? theme.fg : theme.muted)),
                    filler(),
                })));
            }
            body = {
                title("Pick a look"),
                text(""),
                vbox(std::move(swatches)),
                text(""),
                note("The window is already showing the highlighted one."),
            };
            footer = {hint("↑↓", "preview", theme), text("   "),
                      button(kNext, "Enter", "keep it"), text("   "),
                      button(kBack, "↑", "back")};
            break;
        }

        case Step::Connection:
            stepNumber = 3;
            body = {
                title("Add an SSH destination?"),
                text(""),
                hbox({
                    text("  "),
                    spots_.track(kWantYes, choiceChip(" yes ", wantsConnection_)),
                    text("  "),
                    spots_.track(kWantNo, choiceChip(" no ", !wantsConnection_)),
                    text("   "),
                    text(connectionField_ == 0 ? "◂ y / n" : "") | color(toFtx(theme.muted)),
                }),
                text(""),
                spots_.track(kFieldBase + 1,
                             field("Name    ", connectionName_,
                                   wantsConnection_ && connectionField_ == 1, "lab")),
                spots_.track(kFieldBase + 2,
                             field("Address ", connectionAddress_,
                                   wantsConnection_ && connectionField_ == 2, "alice@10.0.0.5")),
                spots_.track(kFieldBase + 3,
                             field("Port    ", connectionPort_,
                                   wantsConnection_ && connectionField_ == 3, "22 (optional)")),
                spots_.track(kFieldBase + 4,
                             field("Key     ", connectionKey_,
                                   wantsConnection_ && connectionField_ == 4,
                                   "~/.ssh/id_ed25519 (optional)")),
                spots_.track(kFieldBase + 5,
                             field("Password", connectionPassword_,
                                   wantsConnection_ && connectionField_ == 5,
                                   connectionKey_.text.empty() ? "only if you have no key"
                                                               : "not needed, you gave a key",
                                   true)),
                text(""),
                note("`apollo connect` uses this. Add more later with `apollo config`."),
                note("Prefer a key: a password has to go through sshpass, and is"),
                note("stored in ~/.apollo/apollo.conf, which is yours alone to read."),
            };
            footer = {hint("Tab", "next field", theme), text("   "),
                      button(kNext, "Enter", "continue"), text("   "),
                      button(kBack, "↑", "back")};
            break;

        case Step::ShellIntegration:
            stepNumber = 4;
            body = {
                title("Let the browser follow your shell?"),
                text(""),
                note("Apollo can add one line to your shell's startup file. It sources"),
                note("a snippet that reports the working directory and marks prompts, so"),
                note("the browser follows every cd and Leader+↑ jumps between commands."),
                note("Plain OSC 7 and 133 — harmless elsewhere, removed by deleting the line."),
                text(""),
                hbox({
                    text("  "),
                    spots_.track(kShellYes, choiceChip(" add it ", shellIntegrationChoice_)),
                    text("   "),
                    spots_.track(kShellNo, choiceChip(" skip ", !shellIntegrationChoice_)),
                }),
                text(""),
                note(shellRcFile().empty()
                         ? "Your shell was not recognised; the snippet will just be written out."
                         : "It would go in " + paths::contractUser(shellRcFile()) + "."),
            };
            footer = {hint("y / n", "choose", theme), text("   "),
                      button(kNext, "Enter", "continue"), text("   "),
                      button(kBack, "↑", "back")};
            break;

        case Step::Done:
            stepNumber = 5;
            const int room = std::max(20, panelWidth - 14);
            const auto row = [&](const std::string& label, const std::string& value) {
                return hbox({text("  " + label + "  ") | color(toFtx(theme.muted)),
                             text(elide(value, room))});
            };
            const auto keyRow = [&](const std::string& keys, const std::string& what) {
                return hbox({text("  "), hint(keys, what, theme)});
            };

            body = {
                title("Ready"),
                text(""),
                row("Config ", paths::contractUser(config_.path())),
                row("Opens  ", config_.general().workspace),
                row("Theme  ", config_.decoration().theme),
                shellIntegrationResult_.empty() ? text("")
                                                : row("Shell  ", shellIntegrationResult_),
                text(""),
                keyRow(config_.general().leader.describe() + " then Space", "the command palette"),
                keyRow("F1", "the key reference"),
                keyRow(config_.general().leader.describe() + " then ,", "these settings again"),
                text(""),
            };
            footer = {button(kNext, "Enter", "start")};
            break;
    }

    if (!error_.empty()) {
        body.push_back(text("  " + error_) | color(toFtx(theme.error)));
    }

    Elements header;
    header.push_back(text(" apollo setup ") | bold | color(toFtx(theme.accent)));
    header.push_back(filler());
    if (stepNumber > 0) {
        header.push_back(text("step " + std::to_string(stepNumber) + " of 5 ") |
                         color(toFtx(theme.muted)));
    }

    Elements footerRow{text(" ")};
    for (auto& piece : footer) footerRow.push_back(std::move(piece));
    footerRow.push_back(filler());

    Element content = vbox({
        hbox(std::move(header)),
        separator() | color(toFtx(theme.border)),
        vbox(std::move(body)) | flex,
        separator() | color(toFtx(theme.border)),
        hbox(std::move(footerRow)),
    });

    return modal(std::move(content), theme, decoration, panelWidth, height - 2);
}

} // namespace apollo::ui
