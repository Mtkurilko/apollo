#include "ui/BrowserView.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <sstream>
#include <system_error>

#include "core/Commands.h"
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

// file_time_type has no portable calendar conversion before C++20.
std::time_t toTimeT(fs::file_time_type when) {
    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        when - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(systemTime);
}

std::string humanTime(fs::file_time_type when) {
    const std::time_t stamp = toTimeT(when);
    std::tm parts{};
    if (!::localtime_r(&stamp, &parts)) return "";

    const std::time_t now = std::time(nullptr);
    std::tm today{};
    ::localtime_r(&now, &today);

    char buffer[32];
    if (parts.tm_year == today.tm_year && parts.tm_yday == today.tm_yday) {
        std::strftime(buffer, sizeof(buffer), "%H:%M", &parts);
    } else if (parts.tm_year == today.tm_year) {
        std::strftime(buffer, sizeof(buffer), "%e %b", &parts);
    } else {
        std::strftime(buffer, sizeof(buffer), "%b %Y", &parts);
    }

    std::string out = buffer;
    if (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out;
}

std::string permissionString(fs::perms mode, bool directory) {
    const auto bit = [&](fs::perms flag, char c) {
        return (mode & flag) != fs::perms::none ? c : '-';
    };
    std::string out(1, directory ? 'd' : '-');
    out += bit(fs::perms::owner_read, 'r');
    out += bit(fs::perms::owner_write, 'w');
    out += bit(fs::perms::owner_exec, 'x');
    out += bit(fs::perms::group_read, 'r');
    out += bit(fs::perms::group_write, 'w');
    out += bit(fs::perms::group_exec, 'x');
    out += bit(fs::perms::others_read, 'r');
    out += bit(fs::perms::others_write, 'w');
    out += bit(fs::perms::others_exec, 'x');
    return out;
}

enum class Kind { Directory, Symlink, Executable, Code, Document, Data, Image, Archive, Media, Plain };

Kind kindOf(const BrowserView::Entry& entry) {
    if (entry.directory) return Kind::Directory;
    if (entry.symlink) return Kind::Symlink;

    std::string extension = fs::path(entry.name).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    static const std::map<std::string, Kind> byExtension = {
        {".c", Kind::Code},     {".h", Kind::Code},     {".cc", Kind::Code},
        {".cpp", Kind::Code},   {".hpp", Kind::Code},   {".m", Kind::Code},
        {".py", Kind::Code},    {".rb", Kind::Code},    {".js", Kind::Code},
        {".ts", Kind::Code},    {".tsx", Kind::Code},   {".jsx", Kind::Code},
        {".rs", Kind::Code},    {".go", Kind::Code},    {".java", Kind::Code},
        {".kt", Kind::Code},    {".swift", Kind::Code}, {".sh", Kind::Code},
        {".zsh", Kind::Code},   {".bash", Kind::Code},  {".lua", Kind::Code},
        {".sql", Kind::Code},   {".php", Kind::Code},   {".cs", Kind::Code},

        {".md", Kind::Document},  {".txt", Kind::Document}, {".rst", Kind::Document},
        {".adoc", Kind::Document},{".pdf", Kind::Document}, {".tex", Kind::Document},
        {".org", Kind::Document},

        {".json", Kind::Data},  {".yaml", Kind::Data},  {".yml", Kind::Data},
        {".toml", Kind::Data},  {".xml", Kind::Data},   {".csv", Kind::Data},
        {".ini", Kind::Data},   {".conf", Kind::Data},  {".properties", Kind::Data},
        {".lock", Kind::Data},  {".env", Kind::Data},

        {".png", Kind::Image},  {".jpg", Kind::Image},  {".jpeg", Kind::Image},
        {".gif", Kind::Image},  {".svg", Kind::Image},  {".webp", Kind::Image},
        {".ico", Kind::Image},  {".bmp", Kind::Image},  {".tiff", Kind::Image},

        {".zip", Kind::Archive},{".tar", Kind::Archive},{".gz", Kind::Archive},
        {".tgz", Kind::Archive},{".bz2", Kind::Archive},{".xz", Kind::Archive},
        {".7z", Kind::Archive}, {".rar", Kind::Archive},{".zst", Kind::Archive},

        {".mp3", Kind::Media},  {".wav", Kind::Media},  {".flac", Kind::Media},
        {".mp4", Kind::Media},  {".mov", Kind::Media},  {".mkv", Kind::Media},
        {".webm", Kind::Media}, {".m4a", Kind::Media},  {".aac", Kind::Media},
    };

    if (const auto it = byExtension.find(extension); it != byExtension.end()) return it->second;
    if (entry.executable) return Kind::Executable;
    return Kind::Plain;
}

std::string glyphFor(Kind kind) {
    switch (kind) {
        case Kind::Directory:  return "▸";
        case Kind::Symlink:    return "→";
        case Kind::Executable: return "▪";
        case Kind::Code:       return "◆";
        case Kind::Document:   return "≡";
        case Kind::Data:       return "◇";
        case Kind::Image:      return "▩";
        case Kind::Archive:    return "▤";
        case Kind::Media:      return "●";
        case Kind::Plain:      return "·";
    }
    return "·";
}

Color colorFor(Kind kind, const Theme& theme) {
    switch (kind) {
        case Kind::Directory:  return toFtx(theme.accent);
        case Kind::Symlink:    return toFtx(theme.accentAlt);
        case Kind::Executable: return toFtx(theme.success);
        case Kind::Code:       return toFtx(theme.accent.mix(theme.fg, 0.35f));
        case Kind::Document:   return toFtx(theme.fg);
        case Kind::Data:       return toFtx(theme.warning);
        case Kind::Image:      return toFtx(theme.accentAlt);
        case Kind::Archive:    return toFtx(theme.error.mix(theme.fg, 0.4f));
        case Kind::Media:      return toFtx(theme.accentAlt.mix(theme.fg, 0.3f));
        case Kind::Plain:      return toFtx(theme.fg.mix(theme.muted, 0.35f));
    }
    return toFtx(theme.fg);
}

Color gitColor(char status, const Theme& theme) {
    switch (status) {
        case 'M': return toFtx(theme.warning);
        case 'A': return toFtx(theme.success);
        case 'D': return toFtx(theme.error);
        default:  return toFtx(theme.muted);
    }
}

std::string sortLabel(const BrowserSettings& settings) {
    return settings.sort + (settings.sortReverse ? " ↑" : " ↓");
}

constexpr int kToolbarRows = 1;
constexpr int kDetailRows = 2;

} // namespace

