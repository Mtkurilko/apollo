#include "core/ConfigFile.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

#include "core/Paths.h"

namespace fs = std::filesystem;

namespace apollo {
namespace {

bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// `#` is a comment marker at the start of a line, or when it stands alone
// between spaces. Anywhere else it is part of a value, which is what makes
// `accent = #7aa2f7` work without quoting.
std::string stripComment(const std::string& line) {
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            return line.substr(0, i);
        }
        if (line[i] != '#') continue;

        bool atStart = true;
        for (std::size_t j = 0; j < i; ++j) {
            if (!std::isspace(static_cast<unsigned char>(line[j]))) { atStart = false; break; }
        }
        if (atStart) return line.substr(0, i);

        const bool spaceBefore = std::isspace(static_cast<unsigned char>(line[i - 1]));
        const bool spaceAfter = i + 1 >= line.size() ||
                                std::isspace(static_cast<unsigned char>(line[i + 1]));
        if (spaceBefore && spaceAfter) return line.substr(0, i);
    }
    return line;
}

// Index of the comment marker in a line, or npos. Mirrors stripComment.
std::size_t commentStart(const std::string& line) {
    const std::string stripped = stripComment(line);
    return stripped.size() == line.size() ? std::string::npos : stripped.size();
}

std::size_t findAssign(const std::string& line) {
    bool inQuote = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') inQuote = !inQuote;
        else if (line[i] == '=' && !inQuote) return i;
    }
    return std::string::npos;
}

std::string unquote(const std::string& value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::string indentOf(const std::string& line) {
    std::size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    return line.substr(0, i);
}

} // namespace

std::string ConfigDiagnostic::format() const {
    return file + ":" + std::to_string(line) + ": " + message;
}

// --- tree lookups ----------------------------------------------------------

const ConfigNode* ConfigNode::child(const std::string& childName,
                                    const std::string& childLabel) const {
    for (const auto& c : children) {
        if (c.name != childName) continue;
        if (!childLabel.empty() && c.label != childLabel) continue;
        return &c;
    }
    return nullptr;
}

const ConfigEntry* ConfigNode::entry(const std::string& key) const {
    for (const auto& e : entries) {
        if (e.key == key) return &e;
    }
    return nullptr;
}

std::vector<const ConfigEntry*> ConfigNode::entriesNamed(const std::string& key) const {
    std::vector<const ConfigEntry*> found;
    for (const auto& e : entries) {
        if (e.key == key) found.push_back(&e);
    }
    return found;
}

// --- static helpers --------------------------------------------------------

