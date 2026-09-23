// First run wizard. Five short questions, each one skippable, every answer
// written to apollo.conf as you go.
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

    // Rewrites ~/.apollo/shell-integration.sh if it's there and out of date,
    // so a newer Apollo's marks reach shells set up by an older one.
    static void refreshShellIntegration();

private:
    enum class Step { Workspace, Appearance, Leader, Connection, ShellIntegration, Done };
    static constexpr int kSteps = 5; // Done isn't a question

    void advance();
    void back();
    void goTo(Step step);
    void finish();
    bool writeShellIntegration(std::string& where);
    void completeWorkspace();
    bool saveConnection();

    Config& config_;
    bool open_ = false;
    Step step_ = Step::Workspace;

    LineEdit workspace_;
    int themeIndex_ = 0;
    std::string previewTheme_;

    // The leader choices, then "press a key" at the end.
    int leaderIndex_ = 0;
    bool capturingLeader_ = false;
    std::string customLeader_; // what "press a key" caught, as a spec

    // 0 is the yes/no chip, then address, name, key and password.
    enum Field { Chip, Address, Name, Key, Password, FieldCount };
    int connectionField_ = Chip;
    bool wantsConnection_ = false;
    LineEdit connectionAddress_;
    LineEdit connectionName_;
    LineEdit connectionKey_;
    LineEdit connectionPassword_;
    std::string savedConnection_;
    bool visible(Field field) const;
    void moveField(int direction);

    Hotspots spots_;

    bool shellIntegrationChoice_ = true;
    std::string shellIntegrationResult_;
    std::string error_;
};

} // namespace apollo::ui