// --- navigation ------------------------------------------------------------

void BrowserView::setPath(const fs::path& path, bool record) {
    std::error_code ec;
    fs::path resolved = fs::weakly_canonical(path, ec);
    if (ec || resolved.empty()) resolved = path;
    if (resolved == path_) return;

    if (record) {
        back_.push_back(path_);
        forward_.clear();
        if (back_.size() > 128) back_.erase(back_.begin());
    }

    path_ = resolved;
    selected_ = 0;
    scroll_ = 0;
    stamp_ = fs::file_time_type{};
    gitRoot_.clear();
    gitStatus_.clear();
    all_.clear();
    shown_.clear();
    filtering_ = false;
    filter_.clear();
}

bool BrowserView::goBack(const BrowserSettings& settings) {
    if (back_.empty()) return false;

    const fs::path target = back_.back();
    back_.pop_back();
    const fs::path leaving = path_;

    setPath(target, false);
    forward_.push_back(leaving);
    refresh(settings);
    selectByName(leaving.filename().string());
    return true;
}

bool BrowserView::goForward(const BrowserSettings& settings) {
    if (forward_.empty()) return false;

    const fs::path target = forward_.back();
    forward_.pop_back();
    const fs::path leaving = path_;

    setPath(target, false);
    back_.push_back(leaving);
    refresh(settings);
    return true;
}

