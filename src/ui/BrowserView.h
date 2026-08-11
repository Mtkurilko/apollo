// The file browser.
//
// It shows one directory, follows the terminal's working directory when the
// shell reports it, and hands anything the user opens back to the app.
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

namespace apollo::ui {

class BrowserView {
public:
    struct Entry {
        std::string name;
        bool directory = false;
        bool executable = false;
        bool symlink = false;
        std::uintmax_t size = 0;
        std::filesystem::file_time_type modified{};
        char git = ' '; // M, A, D, ?, or a space
    };

    void setPath(const std::filesystem::path& path);
    const std::filesystem::path& path() const { return path_; }

    void refresh(const BrowserSettings& settings);
    // Re-reads only if the directory's timestamp moved. Cheap enough per frame.
    void refreshIfStale(const BrowserSettings& settings);

    // Returns true when the key was the browser's to handle.
    bool onKey(const KeyChord& chord, const BrowserSettings& settings);
    bool onClick(int row, bool doubleClick);

    ftxui::Element render(const Theme& theme,
                          const BrowserSettings& settings,
                          bool focused,
                          int height,
                          int width) const;

    const Entry* selected() const;
    std::string statusLine() const;

    // What to do when something is opened. The app decides; the browser only
    // ever reads the filesystem.
    std::function<void(const std::filesystem::path&)> onEnterDirectory;
    std::function<void(const std::filesystem::path&)> onOpenFile;

    void selectByName(const std::string& name);
    void moveSelection(int delta);

private:
    void sortEntries(const BrowserSettings& settings);
    void loadGitStatus();

    std::filesystem::path path_ = std::filesystem::current_path();
    std::vector<Entry> entries_;
    int selected_ = 0;
    int scroll_ = 0;
    int lastHeight_ = 20;
    bool lastShowHidden_ = false;
    std::string lastSort_;
    std::string error_;

    std::filesystem::file_time_type stamp_{};
    std::chrono::steady_clock::time_point lastCheck_{};
    // Repository root for the current directory, empty when there is none.
    std::filesystem::path gitRoot_;
    std::map<std::string, char> gitStatus_;
};

} // namespace apollo::ui
