#include "core/Commands.h"

#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <cctype>
#include <system_error>

#include "core/Paths.h"

namespace fs = std::filesystem;

namespace apollo {

namespace fuzzy {
namespace {

bool isBoundary(const std::string& text, std::size_t i) {
    if (i == 0) return true;
    const unsigned char prev = static_cast<unsigned char>(text[i - 1]);
    const unsigned char here = static_cast<unsigned char>(text[i]);
    if (prev == ' ' || prev == '_' || prev == '-' || prev == '.' || prev == '/') return true;
    return std::islower(prev) && std::isupper(here); // camelCase
}

} // namespace

std::optional<int> score(const std::string& haystack,
                         const std::string& needle,
                         std::vector<int>* positions) {
    if (positions) positions->clear();
    if (needle.empty()) return 0;
    if (needle.size() > haystack.size()) return std::nullopt;

    int total = 0;
    int consecutive = 0;
    std::size_t at = 0;

    for (const char wanted : needle) {
        const char want = static_cast<char>(std::tolower(static_cast<unsigned char>(wanted)));

        std::size_t found = std::string::npos;
        for (std::size_t i = at; i < haystack.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(haystack[i])) == want) { found = i; break; }
        }
        if (found == std::string::npos) return std::nullopt;

        int gained = 1;
        if (found == 0) gained += 8;                  // matches the first letter
        else if (isBoundary(haystack, found)) gained += 6; // start of a word
        if (found == at && at > 0) { consecutive += 1; gained += 3 + consecutive; }
        else consecutive = 0;

        total += gained;
        if (positions) positions->push_back(static_cast<int>(found));
        at = found + 1;
    }

    total -= static_cast<int>(haystack.size() - needle.size()) / 4;
    return total;
}

} // namespace fuzzy

std::string Command::kindLabel() const {
    switch (kind) {
        case Kind::Builtin:  return "built-in";
        case Kind::Declared: return "config";
        case Kind::Script:   return "script";
    }
    return "";
}

void CommandRegistry::clear() { commands_.clear(); }

void CommandRegistry::addBuiltin(const std::string& name, const std::string& summary) {
    Command command;
    command.name = name;
    command.summary = summary;
    command.kind = Command::Kind::Builtin;
    command.origin = "built in";
    commands_.push_back(std::move(command));
}

bool CommandRegistry::addDeclared(const std::string& name,
                                  const std::string& exec,
                                  const std::string& summary,
                                  const std::string& origin,
                                  bool interactive) {
    if (name.empty() || exec.empty()) return false;
    if (find(name)) return false; // built-ins win; the caller reports the clash

    Command command;
    command.name = name;
    command.summary = summary.empty() ? exec : summary;
    command.exec = exec;
    command.kind = Command::Kind::Declared;
    command.origin = origin;
    command.interactive = interactive;
    commands_.push_back(std::move(command));
    return true;
}

namespace {

std::string describeScript(const fs::path& script) {
    std::ifstream in(script);
    std::string line;
    int examined = 0;
    while (std::getline(in, line) && examined++ < 5) {
        if (line.rfind("#!", 0) == 0) continue;
        if (line.empty()) continue;
        if (line[0] != '#') break;

        std::string summary = line.substr(1);
        const auto begin = summary.find_first_not_of(" \t");
        if (begin == std::string::npos) continue;
        summary = summary.substr(begin);
        if (!summary.empty()) return summary;
    }
    return script.filename().string();
}

} // namespace

int CommandRegistry::scanDirectory(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;

    std::vector<fs::path> found;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        if (::access(entry.path().c_str(), X_OK) != 0) continue;
        if (entry.path().filename().string().front() == '.') continue;
        found.push_back(entry.path());
    }
    std::sort(found.begin(), found.end());

    int added = 0;
    for (const auto& script : found) {
        const std::string name = script.stem().string();
        if (find(name)) continue;

        Command command;
        command.name = name;
        command.summary = describeScript(script);
        command.exec = script.string();
        command.kind = Command::Kind::Script;
        command.origin = paths::contractUser(script);
        commands_.push_back(std::move(command));
        ++added;
    }
    return added;
}

const Command* CommandRegistry::find(const std::string& name) const {
    for (const auto& command : commands_) {
        if (command.name == name) return &command;
    }
    return nullptr;
}

std::vector<std::string> CommandRegistry::names() const {
    std::vector<std::string> out;
    out.reserve(commands_.size());
    for (const auto& command : commands_) out.push_back(command.name);
    return out;
}

std::vector<CommandRegistry::Match> CommandRegistry::search(const std::string& query) const {
    std::vector<Match> matches;

    for (const auto& command : commands_) {
        Match match;
        match.command = &command;

        if (query.empty()) {
            matches.push_back(std::move(match));
            continue;
        }

        const auto onName = fuzzy::score(command.name, query, &match.positions);
        if (onName) {
            match.score = *onName + 20; // a name hit always outranks a summary hit
            matches.push_back(std::move(match));
            continue;
        }
        if (const auto onSummary = fuzzy::score(command.summary, query)) {
            match.score = *onSummary;
            match.positions.clear();
            matches.push_back(std::move(match));
        }
    }

    if (!query.empty()) {
        std::stable_sort(matches.begin(), matches.end(),
                         [](const Match& a, const Match& b) { return a.score > b.score; });
    }
    return matches;
}

} // namespace apollo
