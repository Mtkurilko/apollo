#include "PropertiesParser.h"
#include <fstream>
#include <sstream>
#include <algorithm>

PropertiesParser::PropertiesParser(const std::string& filePath) {
    loadProperties(filePath);
}

void PropertiesParser::loadProperties(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return; // File not found, properties remain empty
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Find the equals sign
        size_t delimPos = line.find('=');
        if (delimPos == std::string::npos) {
            continue;
        }

        // Extract key and value
        std::string key = line.substr(0, delimPos);
        std::string value = line.substr(delimPos + 1);

        // Trim whitespace
        key = trim(key);
        value = trim(value);

        properties[key] = value;
    }

    file.close();
}

std::string PropertiesParser::getProperty(const std::string& key, const std::string& defaultValue) {
    auto it = properties.find(key);
    if (it != properties.end()) {
        return it->second;
    }
    return defaultValue;
}

bool PropertiesParser::propertyExists(const std::string& key) const {
    return properties.find(key) != properties.end();
}

std::string PropertiesParser::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}
