// First run.
//
// Six screens, none of them required beyond the first two, and every answer is
// something `apollo config` can change afterwards. The point is to leave a
// working, commented config behind rather than an empty one.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>

#include "core/Config.h"
#include "ui/Widgets.h"

namespace apollo::ui {

class Onboard {
public:
    explicit Onboard(Config& config) : config_(config) {}

    void start();
    bool isOpen() const { return open_; }
    bool onKey(const KeyChord& chord, const std::string& raw);
    ftxui::Element render(const Theme& theme, const DecorationSettings& decoration,
                          int width, int height);

    // The theme highlighted right now, so the whole interface can preview it
    // while the user is choosing.
    const std::string& previewTheme() const { return previewTheme_; }
    // Called when the wizard writes the config, and again when it finishes.
    std::function<void()> onChanged;
    std::function<void()> onFinished;

private:
    enum class Step { Welcome, Workspace, Appearance, Connection, ShellIntegration, Done };

    void advance();
    void back();
    void finish();
    bool writeShellIntegration(std::string& where);

    Config& config_;
    bool open_ = false;
    Step step_ = Step::Welcome;

    LineEdit workspace_;
    int themeIndex_ = 0;
    std::string previewTheme_;

    // Connection, all optional.
    int connectionField_ = 0;
    LineEdit connectionName_;
    LineEdit connectionAddress_;
    LineEdit connectionKey_;
    bool wantsConnection_ = false;

    bool shellIntegrationChoice_ = true;
    std::string shellIntegrationResult_;
    std::string error_;
};

} // namespace apollo::ui
