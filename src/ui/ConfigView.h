// `apollo config` with nothing after it.
//
// Every setting Apollo has, in one screen, each with what it does and what it
// accepts. Changes are written to ~/.apollo/apollo.conf as they are made and
// take effect immediately — switch the theme and the screen restyles under
// you — and the file keeps its comments and layout throughout.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>

#include "core/Config.h"
#include "ui/Widgets.h"

namespace apollo::ui {

class ConfigView {
public:
    explicit ConfigView(Config& config) : config_(config) {}

    void open();
    void close();
    bool isOpen() const { return open_; }

    bool onKey(const KeyChord& chord, const std::string& raw);
    ftxui::Element render(const Theme& theme,
                          const DecorationSettings& decoration,
                          int width,
                          int height);

    // Fired after anything is written, so the app can pick up a new theme or
    // re-derive the command table.
    std::function<void()> onChanged;

private:
    enum class Page { General, Appearance, Terminal, Browser, Keys, Commands, Connections, Problems, About };

    struct Field {
        std::string label;
        std::string placeholder;
        bool mask = false;
        bool optional = false;
    };

    std::vector<Page> pages() const;
    static std::string pageName(Page page);
    std::vector<const Config::Setting*> settingsFor(Page page) const;
    int rowCount() const;

    void beginEdit();
    void commitEdit();
    void cancelEdit();
    void cycleChoice(int direction);
    void toggleBool();
    void applyChange(const std::string& path, const std::string& value);
    void remove();
    void beginAdd();
    void advancePrompt();

    ftxui::Element renderSettings(Page page, const Theme& theme, int width, int height);
    ftxui::Element renderKeys(const Theme& theme, int height);
    ftxui::Element renderCommands(const Theme& theme, int height);
    ftxui::Element renderConnections(const Theme& theme, int height);
    ftxui::Element renderProblems(const Theme& theme, int height);
    ftxui::Element renderAbout(const Theme& theme);
    ftxui::Element renderPrompt(const Theme& theme);

    Config& config_;
    bool open_ = false;
    int page_ = 0;
    int row_ = 0;
    int scroll_ = 0;

    bool editing_ = false;
    LineEdit editor_;
    std::string editingPath_;
    std::string error_;
    std::string flash_;

    // A short sequence of questions, used for adding a connection.
    struct Prompt {
        std::string title;
        std::vector<Field> fields;
        std::vector<std::string> answers;
        std::size_t at = 0;
        std::function<void(const std::vector<std::string>&)> finish;
    };
    bool prompting_ = false;
    Prompt prompt_;
    LineEdit promptInput_;
};

} // namespace apollo::ui
