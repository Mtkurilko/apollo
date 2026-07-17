#ifndef CONFIG_H
#define CONFIG_H

#include <optional>
#include <string>
#include <vector>

#include "PropertiesParser.h"

// One named SSH destination. Apollo can hold any number of these; `apollo
// connect` picks between them by name.
struct Connection {
    std::string name;
    std::string host;
    std::string user;
    std::string password;  // used only when key is empty
    std::string keyPath;   // preferred: keeps the password out of the process table
    std::string port = "22";
    std::string remoteDir = "~/APOLLO";

    bool valid() const { return !host.empty() && !user.empty(); }
    std::string label() const { return user + "@" + host; }
};

class Config {
public:
    // Loads ~/.apollo/config.properties, migrating a pre-0.2 apollo.properties
    // in the source tree if that is all that exists. Returns false when there
    // is no configuration yet — the caller should onboard.
    bool load();
    bool save();
    bool exists() const;

    // --- scalars ----------------------------------------------------------
    std::string get(const std::string& key, const std::string& fallback = "") const;
    void set(const std::string& key, const std::string& value);
    bool unset(const std::string& key);
    bool isKnownKey(const std::string& key) const;

    std::string password() const;
    void setPassword(const std::string& value);

    std::string rootDir() const;
    void setRootDir(const std::string& value);

    std::string defaultConnection() const;
    void setDefaultConnection(const std::string& name);

    // --- connections ------------------------------------------------------
    std::vector<std::string> connectionNames() const;
    std::optional<Connection> connection(const std::string& name) const;
    void putConnection(const Connection& conn);
    bool removeConnection(const std::string& name);

    // Decides which connection `apollo connect [name]` should use:
    //   explicit name  -> that one
    //   one configured -> that one
    //   several        -> apollo.defaultConnection, else an error naming them
    std::optional<Connection> resolveConnection(const std::string& requested,
                                                std::string& error) const;

    static bool isValidConnectionName(const std::string& name);

    // Every key/value, with secrets masked. For `apollo config list`.
    std::vector<std::pair<std::string, std::string>> listing() const;

    const std::string& lastError() const { return errorMessage; }

private:
    PropertiesParser props;
    std::string errorMessage;

    bool migrateLegacy();
    static std::string connectionKey(const std::string& name, const std::string& field);
};

#endif // CONFIG_H
