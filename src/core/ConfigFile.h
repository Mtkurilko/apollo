// Parser and writer for apollo.conf.
//
//     general {
//         workspace = ~/Apollo
//     }
//     connection lab { host = 10.0.0.5 }
//     bind = CTRL, K, command_palette
//
// Rewriting a value keeps the file's comments and layout. `#` starts a
// comment at the start of a line or between spaces, so #7aa2f7 is a colour.
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace apollo {

struct ConfigDiagnostic {
    std::string file;
    int line = 0; // 1-based, for messages
    std::string message;

    std::string format() const;
};

struct ConfigEntry {
    std::string key;
    std::string value;    // variables expanded, quotes stripped
    std::string rawValue; // exactly as written
    int line = -1;        // 0-based index into the owning file's lines
};

// A `name { ... }` block, or `name label { ... }`. The root node has no name.
struct ConfigNode {
    std::string name;
    std::string label;
    std::vector<ConfigEntry> entries;
    std::vector<ConfigNode> children;
    int openLine = -1;
    int closeLine = -1;

    const ConfigNode* child(const std::string& name,
                            const std::string& label = "") const;
    const ConfigEntry* entry(const std::string& key) const;
    std::vector<const ConfigEntry*> entriesNamed(const std::string& key) const;
};

class ConfigFile {
public:
    bool parse(const std::string& text, const std::string& originName = "<memory>");
    bool load(const std::filesystem::path& path);

    // Writes the current text atomically, with owner-only permissions.
    bool save(const std::filesystem::path& path, std::string* error = nullptr) const;

    // --- reading ---------------------------------------------------------- Paths are
    // dotted: "general.workspace", "connection.lab.host".
    std::optional<std::string> get(const std::string& path) const;
    std::string get(const std::string& path, const std::string& fallback) const;
    std::vector<std::string> getAll(const std::string& path) const;

    const ConfigNode& root() const { return root_; }
    const ConfigNode* section(const std::string& path) const;
    // Every `name <label> { }` block, in file order.
    std::vector<const ConfigNode*> labelled(const std::string& name) const;

    const std::map<std::string, std::string>& variables() const { return vars_; }
    const std::vector<ConfigDiagnostic>& diagnostics() const { return diags_; }
    bool ok() const { return diags_.empty(); }

    void set(const std::string& path, const std::string& value);
    bool unset(const std::string& path);
    bool removeSection(const std::string& path);
    void append(const std::string& key, const std::string& value);
    bool removeMatching(const std::string& key, const std::string& valuePrefix);

    std::string text() const;

    // --- value helpers ----------------------------------------------------
    static bool asBool(const std::string& value, bool fallback);
    static int asInt(const std::string& value, int fallback);
    static float asFloat(const std::string& value, float fallback);
    static std::vector<std::string> split(const std::string& value, char sep = ',');
    static std::string trim(const std::string& text);

private:
    struct Located { ConfigNode* node; };

    void reparse();
    ConfigNode* findSection(const std::string& path, bool create);
    bool resolve(const std::string& path,
                 std::vector<std::string>& sectionPath,
                 std::string& key) const;
    std::string expand(const std::string& value) const;
    void note(int line, const std::string& message);

    std::vector<std::string> lines_;
    ConfigNode root_;
    std::map<std::string, std::string> vars_;
    std::vector<ConfigDiagnostic> diags_;
    std::string origin_ = "<memory>";
    // Files pulled in by `source =`; they are read but never rewritten.
    std::vector<std::filesystem::path> sourced_;
};

} // namespace apollo
