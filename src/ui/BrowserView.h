// The file browser. Shows more as it is given more room.
#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "core/Config.h"
#include "net/RemoteFs.h"
#include "ui/Widgets.h"

namespace apollo::ui {

class BrowserView {
public:
    struct Entry {
        std::string name;
        bool directory = false;
        bool executable = false;
        bool symlink = false;
        std::uintmax_t size = 0;
        std::time_t modified = 0;
        std::string modifiedText; // set instead when only the remote's own date is known
        std::filesystem::perms permissions{};
        char git = ' '; // M, A, D, ?, or a space
    };

    enum class Hit { None, Back, Forward, Up, Path, Sort, Filter };

    void setPath(const std::filesystem::path& path, bool record = true);
    const std::filesystem::path& path() const { return path_; }

    // --- the other end of a connection ------------------------------------
    // The pane shows one machine at a time. Remote listings arrive from
    // outside rather than being read here, since they take a round trip.
    bool remote() const { return !connection_.empty(); }
    const std::string& connection() const { return connection_; }
    // Where the pane is pointed, which is a remote path while connected.
    std::string where() const { return remote() ? remotePath_ : path_.string(); }
    // Points the pane at a directory on `connection` and marks it as loading.
    void expectRemote(const std::string& connection, const std::string& path,
                      bool record = true);
    void showRemote(const remote::Listing& listing, const BrowserSettings& settings);
    void backToLocal(const std::filesystem::path& path, const BrowserSettings& settings);
    bool loading() const { return loading_; }

    bool canGoBack() const { return !back_.empty(); }
    bool canGoForward() const { return !forward_.empty(); }
    bool goBack(const BrowserSettings& settings);
    bool goForward(const BrowserSettings& settings);
    bool goUp(const BrowserSettings& settings);

    void refresh(const BrowserSettings& settings);
    void refreshIfStale(const BrowserSettings& settings);

    // --- filtering --------------------------------------------------------
    bool filtering() const { return filtering_; }
    const std::string& findPrefix() const { return find_; }
    bool findExpired() const;
    void clearFind() { find_.clear(); }
    void beginFilter();
    void endFilter(bool keep);
    const std::string& filter() const { return filter_.text; }
    bool onFilterKey(const KeyChord& chord, const std::string& raw,
                     const BrowserSettings& settings);

    // --- input ------------------------------------------------------------ Returns true
    // when the key was the browser's to handle.
    bool onKey(const KeyChord& chord, const BrowserSettings& settings);
    bool onClick(int row, int column, bool doubleClick, const BrowserSettings& settings);
    Hit hitTest(int row, int column, const BrowserSettings& settings) const;
    void hover(int row, int column, const BrowserSettings& settings);
    void clearHover() { hovered_ = Hit::None; }

    void scrollBy(int rows);
    void moveSelection(int delta);
    void selectByName(const std::string& name);

    ftxui::Element render(const Theme& theme,
                          const BrowserSettings& settings,
                          bool focused,
                          int height,
                          int width) const;

    const Entry* selected() const;
    std::string statusLine() const;

    std::function<void(const std::filesystem::path&)> onEnterDirectory;
    // Asked when the pane needs a directory from the other end of a
    // connection, which it cannot read for itself.
    std::function<void(const std::string& connection, const std::string& path)> onNeedRemote;
    // Fired when the pane moves itself — back and forward — so the shell can
    // be taken along. Entering a directory goes through onEnterDirectory.
    std::function<void(const std::string& connection, const std::string& path)> onMoved;
    std::function<void(const std::filesystem::path&)> onOpenFile;
    // The toolbar's own buttons, for the ones the app owns.
    std::function<void()> onCopyPath;
    std::function<void()> onCycleSort;

private:
    void applyFilterAndSort(const BrowserSettings& settings);
    void loadGitStatus();
    void keepSelectionVisible(int visible) const;

    struct Segment {
        Hit hit = Hit::None;
        int from = 0, to = 0; // inclusive
        bool enabled = true;
    };
    std::vector<Segment> toolbar(const BrowserSettings& settings, int width) const;

    ftxui::Element renderToolbar(const Theme& theme, const BrowserSettings& settings,
                                 bool focused, int width) const;
    ftxui::Element renderDetails(const Theme& theme, int width) const;

    std::filesystem::path path_ = std::filesystem::current_path();

    // History remembers which machine each step was on, so back and forward
    // still work across a connect.
    struct Step {
        std::string connection;
        std::string path;
    };
    Step here() const { return {connection_, where()}; }
    void goTo(const Step& step, const BrowserSettings& settings);
    std::vector<Step> back_;
    std::vector<Step> forward_;

    std::string connection_;
    std::string remotePath_;
    bool loading_ = false;

    std::vector<Entry> all_;   // everything on disk
    std::vector<Entry> shown_; // after filtering and sorting
    int selected_ = 0;

    bool filtering_ = false;
    LineEdit filter_;
    std::string find_;
    std::chrono::steady_clock::time_point findAt_{};

    // Set by render() rather than by anyone calling in.
    mutable int scroll_ = 0;
    mutable int lastHeight_ = 20;
    mutable int lastWidth_ = 34;

    Hit hovered_ = Hit::None;
    bool lastShowHidden_ = false;
    std::string lastSort_;
    bool lastReverse_ = false;
    std::string error_;

    std::filesystem::file_time_type stamp_{};
    std::chrono::steady_clock::time_point lastCheck_{};
    // Repository root for the current directory, empty when there is none.
    std::filesystem::path gitRoot_;
    std::map<std::string, char> gitStatus_;
};

} // namespace apollo::ui
