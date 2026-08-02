#include "ui/Palette.h"

#include <algorithm>

#include "core/Commands.h"

namespace apollo::ui {

using namespace ftxui;

void Palette::open(std::vector<Item> items, const std::string& prompt) {
    items_ = std::move(items);
    prompt_ = prompt;
    query_.clear();
    selected_ = 0;
    scroll_ = 0;
    open_ = true;
    refilter();
}

void Palette::close() {
    open_ = false;
    items_.clear();
    shown_.clear();
    query_.clear();
}

void Palette::refilter() {
    shown_.clear();
    for (std::size_t i = 0; i < items_.size(); ++i) {
        Filtered entry;
        entry.index = i;

        if (query_.text.empty()) {
            shown_.push_back(std::move(entry));
            continue;
        }
        if (const auto onTitle = fuzzy::score(items_[i].title, query_.text, &entry.positions)) {
            entry.score = *onTitle + 30;
            shown_.push_back(std::move(entry));
            continue;
        }
        if (const auto onSubtitle = fuzzy::score(items_[i].subtitle, query_.text)) {
            entry.score = *onSubtitle;
            entry.positions.clear();
            shown_.push_back(std::move(entry));
        }
    }

    if (!query_.text.empty()) {
        std::stable_sort(shown_.begin(), shown_.end(),
                         [](const Filtered& a, const Filtered& b) { return a.score > b.score; });
    }
    selected_ = std::clamp(selected_, 0, std::max(0, static_cast<int>(shown_.size()) - 1));
}

bool Palette::onKey(const KeyChord& chord, const std::string& raw) {
    if (!open_) return false;

    if (chord.key == "escape" || (chord.mods == ModCtrl && chord.key == "g")) {
        close();
        return true;
    }
    if (chord.key == "enter") {
        if (selected_ >= 0 && selected_ < static_cast<int>(shown_.size())) {
            // Copy the action before closing: close() drops the items, and the
            // action itself may well reopen the palette.
            auto action = items_[shown_[static_cast<std::size_t>(selected_)].index].run;
            close();
            if (action) action();
        } else {
            close();
        }
        return true;
    }
    if (chord.key == "up" || (chord.mods == ModCtrl && chord.key == "p")) {
        if (!shown_.empty()) selected_ = (selected_ + static_cast<int>(shown_.size()) - 1) %
                                         static_cast<int>(shown_.size());
        return true;
    }
    if (chord.mods == ModShift && chord.key == "tab") {
        if (!shown_.empty()) selected_ = (selected_ + static_cast<int>(shown_.size()) - 1) %
                                         static_cast<int>(shown_.size());
        return true;
    }
    if (chord.key == "down" || chord.key == "tab" || (chord.mods == ModCtrl && chord.key == "n")) {
        if (!shown_.empty()) selected_ = (selected_ + 1) % static_cast<int>(shown_.size());
        return true;
    }
    if (chord.key == "pageup") { selected_ = std::max(0, selected_ - 8); return true; }
    if (chord.key == "pagedown") {
        selected_ = std::min(static_cast<int>(shown_.size()) - 1, selected_ + 8);
        return true;
    }

    const std::string before = query_.text;
    if (query_.onKey(chord, raw)) {
        if (query_.text != before) { selected_ = 0; refilter(); }
        return true;
    }
    // Swallow everything else: an open palette must never leak a keystroke
    // into the terminal underneath it.
    return true;
}

Element Palette::render(const Theme& theme, const DecorationSettings& decoration,
                        int width, int height) {
    const int paletteWidth = std::clamp(width - 8, 40, 88);
    const int listHeight = std::clamp(height - 10, 4, 16);

    if (selected_ < scroll_) scroll_ = selected_;
    if (selected_ >= scroll_ + listHeight) scroll_ = selected_ - listHeight + 1;
    scroll_ = std::clamp(scroll_, 0,
                         std::max(0, static_cast<int>(shown_.size()) - listHeight));

    Elements rows;
    for (int i = scroll_; i < std::min(scroll_ + listHeight, static_cast<int>(shown_.size())); ++i) {
        const Filtered& entry = shown_[static_cast<std::size_t>(i)];
        const Item& item = items_[entry.index];
        const bool isSelected = i == selected_;

        Elements cells;
        cells.push_back(text(isSelected ? " ▸ " : "   ") | color(toFtx(theme.accent)));
        cells.push_back(highlighted(item.title, entry.positions, theme, isSelected));
        if (!item.subtitle.empty()) {
            cells.push_back(text("  "));
            cells.push_back(text(elide(item.subtitle, paletteWidth / 2)) |
                            color(toFtx(theme.muted)));
        }
        cells.push_back(filler());
        if (!item.keyHint.empty()) {
            cells.push_back(text(item.keyHint + " ") | color(toFtx(theme.muted)));
        }
        if (!item.group.empty()) {
            cells.push_back(text(" " + item.group + " ") | color(toFtx(theme.muted)) |
                            bgcolor(toFtx(theme.bg)));
        }

        Element row = hbox(std::move(cells));
        if (isSelected) row = std::move(row) | bgcolor(toFtx(theme.selection));
        rows.push_back(std::move(row));
    }

    if (rows.empty()) {
        rows.push_back(hbox({text("   no match") | color(toFtx(theme.muted))}));
    }
    while (static_cast<int>(rows.size()) < listHeight) rows.push_back(text(""));

    Element body = vbox({
        hbox({
            text(" " + prompt_ + " ▸ ") | color(toFtx(theme.accent)) | bold,
            query_.render(theme, "type to filter", true),
            filler(),
            text(std::to_string(shown_.size()) + "/" + std::to_string(items_.size()) + " ") |
                color(toFtx(theme.muted)),
        }),
        separator() | color(toFtx(theme.border)),
        vbox(std::move(rows)),
        separator() | color(toFtx(theme.border)),
        hbox({
            text(" "),
            hint("↑↓", "move", theme),
            text("   "),
            hint("Enter", "run", theme),
            text("   "),
            hint("Esc", "close", theme),
        }),
    });

    return modal(std::move(body), theme, decoration, paletteWidth, height - 4);
}

} // namespace apollo::ui
