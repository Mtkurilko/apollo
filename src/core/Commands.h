// Apollo's command table.
//
// Three things end up here and are indistinguishable once registered:
//   * built-ins, compiled in;
//   * `command = name, exec, "summary"` lines in apollo.conf;
//   * any executable file dropped into ~/.apollo/commands.
//
// That is the whole extension story: no plugin ABI, no rebuild, and a command
// somebody adds shows up in the palette and in `apollo help` immediately.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace apollo {

// Subsequence matching in the spirit of fzf: every character of `needle` must
// appear in `haystack` in order, and matches at word boundaries score higher.
namespace fuzzy {
std::optional<int> score(const std::string& haystack,
                         const std::string& needle,
                         std::vector<int>* positions = nullptr);
}

struct Command {
    enum class Kind { Builtin, Declared, Script };

    std::string name;
    std::string summary;
    std::string exec;   // empty for built-ins
    Kind kind = Kind::Builtin;
    std::string origin; // shown by `apollo commands`, so surprises are traceable

    // Declared and script commands run in the terminal pane by default; a
    // `quiet` command has its output captured and shown as a notice instead.
    bool interactive = true;

    std::string kindLabel() const;
};

class CommandRegistry {
public:
    void clear();

    void addBuiltin(const std::string& name, const std::string& summary);
    // Returns false and leaves the table untouched if `name` is already taken.
    bool addDeclared(const std::string& name,
                     const std::string& exec,
                     const std::string& summary,
                     const std::string& origin,
                     bool interactive = true);
    // Registers every executable file in `dir` under its own file name.
    int scanDirectory(const std::filesystem::path& dir);

    const Command* find(const std::string& name) const;
    const std::vector<Command>& all() const { return commands_; }
    std::vector<std::string> names() const;

    struct Match {
        const Command* command = nullptr;
        int score = 0;
        std::vector<int> positions; // indices into name, for highlighting
    };
    // Best matches first. An empty query returns everything, in table order.
    std::vector<Match> search(const std::string& query) const;

private:
    std::vector<Command> commands_;
};

} // namespace apollo
