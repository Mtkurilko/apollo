// `apollo config` with no arguments. Writes apollo.conf as you change things.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <ftxui/component/mouse.hpp>
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
    bool onMouse(const ftxui::Mouse& mouse);
    ftxui::Element render(const Theme& theme,
                          const DecorationSettings& decoration,
                          int width,
                          int height);

    std::function<void()> onChanged;
    std::function<void()> onEditExternally;

private:
    enum class Page { General, Appearance, Terminal, Browser, Keys, Commands, Connections,
                      Openers, Problems, About };

    struct Field {
        std::string label;
        std::string placeholder;
        bool mask = false;
        bool optional = false;
        // Gets the answers so far. Return true to skip this field.
        std::function<bool(const std::vector<std::string>&)> skip;
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
    ftxui::Element renderOpeners(const Theme& theme, int height);
    ftxui::Element renderProblems(const Theme& theme, int height);
    ftxui::Element renderAbout(const Theme& theme);
    ftxui::Element renderPrompt(const Theme& theme);

    Config& config_;
    bool open_ = false;
    int page_ = 0;
    int row_ = 0;
    int scroll_ = 0;

    Hotspots spots_;

    bool editing_ = false;
    LineEdit editor_;
    std::string editingPath_;
    std::string error_;
    std::string flash_;

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
