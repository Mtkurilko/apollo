// Typed view of apollo.conf. Every setting is declared once in schema(),
// which then drives validation/the editor/the CLI/completion.
#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/ConfigFile.h"
#include "core/Keys.h"
#include "core/Theme.h"
#include "net/Ssh.h"

namespace apollo {

struct GeneralSettings {
    std::string workspace = "~";
    std::string shell;  // empty: the user's login shell
    std::string editor; // empty: $EDITOR, then $VISUAL, then the system opener
    bool followCwd = true;   // the browser tracks the shell's directory
    bool confirmQuit = false;
    // Tab opened for a connection goes away with it instead of turning back
    // into a local shell.
    bool disconnectClosesTab = true;
    // Space works at an empty prompt and in the browser.
    KeyChord leader{ModNone, "space"};
    // Also the leader, everywhere: inside vim, mid-command. Two by default,
    // since some systems keep Ctrl+Space for switching input languages.
    std::vector<KeyChord> leaderAnywhere{{ModCtrl, "space"}, {ModCtrl, "\\"}};
};

struct DecorationSettings {
    std::string theme = "apollo";
    std::string border = "rounded"; // rounded | light | heavy | double | none
    int gaps = 1;
    bool animate = true;
    bool boot = true; // the splash on the way in
    bool dimInactive = true;
    bool statusBar = true;
    bool titleBar = true;
};

struct TerminalSettings {
    int scrollback = 10000;
    int scrollLines = 3;
    std::string cursor = "block"; // block | bar | underline
    bool cursorBlink = true;
    bool bell = false;
    bool copyOnSelect = true;
    bool shellIntegration = true; // OSC 7/133: cwd tracking and prompt jumps
    bool osc52Clipboard = false;  // OSC 52, off by default
    std::string wordChars = "_-./@~";
};

struct BrowserSettings {
    bool show = true;
    int width = 42;
    std::string position = "left";  // left | right
    std::string layout = "split";   // split | stacked
    bool showHidden = false;
    std::string sort = "name"; // name | size | modified | type
    bool sortReverse = false;
    bool dirsFirst = true;
    bool icons = true;
    bool gitStatus = true;
    bool toolbar = true;  // back/forward, the path, sort and filter
    bool details = true;  // the line about the selected entry

    enum class Density { Compact, Normal, Wide };
    static Density densityFor(int columns) {
        if (columns < 30) return Density::Compact;
        if (columns < 52) return Density::Normal;
        return Density::Wide;
    }
};

// What happens when you open a file from the browser. One rule per extension
// plus "*" for the rest. "ask" means show the choice and remember it.
struct OpenRule {
    std::string match; // an extension without the dot, lowercased, or "*"
    std::string how;   // "ask", "desktop", "editor", or a command line
    int line = -1;
};

struct DeclaredCommand {
    std::string name;
    std::string exec;
    std::string summary;
    bool interactive = true;
    int line = -1;
};

class Config {
public:
    // --- lifecycle --------------------------------------------------------
    bool load();                                   // from paths::configFile()
    bool loadFrom(const std::filesystem::path& path);
    bool loadText(const std::string& text);        // for tests
    bool save(std::string* error = nullptr);
    bool reloadIfChanged();
    bool exists() const;
    std::filesystem::path path() const { return path_; }

    // Writes the commented starter config. Never overwrites an existing file.
    static std::string defaultText();
    bool writeDefault(std::string* error = nullptr);

    // --- reading ----------------------------------------------------------
    const GeneralSettings& general() const { return general_; }
    const DecorationSettings& decoration() const { return decoration_; }
    const TerminalSettings& terminal() const { return terminal_; }
    const BrowserSettings& browser() const { return browser_; }
    const Theme& theme() const { return theme_; }
    const std::vector<Bind>& binds() const { return binds_; }
    const std::vector<DeclaredCommand>& commands() const { return commands_; }
    const std::vector<OpenRule>& openRules() const { return open_; }
    // Rule for a filename. Its extension, then "*", then nothing.
    const OpenRule* openRuleFor(const std::string& filename) const;
    // Extension a rule gets remembered under. Empty if there isn't one.
    static std::string openKeyFor(const std::string& filename);
    // Same value as a rule writes it. "md", ".MD" and "*" all normalize.
    static std::string openMatch(const std::string& text);
    bool setOpenRule(const std::string& match, const std::string& how);
    bool removeOpenRule(const std::string& match);
    const std::vector<Connection>& connections() const { return connections_; }
    const Connection* connection(const std::string& name) const;

    std::optional<Connection> resolveConnection(const std::string& requested,
                                                std::string& error) const;

    const std::vector<std::string>& issues() const { return issues_; }

    // --- writing ----------------------------------------------------------
    bool set(const std::string& path, const std::string& value, std::string* error = nullptr);
    bool unset(const std::string& path);
    bool addConnection(const Connection& conn, std::string* error = nullptr);
    bool removeConnection(const std::string& name);
    bool addBind(const std::string& spec, std::string* error = nullptr);
    bool removeBind(const std::string& spec);
    bool addCommand(const std::string& name, const std::string& exec,
                    const std::string& summary, std::string* error = nullptr);
    bool removeCommand(const std::string& name);

    ConfigFile& file() { return file_; }
    const ConfigFile& file() const { return file_; }

    // --- schema -----------------------------------------------------------
    struct Setting {
        enum class Type { String, Bool, Int, Enum, Color, Path };

        std::string path;
        Type type = Type::String;
        std::string defaultValue;
        std::string summary;
        std::vector<std::string> choices;
        bool openChoices = false;
        int min = 0, max = 0; // for Int
    };
    static const std::vector<Setting>& schema();
    static const Setting* setting(const std::string& path);
    static std::vector<std::string> availableThemes();
    std::string valueOf(const Setting& setting) const;
    // Empty if the value is fine, otherwise why it isn't.
    static std::string validate(const Setting& setting, const std::string& value);

    static const std::vector<Bind>& defaultBinds();

private:
    void derive();
    void deriveTheme();
    // Reads ~/.apollo/themes/<name>.conf. `base = <built-in>` plus a colors block.
    std::optional<Theme> loadThemeFile(const std::string& name);
    void deriveBinds();
    void note(const std::string& issue);

    ConfigFile file_;
    std::filesystem::path path_;
    std::filesystem::file_time_type stamp_{};

    GeneralSettings general_;
    DecorationSettings decoration_;
    TerminalSettings terminal_;
    BrowserSettings browser_;
    Theme theme_;
    std::vector<Bind> binds_;
    std::vector<DeclaredCommand> commands_;
    std::vector<OpenRule> open_;
    std::vector<Connection> connections_;
    std::vector<std::string> issues_;
};

std::optional<std::string> migrateLegacyConfig(const std::filesystem::path& properties);

} // namespace apollo
