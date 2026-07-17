#include "PropertiesParser.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

PropertiesParser::PropertiesParser(const std::string& filePath) {
    load(filePath);
}

bool PropertiesParser::load(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) return false;

    properties.clear();
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '!') continue;

        const std::size_t delimPos = line.find('=');
        if (delimPos == std::string::npos) continue;

        const std::string key = trim(line.substr(0, delimPos));
        if (key.empty()) continue;
        properties[key] = trim(line.substr(delimPos + 1));
    }
    return true;
}

bool PropertiesParser::save(const std::string& filePath) const {
    // Write to a sibling temp file and rename, so an interrupted save can never
    // leave a half-written config behind.
    const fs::path target(filePath);
    const fs::path temp = target.string() + ".tmp";

    {
        std::ofstream file(temp, std::ios::trunc);
        if (!file.is_open()) return false;

        file << "# Apollo configuration\n";
        file << "# Managed by `apollo config` — hand edits are preserved, comments are not.\n";

        std::string lastSection;
        for (const auto& [key, value] : properties) {
            // Blank line between top-level sections (apollo.*, connection.foo.*)
            const std::size_t firstDot = key.find('.');
            std::string section = key.substr(0, firstDot);
            if (section == "connection") {
                const std::size_t secondDot = key.find('.', firstDot + 1);
                section = key.substr(0, secondDot);
            }
            if (section != lastSection) {
                file << "\n";
                lastSection = section;
            }
            file << key << "=" << value << "\n";
        }
        if (!file.good()) return false;
    }

    std::error_code ec;
    fs::rename(temp, target, ec);
    if (ec) {
        fs::remove(temp, ec);
        return false;
    }

    // Contains SSH credentials — readable by the owner only.
    fs::permissions(target, fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ec);
    return true;
}

std::string PropertiesParser::getProperty(const std::string& key, const std::string& defaultValue) const {
    const auto it = properties.find(key);
    return it != properties.end() ? it->second : defaultValue;
}

bool PropertiesParser::propertyExists(const std::string& key) const {
    return properties.find(key) != properties.end();
}

void PropertiesParser::setProperty(const std::string& key, const std::string& value) {
    properties[key] = value;
}

bool PropertiesParser::removeProperty(const std::string& key) {
    return properties.erase(key) > 0;
}

std::size_t PropertiesParser::removePrefix(const std::string& prefix) {
    std::size_t removed = 0;
    for (auto it = properties.begin(); it != properties.end();) {
        if (it->first.rfind(prefix, 0) == 0) {
            it = properties.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

std::vector<std::string> PropertiesParser::keys() const {
    std::vector<std::string> out;
    out.reserve(properties.size());
    for (const auto& [key, value] : properties) out.push_back(key);
    return out;
}

std::vector<std::string> PropertiesParser::keysWithPrefix(const std::string& prefix) const {
    std::vector<std::string> out;
    for (const auto& [key, value] : properties) {
        if (key.rfind(prefix, 0) == 0) out.push_back(key);
    }
    return out;
}

std::string PropertiesParser::trim(const std::string& str) {
    const std::size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    const std::size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}