bool BrowserView::goUp(const BrowserSettings& settings) {
    const fs::path parent = path_.parent_path();
    if (parent.empty() || parent == path_) return false;

    const std::string leaving = path_.filename().string();
    if (onEnterDirectory) onEnterDirectory(parent);
    else setPath(parent);
    refresh(settings);
    selectByName(leaving);
    return true;
}

// --- reading ---------------------------------------------------------------

void BrowserView::refresh(const BrowserSettings& settings) {
    std::error_code ec;
    all_.clear();
    error_.clear();

    if (!fs::is_directory(path_, ec)) {
        error_ = "not a directory";
        shown_.clear();
        return;
    }

    for (const auto& item : fs::directory_iterator(
             path_, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;

        Entry entry;
        entry.name = item.path().filename().string();

        std::error_code itemError;
        entry.symlink = item.is_symlink(itemError);
        entry.directory = item.is_directory(itemError);
        const auto status = item.status(itemError);
        if (!itemError) entry.permissions = status.permissions();
        if (!entry.directory) {
            entry.size = item.file_size(itemError);
            if (itemError) entry.size = 0;
            entry.executable = (entry.permissions & fs::perms::owner_exec) != fs::perms::none;
        }
        entry.modified = item.last_write_time(itemError);
        all_.push_back(std::move(entry));
    }
    if (ec) error_ = ec.message();

    loadGitStatus();
    for (auto& entry : all_) {
        if (const auto it = gitStatus_.find(entry.name); it != gitStatus_.end()) {
            entry.git = it->second;
        }
    }

    applyFilterAndSort(settings);
    stamp_ = fs::last_write_time(path_, ec);
    lastShowHidden_ = settings.showHidden;
    lastSort_ = settings.sort;
    lastReverse_ = settings.sortReverse;
}

void BrowserView::applyFilterAndSort(const BrowserSettings& settings) {
    const std::string wanted = filter_.text;

    shown_.clear();
    for (const auto& entry : all_) {
        if (!settings.showHidden && !entry.name.empty() && entry.name[0] == '.') continue;
        if (!wanted.empty() && !fuzzy::score(entry.name, wanted)) continue;
        shown_.push_back(entry);
    }

    // directory_iterator promises no order, so the list would reshuffle on every change.
    std::sort(shown_.begin(), shown_.end(), [&](const Entry& a, const Entry& b) {
        if (settings.dirsFirst && a.directory != b.directory) return a.directory;

        const auto byName = [&] {
            // Case-insensitive, so `Makefile` sits next to `main.cpp`.
            return std::lexicographical_compare(
                a.name.begin(), a.name.end(), b.name.begin(), b.name.end(),
                [](char x, char y) {
                    return std::tolower(static_cast<unsigned char>(x)) <
                           std::tolower(static_cast<unsigned char>(y));
                });
        };

        bool less = false;
        if (settings.sort == "size") {
            if (a.size != b.size) less = a.size > b.size;
            else return byName();
        } else if (settings.sort == "modified") {
            if (a.modified != b.modified) less = a.modified > b.modified;
            else return byName();
        } else if (settings.sort == "type") {
            const std::string ax = fs::path(a.name).extension().string();
            const std::string bx = fs::path(b.name).extension().string();
            if (ax != bx) less = ax < bx;
            else return byName();
        } else {
            less = byName();
        }
        return settings.sortReverse ? !less : less;
    });

    selected_ = std::clamp(selected_, 0, std::max(0, static_cast<int>(shown_.size()) - 1));
}

void BrowserView::loadGitStatus() {
    gitStatus_.clear();

    // Find .git first: outside a repo this is one stat per parent, not a failed spawn.
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

        // Only the first component matters: a change deep inside marks the directory.
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

    if (settings.showHidden != lastShowHidden_ || settings.sort != lastSort_ ||
        settings.sortReverse != lastReverse_) {
        lastShowHidden_ = settings.showHidden;
        lastSort_ = settings.sort;
        lastReverse_ = settings.sortReverse;
        applyFilterAndSort(settings);
        return;
    }

    std::error_code ec;
    const auto stamp = fs::last_write_time(path_, ec);
    if (ec) return;
    if (stamp != stamp_ || all_.empty()) refresh(settings);
}

// --- selection -------------------------------------------------------------

const BrowserView::Entry* BrowserView::selected() const {
    if (selected_ < 0 || selected_ >= static_cast<int>(shown_.size())) return nullptr;
    return &shown_[static_cast<std::size_t>(selected_)];
}

void BrowserView::selectByName(const std::string& name) {
    for (std::size_t i = 0; i < shown_.size(); ++i) {
        if (shown_[i].name == name) { selected_ = static_cast<int>(i); return; }
    }
}

void BrowserView::moveSelection(int delta) {
    if (shown_.empty()) return;
    selected_ = std::clamp(selected_ + delta, 0, static_cast<int>(shown_.size()) - 1);
    keepSelectionVisible(lastHeight_);
}

void BrowserView::scrollBy(int rows) {
    const int visible = std::max(1, lastHeight_);
    scroll_ = std::clamp(scroll_ + rows, 0,
                         std::max(0, static_cast<int>(shown_.size()) - visible));
}

void BrowserView::keepSelectionVisible(int visible) const {
    visible = std::max(1, visible);
    if (selected_ < scroll_) scroll_ = selected_;
    if (selected_ >= scroll_ + visible) scroll_ = selected_ - visible + 1;
    scroll_ = std::clamp(scroll_, 0, std::max(0, static_cast<int>(shown_.size()) - visible));
}

// --- filtering -------------------------------------------------------------

void BrowserView::beginFilter() {
    filtering_ = true;
    filter_.clear();
}

void BrowserView::endFilter(bool keep) {
    filtering_ = false;
    if (!keep) filter_.clear();
}

bool BrowserView::onFilterKey(const KeyChord& chord, const std::string& raw,
                              const BrowserSettings& settings) {
    if (!filtering_) return false;

    if (chord.key == "escape") {
        endFilter(false);
        applyFilterAndSort(settings);
        return true;
    }
    if (chord.key == "enter") {
        endFilter(true);
        return true;
    }
    if (chord.key == "up") { moveSelection(-1); return true; }
    if (chord.key == "down") { moveSelection(1); return true; }

    const std::string before = filter_.text;
    if (filter_.onKey(chord, raw)) {
        if (filter_.text != before) {
            selected_ = 0;
            scroll_ = 0;
            applyFilterAndSort(settings);
        }
        return true;
    }
    return true; // an open filter swallows the rest
}

// --- input -----------------------------------------------------------------

bool BrowserView::onKey(const KeyChord& chord, const BrowserSettings& settings) {
    if (chord.mods == ModAlt) {
        if (chord.key == "left") return goBack(settings);
        if (chord.key == "right") return goForward(settings);
        if (chord.key == "up") return goUp(settings);
        return false;
    }
    if (chord.mods != ModNone && chord.mods != ModShift) return false;

    if (chord.key == "up") { moveSelection(-1); return true; }
    if (chord.key == "down") { moveSelection(1); return true; }
    if (chord.key == "pageup") { moveSelection(-std::max(1, lastHeight_ - 2)); return true; }
    if (chord.key == "pagedown") { moveSelection(std::max(1, lastHeight_ - 2)); return true; }
    if (chord.key == "home") { selected_ = 0; keepSelectionVisible(lastHeight_); return true; }
    if (chord.key == "end") {
        selected_ = std::max(0, static_cast<int>(shown_.size()) - 1);
        keepSelectionVisible(lastHeight_);
        return true;
    }
    if (chord.key == "[") return goBack(settings);
    if (chord.key == "]") return goForward(settings);
    if (chord.key == "/") { beginFilter(); return true; }

    if (chord.key == "left" || chord.key == "backspace") {
        goUp(settings);
        return true;
    }

    if (chord.key == "enter" || chord.key == "right") {
        const Entry* entry = selected();
        if (!entry) return true;
        const fs::path target = path_ / entry->name;
        if (entry->directory) { if (onEnterDirectory) onEnterDirectory(target); }
        else if (onOpenFile) onOpenFile(target);
        return true;
    }

    if (chord.key == "escape") {
        if (find_.empty()) return false;
        find_.clear();
        return true;
    }

    if (chord.key.size() == 1 && chord.key[0] >= ' ' && chord.key[0] <= '~') {
        if (findExpired()) find_.clear();
        findAt_ = std::chrono::steady_clock::now();

        const std::string wanted = find_ + chord.key;
        const auto lower = [](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            return text;
        };
        const std::string needle = lower(wanted);

        const int count = static_cast<int>(shown_.size());
        for (const bool prefixOnly : {true, false}) {
            for (int step = 1; step <= count; ++step) {
                const int i = (selected_ + step) % std::max(1, count);
                const std::string name = lower(shown_[static_cast<std::size_t>(i)].name);
                const bool hit = prefixOnly ? name.rfind(needle, 0) == 0
                                            : name.find(needle) != std::string::npos;
                if (!hit) continue;
                selected_ = i;
                keepSelectionVisible(lastHeight_);
                find_ = wanted;
                return true;
            }
        }
        return true;
    }
    return false;
}

bool BrowserView::findExpired() const {
    return find_.empty() ||
           std::chrono::steady_clock::now() - findAt_ > std::chrono::milliseconds(1200);
}

std::vector<BrowserView::Segment> BrowserView::toolbar(const BrowserSettings& settings,
                                                       int width) const {
    std::vector<Segment> segments;
    if (!settings.toolbar || width < 12) return segments;

    segments.push_back({Hit::Back, 0, 2, canGoBack()});
    segments.push_back({Hit::Forward, 3, 5, canGoForward()});
    segments.push_back({Hit::Up, 6, 8, path_.parent_path() != path_});

    const int sortWidth = static_cast<int>(sortLabel(settings).size()) + 2;
    int right = width;
    std::vector<Segment> tail;
    if (width >= 30) {
        right -= 3;
        tail.push_back({Hit::Filter, right, right + 2, true});
    }
    if (width >= 30 + sortWidth) {
        right -= sortWidth;
        tail.push_back({Hit::Sort, right, right + sortWidth - 1, true});
    }

    segments.push_back({Hit::Path, 9, std::max(9, right - 1), true});
    segments.insert(segments.end(), tail.rbegin(), tail.rend());
    return segments;
}

BrowserView::Hit BrowserView::hitTest(int row, int column,
                                      const BrowserSettings& settings) const {
    if (!settings.toolbar) return Hit::None;
    if (row != 0) return Hit::None;

    for (const auto& segment : toolbar(settings, lastWidth_)) {
        if (column >= segment.from && column <= segment.to) {
            return segment.enabled ? segment.hit : Hit::None;
        }
    }
    return Hit::None;
}

void BrowserView::hover(int row, int column, const BrowserSettings& settings) {
    hovered_ = hitTest(row, column, settings);
}

bool BrowserView::onClick(int row, int column, bool doubleClick,
                          const BrowserSettings& settings) {
    const int toolbarRows = settings.toolbar ? kToolbarRows : 0;

    if (settings.toolbar && row == 0) {
        switch (hitTest(row, column, settings)) {
            case Hit::Back:    return goBack(settings);
            case Hit::Forward: return goForward(settings);
            case Hit::Up:      return goUp(settings);
            case Hit::Sort:    if (onCycleSort) onCycleSort(); return true;
            case Hit::Filter:  beginFilter(); return true;
            case Hit::Path:
                if (onCopyPath) onCopyPath();
                return true;
            default: return true;
        }
    }

    const int index = scroll_ + row - toolbarRows;
    if (index < 0 || index >= static_cast<int>(shown_.size())) return false;

    selected_ = index;
    if (!doubleClick) return true;

    const Entry& entry = shown_[static_cast<std::size_t>(index)];
    const fs::path target = path_ / entry.name;
    if (entry.directory) { if (onEnterDirectory) onEnterDirectory(target); }
    else if (onOpenFile) onOpenFile(target);
    return true;
}

std::string BrowserView::statusLine() const {
    if (!error_.empty()) return error_;

    int directories = 0;
    for (const auto& entry : shown_) directories += entry.directory ? 1 : 0;

    std::string out = std::to_string(directories) + " dirs, " +
                      std::to_string(static_cast<int>(shown_.size()) - directories) + " files";
    if (!filter_.text.empty()) {
        out += " of " + std::to_string(all_.size()) + " matching '" + filter_.text + "'";
    }
    return out;
}

// --- rendering -------------------------------------------------------------

Element BrowserView::renderToolbar(const Theme& theme, const BrowserSettings& settings,
                                   bool focused, int width) const {
    const auto button = [&](const std::string& glyph, Hit hit, bool enabled) {
        Color colour = enabled ? toFtx(theme.fg) : toFtx(theme.border);
        Element cell = text(" " + glyph + " ") | color(colour);
        if (enabled && hovered_ == hit) {
            cell = std::move(cell) | bgcolor(toFtx(theme.selection)) | color(toFtx(theme.accent));
        }
        return cell;
    };

    Elements parts;
    parts.push_back(button("←", Hit::Back, canGoBack()));
    parts.push_back(button("→", Hit::Forward, canGoForward()));
    parts.push_back(button("↑", Hit::Up, path_.parent_path() != path_));

    int pathRoom = width - 9;
    const int sortWidth = static_cast<int>(sortLabel(settings).size()) + 2;
    const bool showFilter = width >= 30;
    const bool showSort = width >= 30 + sortWidth;
    if (showFilter) pathRoom -= 3;
    if (showSort) pathRoom -= sortWidth;

    if (filtering_ || !filter_.text.empty()) {
        parts.push_back(text("/") | color(toFtx(theme.warning)));
        parts.push_back(filter_.render(theme, "filter", filtering_));
        parts.push_back(filler());
    } else if (!find_.empty() && !findExpired()) {
        parts.push_back(text("find ") | color(toFtx(theme.muted)));
        parts.push_back(text(find_) | color(toFtx(theme.accent)) | bold);
        parts.push_back(filler());
    } else {
        parts.push_back(text(elidePath(paths::contractUser(path_), std::max(4, pathRoom))) |
                        color(toFtx(focused ? theme.fg : theme.muted)));
        parts.push_back(filler());
    }

    if (showSort) {
        Element chip = text(" " + sortLabel(settings) + " ") | color(toFtx(theme.muted));
        if (hovered_ == Hit::Sort) {
            chip = std::move(chip) | bgcolor(toFtx(theme.selection)) | color(toFtx(theme.accent));
        }
        parts.push_back(std::move(chip));
    }
    if (showFilter) {
        Element chip = text(" / ") |
                       color(toFtx(filtering_ ? theme.warning : theme.muted));
        if (hovered_ == Hit::Filter) {
            chip = std::move(chip) | bgcolor(toFtx(theme.selection)) | color(toFtx(theme.accent));
        }
        parts.push_back(std::move(chip));
    }

    return hbox(std::move(parts)) | bgcolor(toFtx(theme.surface));
}

Element BrowserView::renderDetails(const Theme& theme, int width) const {
    const Entry* entry = selected();
    if (!entry) {
        return vbox({separator() | color(toFtx(theme.border)),
                     text(" " + statusLine()) | color(toFtx(theme.muted))});
    }

    const Kind kind = kindOf(*entry);
    Elements first{
        text(" " + glyphFor(kind) + " ") | color(colorFor(kind, theme)),
        text(elide(entry->name, std::max(4, width - 6))) | color(toFtx(theme.fg)),
    };

    std::string facts = permissionString(entry->permissions, entry->directory);
    if (!entry->directory) facts += "  " + humanSize(entry->size);
    facts += "  " + humanTime(entry->modified);
    if (entry->git != ' ') facts += "  git:" + std::string(1, entry->git);

    return vbox({
        separator() | color(toFtx(theme.border)),
        hbox(std::move(first)),
        text(" " + elide(facts, std::max(4, width - 2))) | color(toFtx(theme.muted)),
    });
}

Element BrowserView::render(const Theme& theme,
                            const BrowserSettings& settings,
                            bool focused,
                            int height,
                            int width) const {
    lastWidth_ = width;

    const auto density = BrowserSettings::densityFor(width);
    const bool showToolbar = settings.toolbar && width >= 12 && height >= 4;
    const bool showDetails = settings.details && density != BrowserSettings::Density::Compact &&
                             height >= 8;

    const int chrome = (showToolbar ? kToolbarRows : 0) + (showDetails ? kDetailRows + 1 : 0);
    const int visible = std::max(1, height - chrome);
    lastHeight_ = visible;

    Elements sections;
    if (showToolbar) sections.push_back(renderToolbar(theme, settings, focused, width));

    if (!error_.empty()) {
        sections.push_back(vbox({text(" " + error_) | color(toFtx(theme.error)), filler()}));
        return vbox(std::move(sections));
    }

    if (shown_.empty()) {
        const std::string what =
            filter_.text.empty() ? "  empty" : "  nothing matches '" + filter_.text + "'";
        Elements empty{text(what) | color(toFtx(theme.muted))};
        while (static_cast<int>(empty.size()) < visible) empty.push_back(text(""));
        sections.push_back(vbox(std::move(empty)));
        if (showDetails) sections.push_back(renderDetails(theme, width));
        return vbox(std::move(sections));
    }

    keepSelectionVisible(visible);

    const int iconWidth = settings.icons ? 3 : 1;
    const int gitWidth = settings.gitStatus ? 2 : 0;
    const int sizeWidth = density == BrowserSettings::Density::Compact ? 0 : 7;
    const int timeWidth = density == BrowserSettings::Density::Wide ? 9 : 0;
    const int nameRoom = std::max(4, width - iconWidth - gitWidth - sizeWidth - timeWidth - 1);

    Elements rows;
    for (int i = scroll_; i < std::min(scroll_ + visible, static_cast<int>(shown_.size())); ++i) {
        const Entry& entry = shown_[static_cast<std::size_t>(i)];
        const bool isSelected = i == selected_;
        const Kind kind = kindOf(entry);
        const Color tint = colorFor(kind, theme);

        Elements cells;
        cells.push_back(text(settings.icons ? " " + glyphFor(kind) + " " : " ") | color(tint));
        cells.push_back(text(elide(entry.name + (entry.directory ? "/" : ""), nameRoom)) |
                        color(tint));
        cells.push_back(filler());

        if (gitWidth > 0) {
            cells.push_back(text(entry.git == ' ' ? " " : std::string(1, entry.git)) |
                            color(gitColor(entry.git, theme)));
            cells.push_back(text(" "));
        }
        if (sizeWidth > 0) {
            std::string label = entry.directory ? "" : humanSize(entry.size);
            if (static_cast<int>(label.size()) < sizeWidth - 1) {
                label.insert(label.begin(), sizeWidth - 1 - label.size(), ' ');
            }
            cells.push_back(text(label + " ") | color(toFtx(theme.muted)));
        }
        if (timeWidth > 0) {
            std::string label = humanTime(entry.modified);
            if (static_cast<int>(label.size()) < timeWidth - 1) {
                label.insert(label.begin(), timeWidth - 1 - label.size(), ' ');
            }
            cells.push_back(text(label + " ") | color(toFtx(theme.muted.mix(theme.fg, 0.15f))));
        }

        Element row = hbox(std::move(cells));
        if (isSelected) {
            row = std::move(row) | bgcolor(toFtx(focused ? theme.selection : theme.surface));
            if (focused) row = bold(std::move(row));
        }
        rows.push_back(std::move(row));
    }
    while (static_cast<int>(rows.size()) < visible) rows.push_back(text(""));

    sections.push_back(vbox(std::move(rows)));
    if (showDetails) sections.push_back(renderDetails(theme, width));
    return vbox(std::move(sections));
}

} // namespace apollo::ui
