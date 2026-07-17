#ifndef PROPERTIES_PARSER_H
#define PROPERTIES_PARSER_H

#include <string>
#include <map>
#include <filesystem>

class PropertiesParser {
public:
    PropertiesParser(const std::string& filePath);
    
    std::string getProperty(const std::string& key, const std::string& defaultValue = "");
    bool propertyExists(const std::string& key) const;
    
private:
    std::map<std::string, std::string> properties;
    void loadProperties(const std::string& filePath);
    std::string trim(const std::string& str);
};

#endif // PROPERTIES_PARSER_H
