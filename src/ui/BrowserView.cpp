#include "ui/BrowserView.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <system_error>

#include "core/Paths.h"
#include "core/Process.h"
#include "ui/Widgets.h"

namespace fs = std::filesystem;

namespace apollo::ui {

using namespace ftxui;

namespace {

std::string humanSize(std::uintmax_t bytes) {
    static const char* units[] = {"B", "K", "M", "G", "T"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) { value /= 1024.0; ++unit; }

    std::ostringstream out;
    if (unit == 0) out << bytes << units[0];
    else if (value < 10.0) out << std::round(value * 10) / 10 << units[unit];
    else out << static_cast<int>(std::round(value)) << units[unit];
    return out.str();
}

// A small, font-safe glyph set. Nerd fonts are lovely and nobody has them
// installed, so Apollo sticks to characters that render everywhere.
std::string glyphFor(const BrowserView::Entry& entry) {
    if (entry.directory) return "▸";
    if (entry.symlink) return "→";
    if (entry.executable) return "▪";
    return "·";
}

Color colorFor(const BrowserView::Entry& entry, const Theme& theme) {
    if (entry.directory) return toFtx(theme.accent);
    if (entry.symlink) return toFtx(theme.accentAlt);
    if (entry.executable) return toFtx(theme.success);
    return toFtx(theme.fg);
}

Color gitColor(char status, const Theme& theme) {
    switch (status) {
        case 'M': return toFtx(theme.warning);
        case 'A': return toFtx(theme.success);
        case 'D': return toFtx(theme.error);
        case '?': return toFtx(theme.muted);
        default:  return toFtx(theme.muted);
    }
}

} // namespace

void BrowserView::setPath(const fs::path& path) {
    std::error_code ec;
    fs::path resolved = fs::weakly_canonical(path, ec);
    if (ec || resolved.empty()) resolved = path;
    if (resolved == path_) return;

    path_ = resolved;
    selected_ = 0;
    scroll_ = 0;
    stamp_ = fs::file_time_type{};
    gitRoot_.clear();
    gitStatus_.clear();
    entries_.clear();
}

void BrowserView::refresh(const BrowserSettings& settings) {
    std::error_code ec;
    entries_.clear();
    error_.clear();

    if (!fs::is_directory(path_, ec)) {
        error_ = "not a directory";
        return;
    }

    for (const auto& item : fs::directory_iterator(
             path_, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;

        Entry entry;
        entry.name = item.path().filename().string();
        if (!settings.showHidden && !entry.name.empty() && entry.name[0] == '.') continue;

        std::error_code itemError;
        entry.symlink = item.is_symlink(itemError);
        entry.directory = item.is_directory(itemError);
        if (!entry.directory) {
            entry.size = item.file_size(itemError);
            if (itemError) entry.size = 0;
            const auto perms = item.status(itemError).permissions();
            entry.executable = (perms & fs::perms::owner_exec) != fs::perms::none;
        }
        entry.modified = item.last_write_time(itemError);
        entries_.push_back(std::move(entry));
    }
    if (ec) error_ = ec.message();

    loadGitStatus();
    for (auto& entry : entries_) {
        if (const auto it = gitStatus_.find(entry.name); it != gitStatus_.end()) {
            entry.git = it->second;
        }
    }

    sortEntries(settings);
    stamp_ = fs::last_write_time(path_, ec);
    lastShowHidden_ = settings.showHidden;
    lastSort_ = settings.sort;
    selected_ = std::clamp(selected_, 0, std::max(0, static_cast<int>(entries_.size()) - 1));
}

void BrowserView::sortEntries(const BrowserSettings& settings) {
    // directory_iterator promises no order at all, so without this the list
    // reshuffles itself every time anything changes.
    std::sort(entries_.begin(), entries_.end(), [&](const Entry& a, const Entry& b) {
        if (settings.dirsFirst && a.directory != b.directory) return a.directory;

        if (settings.sort == "size") {
            if (a.size != b.size) return a.size > b.size;
        } else if (settings.sort == "modified") {
            if (a.modified != b.modified) return a.modified > b.modified;
        } else if (settings.sort == "type") {
            const std::string ax = fs::path(a.name).extension().string();
            const std::string bx = fs::path(b.name).extension().string();
            if (ax != bx) return ax < bx;
        }

        // Case-insensitive, so `Makefile` sits next to `main.cpp`.
        return std::lexicographical_compare(
            a.name.begin(), a.name.end(), b.name.begin(), b.name.end(),
            [](char x, char y) {
                return std::tolower(static_cast<unsigned char>(x)) <
                       std::tolower(static_cast<unsigned char>(y));
            });
    });
}

void BrowserView::loadGitStatus() {
    gitStatus_.clear();

    // Walk up for a .git before running anything: outside a repository this
    // costs one stat per parent instead of spawning a process that will fail.
    std::error_code ec;
    fs::path probe = path_;
    gitRoot_.clear();
    while (!probe.empty()) {
        if (fs::exists(probe / ".git", ec)) { gitRoot_ = probe; break; }
        const fs::path parent = probe.parent_path();
        if (parent == probe) break;
        probe = parent;
    }
    if (gitRoot_.empty()) return;

    const auto result = process::run(
        {"git", "-c", "core.quotepath=false", "status", "--porcelain", "--", "."},
        std::chrono::milliseconds(600), path_.string());
    if (!result.ok()) return;

    std::istringstream lines(result.out);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.size() < 4) continue;
        const char index = line[0];
        const char worktree = line[1];
        std::string name = line.substr(3);

        // Only the first path component matters: a change deep inside a
        // directory marks the directory.
        if (const auto slash = name.find('/'); slash != std::string::npos) {
            name = name.substr(0, slash);
        }
        if (!name.empty() && name.front() == '"') name = name.substr(1, name.size() - 2);

        char status = ' ';
        if (index == '?' || worktree == '?') status = '?';
        else if (index == 'A' || worktree == 'A') status = 'A';
        else if (index == 'D' || worktree == 'D') status = 'D';
        else status = 'M';
        gitStatus_[name] = status;
    }
}

