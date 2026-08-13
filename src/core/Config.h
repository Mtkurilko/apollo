// The typed view of ~/.apollo/apollo.conf.
//
// ConfigFile knows about text; this knows what the text means. Every setting
// Apollo has is declared once in schema(), which drives the config editor, the
// `apollo config` output, tab completion and validation, so a new setting
// cannot be added in one place and forgotten in the others.
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
    std::string defaultConnection;
    bool followCwd = true;   // the browser tracks the shell's directory
    bool confirmQuit = false;
    // The prefix key everything else hides behind. Ctrl+Space is chosen
    // because virtually nothing else wants it.
    KeyChord leader{ModCtrl, "space"};
};

struct DecorationSettings {
    std::string theme = "apollo";
    std::string border = "rounded"; // rounded | light | heavy | double | none
    int gaps = 1;
    bool animate = true;
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
    // OSC 52. Off by default: it lets anything the shell runs write to the
    // system clipboard, which is useful and worth choosing deliberately.
    bool osc52Clipboard = false;
    std::string wordChars = "_-./@~";
};

struct BrowserSettings {
    bool show = true;
    int width = 34;
    std::string position = "left"; // left | right
    bool showHidden = false;
    std::string sort = "name"; // name | size | modified | type
    bool dirsFirst = true;
    bool icons = true;
    bool gitStatus = true;
};

// A `command = name, exec, "summary"` line.
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
    // Re-reads the file if its mtime moved. This is what makes the config feel
    // live: save in your editor and Apollo restyles itself.
    bool reloadIfChanged();
    bool exists() const;
    std::filesystem::path path() const { return path_; }

    // Writes a commented starter config. Never overwrites an existing file.
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
    const std::vector<Connection>& connections() const { return connections_; }
    const Connection* connection(const std::string& name) const;

    // The rule, unchanged since Apollo learned about more than one host:
    // with one connection configured a bare `apollo connect` uses it; with
    // several you must name one, unless a default has been chosen.
    std::optional<Connection> resolveConnection(const std::string& requested,
                                                std::string& error) const;

    // Parse errors, unknown keys, bad binds, connections missing credentials.
    const std::vector<std::string>& issues() const { return issues_; }

    // --- writing ----------------------------------------------------------
    bool set(const std::string& path, const std::string& value, std::string* error = nullptr);
    bool unset(const std::string& path);
    bool addConnection(const Connection& conn, std::string* error = nullptr);
    bool removeConnection(const std::string& name);
    bool setDefaultConnection(const std::string& name, std::string* error = nullptr);
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
        int min = 0, max = 0; // for Int

        std::string typeName() const;
    };
    static const std::vector<Setting>& schema();
    static const Setting* setting(const std::string& path);
    // Current value, falling back to the schema default.
    std::string valueOf(const Setting& setting) const;
    // Empty when the value is acceptable, otherwise the reason it is not.
    static std::string validate(const Setting& setting, const std::string& value);

    // The binds Apollo ships with. User binds are layered on top and win.
    static const std::vector<Bind>& defaultBinds();

private:
    void derive();
    void deriveTheme();
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
    std::vector<Connection> connections_;
    std::vector<std::string> issues_;
};

// Reads a pre-0.3 key=value properties file and returns the equivalent modern
// config text, or nullopt when there is nothing to migrate.
std::optional<std::string> migrateLegacyConfig(const std::filesystem::path& properties);

} // namespace apollo
