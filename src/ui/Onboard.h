// First run.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <ftxui/component/mouse.hpp>
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
    bool onMouse(const ftxui::Mouse& mouse);
    ftxui::Element render(const Theme& theme, const DecorationSettings& decoration,
                          int width, int height);

    const std::string& previewTheme() const { return previewTheme_; }
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

    // 0 is the yes/no chip; the rest are the fields below it.
    static constexpr int kConnectionFields = 6;
    int connectionField_ = 0;
    LineEdit connectionName_;
    LineEdit connectionAddress_;
    LineEdit connectionPort_;
    LineEdit connectionKey_;
    LineEdit connectionPassword_;
    bool wantsConnection_ = false;

    Hotspots spots_;

    bool shellIntegrationChoice_ = true;
    std::string shellIntegrationResult_;
    std::string error_;
};

} // namespace apollo::ui