void BrowserView::refreshIfStale(const BrowserSettings& settings) {
    const auto now = std::chrono::steady_clock::now();
    if (now - lastCheck_ < std::chrono::milliseconds(400)) return;
    lastCheck_ = now;

    if (settings.showHidden != lastShowHidden_ || settings.sort != lastSort_) {
        refresh(settings);
        return;
    }

    std::error_code ec;
    const auto stamp = fs::last_write_time(path_, ec);
    if (ec) return;
    if (stamp != stamp_ || entries_.empty()) refresh(settings);
}

const BrowserView::Entry* BrowserView::selected() const {
    if (selected_ < 0 || selected_ >= static_cast<int>(entries_.size())) return nullptr;
    return &entries_[static_cast<std::size_t>(selected_)];
}

void BrowserView::selectByName(const std::string& name) {
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].name == name) { selected_ = static_cast<int>(i); return; }
    }
}

void BrowserView::moveSelection(int delta) {
    if (entries_.empty()) return;
    selected_ = std::clamp(selected_ + delta, 0, static_cast<int>(entries_.size()) - 1);
}

bool BrowserView::onKey(const KeyChord& chord, const BrowserSettings& settings) {
    if (chord.mods != ModNone && chord.mods != ModShift) return false;

    if (chord.key == "up" || chord.key == "k") { moveSelection(-1); return true; }
    if (chord.key == "down" || chord.key == "j") { moveSelection(1); return true; }
    if (chord.key == "pageup") { moveSelection(-std::max(1, lastHeight_ - 2)); return true; }
    if (chord.key == "pagedown") { moveSelection(std::max(1, lastHeight_ - 2)); return true; }
    if (chord.key == "home") { selected_ = 0; return true; }
    if (chord.key == "end") {
        selected_ = std::max(0, static_cast<int>(entries_.size()) - 1);
        return true;
    }
    if (chord.key == "r") { refresh(settings); return true; }

    if (chord.key == "left" || chord.key == "h" || chord.key == "backspace") {
        const fs::path parent = path_.parent_path();
        if (!parent.empty() && parent != path_ && onEnterDirectory) {
            const std::string leaving = path_.filename().string();
            onEnterDirectory(parent);
            // Land on the directory just left, which is where the eye already is.
            refresh(settings);
            selectByName(leaving);
        }
        return true;
    }

    if (chord.key == "enter" || chord.key == "right" || chord.key == "l") {
        const Entry* entry = selected();
        if (!entry) return true;
        const fs::path target = path_ / entry->name;
        if (entry->directory) { if (onEnterDirectory) onEnterDirectory(target); }
        else if (onOpenFile) onOpenFile(target);
        return true;
    }
    return false;
}

