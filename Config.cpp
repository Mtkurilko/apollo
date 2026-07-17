#include "Config.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

#include "AppPaths.h"

namespace fs = std::filesystem;

namespace {

constexpr const char* kConnectionPrefix = "connection.";

// Any key whose last segment is one of these gets masked in `config list`.
bool isSecretKey(const std::string& key) {
    const std::size_t dot = key.rfind('.');
    const std::string leaf = dot == std::string::npos ? key : key.substr(dot + 1);
    return leaf == "password";
}

} // namespace

std::string Config::connectionKey(const std::string& name, const std::string& field) {
    return std::string(kConnectionPrefix) + name + "." + field;
}

bool Config::isValidConnectionName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    // Dots would collide with the key namespace; whitespace breaks tokenizing.
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_';
    });
}

bool Config::load() {
    const fs::path path = apollo::configPath();
    if (props.load(path.string())) return true;

    if (migrateLegacy()) return true;

    props = PropertiesParser{};
    return false;
}

bool Config::migrateLegacy() {
    // Pre-0.2 stored a single flat ssh.* block next to the sources.
    PropertiesParser legacy;
    if (!legacy.load(apollo::legacyConfigPath().string())) return false;
    if (legacy.empty()) return false;

    props = PropertiesParser{};
    props.setProperty("apollo.password", legacy.getProperty("apollo.password", ""));

    const std::string host = legacy.getProperty("ssh.host", "");
    const std::string user = legacy.getProperty("ssh.user", "");
    if (!host.empty() && !user.empty()) {
        Connection conn;
        conn.name = "default";
        conn.host = host;
        conn.user = user;
        conn.password = legacy.getProperty("ssh.password", "");
        conn.keyPath = legacy.getProperty("ssh.key", "");
        conn.port = legacy.getProperty("ssh.port", "22");
        putConnection(conn);
        setDefaultConnection("default");
    }

    apollo::ensureConfigDir();
    return save();
}

bool Config::save() {
    if (!apollo::ensureConfigDir()) {
        errorMessage = "Could not create " + apollo::configDir().string();
        return false;
    }
    if (!props.save(apollo::configPath().string())) {
        errorMessage = "Could not write " + apollo::configPath().string();
        return false;
    }
    errorMessage.clear();
    return true;
}

bool Config::exists() const {
    std::error_code ec;
    return fs::exists(apollo::configPath(), ec);
}

// --- scalars ---------------------------------------------------------------

std::string Config::get(const std::string& key, const std::string& fallback) const {
    return props.getProperty(key, fallback);
}

void Config::set(const std::string& key, const std::string& value) {
    props.setProperty(key, value);
}

bool Config::unset(const std::string& key) {
    return props.removeProperty(key);
}

bool Config::isKnownKey(const std::string& key) const {
    static const std::set<std::string> topLevel = {
        "apollo.password", "apollo.root", "apollo.defaultConnection"};
    if (topLevel.count(key)) return true;

    if (key.rfind(kConnectionPrefix, 0) != 0) return false;
    const std::size_t dot = key.rfind('.');
    if (dot == std::string::npos) return false;

    static const std::set<std::string> fields = {"host", "user", "password", "key", "port",
                                                 "remoteDir"};
    return fields.count(key.substr(dot + 1)) > 0;
}

std::string Config::password() const {
    return props.getProperty("apollo.password", "");
}

void Config::setPassword(const std::string& value) {
    props.setProperty("apollo.password", value);
}

std::string Config::rootDir() const {
    const std::string configured = props.getProperty("apollo.root", "");
    if (!configured.empty()) return apollo::expandUser(configured).string();
    return (apollo::homeDir() / "Apollo").string();
}

void Config::setRootDir(const std::string& value) {
    props.setProperty("apollo.root", value);
}

std::string Config::defaultConnection() const {
    return props.getProperty("apollo.defaultConnection", "");
}

void Config::setDefaultConnection(const std::string& name) {
    props.setProperty("apollo.defaultConnection", name);
}

// --- connections -----------------------------------------------------------

std::vector<std::string> Config::connectionNames() const {
    std::set<std::string> names;
    for (const auto& key : props.keysWithPrefix(kConnectionPrefix)) {
        const std::string rest = key.substr(std::string(kConnectionPrefix).size());
        const std::size_t dot = rest.find('.');
        if (dot != std::string::npos) names.insert(rest.substr(0, dot));
    }
    return {names.begin(), names.end()};
}

std::optional<Connection> Config::connection(const std::string& name) const {
    const std::string host = props.getProperty(connectionKey(name, "host"), "");
    const std::string user = props.getProperty(connectionKey(name, "user"), "");
    if (host.empty() || user.empty()) return std::nullopt;

    Connection conn;
    conn.name = name;
    conn.host = host;
    conn.user = user;
    conn.password = props.getProperty(connectionKey(name, "password"), "");
    conn.keyPath = props.getProperty(connectionKey(name, "key"), "");
    conn.port = props.getProperty(connectionKey(name, "port"), "22");
    conn.remoteDir = props.getProperty(connectionKey(name, "remoteDir"), "~/APOLLO");
    return conn;
}

void Config::putConnection(const Connection& conn) {
    props.setProperty(connectionKey(conn.name, "host"), conn.host);
    props.setProperty(connectionKey(conn.name, "user"), conn.user);
    props.setProperty(connectionKey(conn.name, "port"), conn.port);
    props.setProperty(connectionKey(conn.name, "remoteDir"), conn.remoteDir);
    if (!conn.password.empty()) props.setProperty(connectionKey(conn.name, "password"), conn.password);
    if (!conn.keyPath.empty()) props.setProperty(connectionKey(conn.name, "key"), conn.keyPath);
}

bool Config::removeConnection(const std::string& name) {
    const bool removed = props.removePrefix(std::string(kConnectionPrefix) + name + ".") > 0;
    if (removed && defaultConnection() == name) props.removeProperty("apollo.defaultConnection");
    return removed;
}

std::optional<Connection> Config::resolveConnection(const std::string& requested,
                                                    std::string& error) const {
    const std::vector<std::string> names = connectionNames();

    if (!requested.empty()) {
        if (auto conn = connection(requested)) return conn;
        error = "No connection named '" + requested + "'.";
        if (names.empty()) {
            error += " Add one with: apollo config add <name> <user>@<host>";
        } else {
            error += " Known: ";
            for (std::size_t i = 0; i < names.size(); ++i) {
                if (i) error += ", ";
                error += names[i];
            }
        }
        return std::nullopt;
    }

    if (names.empty()) {
        error = "No connections configured. Add one with: apollo config add <name> <user>@<host>";
        return std::nullopt;
    }

    if (names.size() == 1) return connection(names.front());

    // More than one: fall back to the configured default, or make the user say.
    const std::string preferred = defaultConnection();
    if (!preferred.empty()) {
        if (auto conn = connection(preferred)) return conn;
    }

    error = "Multiple connections configured — name one: ";
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i) error += ", ";
        error += names[i];
    }
    error += ". Or set a default with: apollo config default <name>";
    return std::nullopt;
}

std::vector<std::pair<std::string, std::string>> Config::listing() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& [key, value] : props.all()) {
        out.emplace_back(key, isSecretKey(key) && !value.empty() ? "********" : value);
    }
    return out;
}
