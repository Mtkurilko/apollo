// The command palette.
//
// One list, fuzzy filtered, over everything Apollo can do: built-in commands,
// commands from the config, executables in ~/.apollo/commands, actions, themes
// and SSH destinations. It is the answer to "what can this thing do" for anyone
// who has not read the config file.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>

#include "core/Config.h"
#include "ui/Widgets.h"

namespace apollo::ui {

class Palette {
public:
    struct Item {
        std::string title;
        std::string subtitle;
        std::string group;   // "command", "action", "connect", "theme"
        std::string keyHint; // the bind that also does this, when there is one
        std::function<void()> run;
    };

    void open(std::vector<Item> items, const std::string& prompt = "Run");
    void close();
    bool isOpen() const { return open_; }

    // Returns true when the palette consumed the key.
    bool onKey(const KeyChord& chord, const std::string& raw);

    ftxui::Element render(const Theme& theme,
                          const DecorationSettings& decoration,
                          int width,
                          int height);

private:
    void refilter();

    bool open_ = false;
    std::string prompt_ = "Run";
    LineEdit query_;
    std::vector<Item> items_;

    struct Filtered {
        std::size_t index = 0;
        int score = 0;
        std::vector<int> positions;
    };
    std::vector<Filtered> shown_;
    int selected_ = 0;
    int scroll_ = 0;
};

} // namespace apollo::ui