bool BrowserView::onClick(int row, bool doubleClick) {
    const int index = scroll_ + row;
    if (index < 0 || index >= static_cast<int>(entries_.size())) return false;

    selected_ = index;
    if (!doubleClick) return true;

    const Entry& entry = entries_[static_cast<std::size_t>(index)];
    const fs::path target = path_ / entry.name;
    if (entry.directory) { if (onEnterDirectory) onEnterDirectory(target); }
    else if (onOpenFile) onOpenFile(target);
    return true;
}

std::string BrowserView::statusLine() const {
    if (!error_.empty()) return error_;

    int directories = 0;
    for (const auto& entry : entries_) directories += entry.directory ? 1 : 0;
    return std::to_string(directories) + " dirs, " +
           std::to_string(static_cast<int>(entries_.size()) - directories) + " files";
}

Element BrowserView::render(const Theme& theme,
                            const BrowserSettings& settings,
                            bool focused,
                            int height,
                            int width) const {
    const_cast<BrowserView*>(this)->lastHeight_ = height;

    if (!error_.empty()) {
        return vbox({text(" " + error_) | color(toFtx(theme.error)), filler()});
    }
    if (entries_.empty()) {
        return vbox({text("  empty") | color(toFtx(theme.muted)), filler()});
    }

    // Keep the selection in view without jumping it to the middle.
    int& scroll = const_cast<BrowserView*>(this)->scroll_;
    const int visible = std::max(1, height);
    if (selected_ < scroll) scroll = selected_;
    if (selected_ >= scroll + visible) scroll = selected_ - visible + 1;
    scroll = std::clamp(scroll, 0, std::max(0, static_cast<int>(entries_.size()) - visible));

    Elements rows;
    for (int i = scroll; i < std::min(scroll + visible, static_cast<int>(entries_.size())); ++i) {
        const Entry& entry = entries_[static_cast<std::size_t>(i)];
        const bool isSelected = i == selected_;

        // name + trailing size column; the glyph and git mark take four columns.
        const std::string sizeLabel = entry.directory ? "" : humanSize(entry.size);
        const int nameRoom = std::max(4, width - 6 - static_cast<int>(sizeLabel.size()));
        const std::string name = elide(entry.name + (entry.directory ? "/" : ""), nameRoom);

        Elements cells;
        cells.push_back(text(settings.icons ? " " + glyphFor(entry) + " " : " ") |
                        color(colorFor(entry, theme)));
        cells.push_back(text(name) | color(colorFor(entry, theme)));
        cells.push_back(filler());
        if (settings.gitStatus && entry.git != ' ') {
            cells.push_back(text(std::string(1, entry.git)) | color(gitColor(entry.git, theme)));
            cells.push_back(text(" "));
        }
        if (!sizeLabel.empty()) {
            cells.push_back(text(sizeLabel + " ") | color(toFtx(theme.muted)));
        }

        Element row = hbox(std::move(cells));
        if (isSelected) {
            row = std::move(row) | bgcolor(toFtx(focused ? theme.selection : theme.surface));
            if (focused) row = bold(std::move(row));
        }
        rows.push_back(std::move(row));
    }
    while (static_cast<int>(rows.size()) < visible) rows.push_back(text(""));

    return vbox(std::move(rows));
}

} // namespace apollo::ui
