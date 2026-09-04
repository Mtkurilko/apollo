// Commands. Built in, declared in the config, or executables dropped into
// ~/.apollo/commands. Also the fuzzy matcher the palette uses.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace apollo {

// fzf-style subsequence match. Word boundaries score higher.
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

    bool interactive = true;

    std::string kindLabel() const;
};

class CommandRegistry {
public:
    void clear();

    void addBuiltin(const std::string& name, const std::string& summary);
    bool addDeclared(const std::string& name,
                     const std::string& exec,
                     const std::string& summary,
                     const std::string& origin,
                     bool interactive = true);
    int scanDirectory(const std::filesystem::path& dir);

    const Command* find(const std::string& name) const;
    const std::vector<Command>& all() const { return commands_; }
    std::vector<std::string> names() const;

    struct Match {
        const Command* command = nullptr;
        int score = 0;
        std::vector<int> positions; // indices into name, for highlighting
    };
    std::vector<Match> search(const std::string& query) const;

private:
    std::vector<Command> commands_;
};

} // namespace apollo
