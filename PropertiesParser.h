#ifndef PROPERTIES_PARSER_H
#define PROPERTIES_PARSER_H

#include <map>
#include <string>
#include <vector>

// A small `key=value` store. Keys are held sorted, so a saved file always comes
// back out in a stable, diffable order.
class PropertiesParser {
public:
    PropertiesParser() = default;
    explicit PropertiesParser(const std::string& filePath);

    bool load(const std::string& filePath);
    bool save(const std::string& filePath) const;

    std::string getProperty(const std::string& key, const std::string& defaultValue = "") const;
    bool propertyExists(const std::string& key) const;

    void setProperty(const std::string& key, const std::string& value);
    bool removeProperty(const std::string& key);
    // Erases every key starting with `prefix`. Returns how many were removed.
    std::size_t removePrefix(const std::string& prefix);

    std::vector<std::string> keys() const;
    std::vector<std::string> keysWithPrefix(const std::string& prefix) const;

    const std::map<std::string, std::string>& all() const { return properties; }
    bool empty() const { return properties.empty(); }

private:
    std::map<std::string, std::string> properties;

    static std::string trim(const std::string& str);
};

#endif // PROPERTIES_PARSER_H