std::string ConfigFile::trim(const std::string& text) {
    std::size_t b = 0, e = text.size();
    while (b < e && std::isspace(static_cast<unsigned char>(text[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(text[e - 1]))) --e;
    return text.substr(b, e - b);
}

bool ConfigFile::asBool(const std::string& value, bool fallback) {
    std::string v = trim(value);
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (v == "true" || v == "yes" || v == "on" || v == "1") return true;
    if (v == "false" || v == "no" || v == "off" || v == "0") return false;
    return fallback;
}

int ConfigFile::asInt(const std::string& value, int fallback) {
    try {
        std::size_t used = 0;
        const int parsed = std::stoi(trim(value), &used);
        return used ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

float ConfigFile::asFloat(const std::string& value, float fallback) {
    try {
        std::size_t used = 0;
        const float parsed = std::stof(trim(value), &used);
        return used ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

std::vector<std::string> ConfigFile::split(const std::string& value, char sep) {
    std::vector<std::string> parts;
    std::string current;
    bool inQuote = false;

    for (const char c : value) {
        if (c == '"') { inQuote = !inQuote; current.push_back(c); continue; }
        if (c == sep && !inQuote) { parts.push_back(unquote(trim(current))); current.clear(); continue; }
        current.push_back(c);
    }
    parts.push_back(unquote(trim(current)));

    // A trailing separator means an empty last field the caller never wants.
    if (parts.size() > 1 && parts.back().empty()) parts.pop_back();
    return parts;
}

// --- parsing ---------------------------------------------------------------

void ConfigFile::note(int line, const std::string& message) {
    diags_.push_back({origin_, line + 1, message});
}

std::string ConfigFile::expand(const std::string& value) const {
    if (value.find('$') == std::string::npos) return value;

    std::string out;
    for (std::size_t i = 0; i < value.size();) {
        if (value[i] != '$') { out.push_back(value[i++]); continue; }

        std::size_t end = i + 1;
        while (end < value.size() && isIdentChar(value[end])) ++end;
        const std::string name = value.substr(i + 1, end - i - 1);

        const auto it = vars_.find(name);
        if (name.empty() || it == vars_.end()) {
            out.append(value, i, end - i); // unknown: leave it verbatim
        } else {
            out += it->second;
        }
        i = end;
    }
    return out;
}

bool ConfigFile::parse(const std::string& text, const std::string& originName) {
    lines_.clear();
    origin_ = originName;

    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines_.push_back(line);
    }
    reparse();
    return ok();
}

void ConfigFile::reparse() {
    root_ = ConfigNode{};
    vars_.clear();
    diags_.clear();
    sourced_.clear();

    std::vector<ConfigNode*> stack{&root_};

    for (int i = 0; i < static_cast<int>(lines_.size()); ++i) {
        const std::string content = trim(stripComment(lines_[i]));
        if (content.empty()) continue;

        if (content == "}") {
            if (stack.size() == 1) { note(i, "unmatched '}'"); continue; }
            stack.back()->closeLine = i;
            stack.pop_back();
            continue;
        }

        if (content.back() == '{') {
            const std::string header = trim(content.substr(0, content.size() - 1));
            std::istringstream words(header);
            std::string name, label, extra;
            words >> name >> label >> extra;

            if (name.empty()) { note(i, "section has no name"); continue; }
            if (!extra.empty()) {
                note(i, "section '" + name + "' takes at most one label");
            }

            ConfigNode child;
            child.name = name;
            child.label = label;
            child.openLine = i;
            stack.back()->children.push_back(std::move(child));
            stack.push_back(&stack.back()->children.back());
            continue;
        }

        const std::size_t eq = findAssign(content);
        if (eq == std::string::npos) {
            note(i, "expected 'key = value', a 'section {' or '}'");
            continue;
        }

        const std::string key = trim(content.substr(0, eq));
        const std::string raw = trim(content.substr(eq + 1));
        if (key.empty()) { note(i, "missing key before '='"); continue; }

        if (key[0] == '$') {
            vars_[key.substr(1)] = unquote(expand(raw));
            continue;
        }

        if (key == "source" && stack.size() == 1) {
            const fs::path included = paths::expandUser(unquote(expand(raw)));
            std::error_code ec;
            if (!fs::exists(included, ec)) {
                note(i, "source: no such file: " + included.string());
                continue;
            }
            ConfigFile nested;
            if (!nested.load(included)) {
                for (const auto& d : nested.diagnostics()) diags_.push_back(d);
            }
            // Merged entries carry line -1: they belong to another file, and
            // `apollo config set` must never try to rewrite them there.
            const auto adopt = [](auto&& self, ConfigNode& into, const ConfigNode& from) -> void {
                for (const auto& e : from.entries) {
                    ConfigEntry copy = e;
                    copy.line = -1;
                    into.entries.push_back(std::move(copy));
                }
                for (const auto& c : from.children) {
                    ConfigNode copy;
                    copy.name = c.name;
                    copy.label = c.label;
                    self(self, copy, c);
                    into.children.push_back(std::move(copy));
                }
            };
            adopt(adopt, root_, nested.root());
            for (const auto& [k, v] : nested.variables()) vars_.emplace(k, v);
            sourced_.push_back(included);
            continue;
        }

        ConfigEntry entry;
        entry.key = key;
        entry.rawValue = raw;
        entry.value = unquote(expand(raw));
        entry.line = i;
        stack.back()->entries.push_back(std::move(entry));
    }

    while (stack.size() > 1) {
        note(stack.back()->openLine, "section '" + stack.back()->name + "' is never closed");
        stack.pop_back();
    }
}

bool ConfigFile::load(const fs::path& path) {
    std::ifstream in(path);
    if (!in) {
        diags_.push_back({path.string(), 0, "cannot read " + path.string()});
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parse(buffer.str(), path.string());
}

bool ConfigFile::save(const fs::path& path, std::string* error) const {
    std::error_code ec;
    if (path.has_parent_path() && !paths::ensureDir(path.parent_path(), error)) return false;

    // Write beside the target and rename: an interrupted save can never leave
    // a half-written config behind.
    const fs::path temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out) {
            if (error) *error = "cannot write " + temp.string();
            return false;
        }
        out << text();
        if (!out) {
            if (error) *error = "write failed: " + temp.string();
            return false;
        }
    }

    fs::permissions(temp, fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ec);
    fs::rename(temp, path, ec);
    if (ec) {
        fs::remove(temp, ec);
        if (error) *error = "cannot replace " + path.string();
        return false;
    }
    return true;
}

std::string ConfigFile::text() const {
    std::string out;
    for (const auto& line : lines_) { out += line; out.push_back('\n'); }
    return out;
}

// --- reading ---------------------------------------------------------------

bool ConfigFile::resolve(const std::string& path,
                         std::vector<std::string>& sectionPath,
                         std::string& key) const {
    sectionPath.clear();
    std::string current;
    for (const char c : path) {
        if (c == '.') { sectionPath.push_back(current); current.clear(); }
        else current.push_back(c);
    }
    key = current;
    if (key.empty()) return false;
    return std::all_of(sectionPath.begin(), sectionPath.end(),
                       [](const std::string& p) { return !p.empty(); });
}

namespace {

// Walks a dotted path, accepting both `connection lab { }` and
// `connection { lab { } }` for the same "connection.lab".
const ConfigNode* descend(const ConfigNode* node,
                          const std::vector<std::string>& path,
                          std::size_t index) {
    if (index >= path.size()) return node;

    if (index + 1 < path.size()) {
        if (const ConfigNode* labelled = node->child(path[index], path[index + 1])) {
            if (const ConfigNode* found = descend(labelled, path, index + 2)) return found;
        }
    }
    if (const ConfigNode* plain = node->child(path[index])) {
        return descend(plain, path, index + 1);
    }
    return nullptr;
}

} // namespace

const ConfigNode* ConfigFile::section(const std::string& path) const {
    if (path.empty()) return &root_;
    std::vector<std::string> parts;
    std::string last;
    if (!resolve(path, parts, last)) return nullptr;
    parts.push_back(last);
    return descend(&root_, parts, 0);
}

std::optional<std::string> ConfigFile::get(const std::string& path) const {
    std::vector<std::string> sectionPath;
    std::string key;
    if (!resolve(path, sectionPath, key)) return std::nullopt;

    const ConfigNode* node = descend(&root_, sectionPath, 0);
    if (!node) return std::nullopt;
    if (const ConfigEntry* e = node->entry(key)) return e->value;
    return std::nullopt;
}

std::string ConfigFile::get(const std::string& path, const std::string& fallback) const {
    return get(path).value_or(fallback);
}

std::vector<std::string> ConfigFile::getAll(const std::string& path) const {
    std::vector<std::string> values;
    std::vector<std::string> sectionPath;
    std::string key;
    if (!resolve(path, sectionPath, key)) return values;

    const ConfigNode* node = descend(&root_, sectionPath, 0);
    if (!node) return values;
    for (const auto* e : node->entriesNamed(key)) values.push_back(e->value);
    return values;
}

std::vector<const ConfigNode*> ConfigFile::labelled(const std::string& name) const {
    std::vector<const ConfigNode*> found;
    for (const auto& c : root_.children) {
        if (c.name == name && !c.label.empty()) found.push_back(&c);
    }
    return found;
}

// --- writing ---------------------------------------------------------------

ConfigNode* ConfigFile::findSection(const std::string& path, bool create) {
    if (path.empty()) return &root_;

    std::vector<std::string> parts;
    std::string last;
    if (!resolve(path, parts, last)) return nullptr;
    parts.push_back(last);

    // Const-walk first; the tree is rebuilt from text after any insertion, so
    // holding a mutable pointer across an edit would be wrong anyway.
    const ConfigNode* existing = descend(&root_, parts, 0);
    if (existing) return const_cast<ConfigNode*>(existing);
    if (!create) return nullptr;

    // Create the missing block at the end of the file. Labelled blocks are
    // written as `connection lab { }`; Apollo has no deeper sections, and
    // silently inventing the wrong shape would be worse than refusing.
    std::string header;
    if (parts.size() == 1) header = parts[0];
    else if (parts.size() == 2) header = parts[0] + " " + parts[1];
    else return nullptr;

    if (!lines_.empty() && !trim(lines_.back()).empty()) lines_.push_back("");
    lines_.push_back(header + " {");
    lines_.push_back("}");
    reparse();

    return const_cast<ConfigNode*>(descend(&root_, parts, 0));
}

void ConfigFile::set(const std::string& path, const std::string& value) {
    std::vector<std::string> sectionPath;
    std::string key;
    if (!resolve(path, sectionPath, key)) return;

    std::string sectionDotted;
    for (const auto& p : sectionPath) {
        if (!sectionDotted.empty()) sectionDotted += ".";
        sectionDotted += p;
    }

    ConfigNode* node = findSection(sectionDotted, true);
    if (!node) return;

    const ConfigEntry* existing = node->entry(key);
    if (existing && existing->line >= 0) {
        // Rewrite just the value, keeping the key spelling, the indentation and
        // any trailing comment the user wrote.
        std::string& line = lines_[existing->line];
        const std::size_t eq = findAssign(line);
        if (eq != std::string::npos) {
            std::size_t valueStart = eq + 1;
            while (valueStart < line.size() && line[valueStart] == ' ') ++valueStart;

            const std::size_t comment = commentStart(line);
            std::string tail;
            if (comment != std::string::npos && comment > valueStart) {
                tail = "  " + trim(line.substr(comment));
            }
            line = line.substr(0, valueStart) + value + tail;
            reparse();
            return;
        }
    }

    // New key: place it just before the section's closing brace, lined up with
    // whatever the neighbours do. A settings screen that writes a ragged line
    // into a hand-aligned file is a small betrayal.
    const int at = node->closeLine >= 0 ? node->closeLine
                                        : static_cast<int>(lines_.size());
    const std::string indent = node == &root_
                                   ? ""
                                   : indentOf(lines_[node->openLine]) + "    ";

    std::size_t column = 0;
    for (const auto& sibling : node->entries) {
        if (sibling.line < 0) continue;
        const std::size_t eq = findAssign(lines_[sibling.line]);
        if (eq != std::string::npos) column = std::max(column, eq);
    }

    std::string spelled = indent + key;
    // One space is added below, so pad to the column before the '='.
    if (column > 0 && column - 1 > spelled.size()) {
        spelled.append(column - 1 - spelled.size(), ' ');
    }
    lines_.insert(lines_.begin() + at, spelled + " = " + value);
    reparse();
}

bool ConfigFile::unset(const std::string& path) {
    std::vector<std::string> sectionPath;
    std::string key;
    if (!resolve(path, sectionPath, key)) return false;

    const ConfigNode* node = descend(&root_, sectionPath, 0);
    if (!node) return false;

    const ConfigEntry* existing = node->entry(key);
    if (!existing || existing->line < 0) return false;

    lines_.erase(lines_.begin() + existing->line);
    reparse();
    return true;
}

bool ConfigFile::removeSection(const std::string& path) {
    const ConfigNode* node = section(path);
    if (!node || node == &root_ || node->openLine < 0) return false;

    const int last = node->closeLine >= 0 ? node->closeLine
                                          : static_cast<int>(lines_.size()) - 1;
    lines_.erase(lines_.begin() + node->openLine, lines_.begin() + last + 1);

    // Leave at most one blank line where the block used to be.
    while (node->openLine > 0 && node->openLine < static_cast<int>(lines_.size()) &&
           trim(lines_[node->openLine]).empty() &&
           trim(lines_[node->openLine - 1]).empty()) {
        lines_.erase(lines_.begin() + node->openLine);
    }
    reparse();
    return true;
}

void ConfigFile::append(const std::string& key, const std::string& value) {
    // Group repeated keys together: put it after the last one if any exist.
    int at = -1;
    for (const auto& e : root_.entries) {
        if (e.key == key && e.line > at) at = e.line;
    }
    if (at >= 0) lines_.insert(lines_.begin() + at + 1, key + " = " + value);
    else {
        if (!lines_.empty() && !trim(lines_.back()).empty()) lines_.push_back("");
        lines_.push_back(key + " = " + value);
    }
    reparse();
}

bool ConfigFile::removeMatching(const std::string& key, const std::string& valuePrefix) {
    std::vector<int> doomed;
    for (const auto& e : root_.entries) {
        if (e.key != key || e.line < 0) continue;
        if (e.value.rfind(valuePrefix, 0) == 0) doomed.push_back(e.line);
    }
    if (doomed.empty()) return false;

    std::sort(doomed.rbegin(), doomed.rend());
    for (const int line : doomed) lines_.erase(lines_.begin() + line);
    reparse();
    return true;
}

} // namespace apollo
