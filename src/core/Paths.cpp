#include "core/Paths.h"

#include <pwd.h>
#include <unistd.h>

#include <cstdlib>
#include <system_error>

namespace fs = std::filesystem;

namespace apollo::paths {
namespace {

const char* envOrNull(const char* name) {
    const char* value = std::getenv(name);
    return (value && *value) ? value : nullptr;
}

} // namespace

fs::path home() {
    if (const char* h = envOrNull("HOME")) return h;
    // HOME can be missing under launchd or a bare sudo; passwd always has it.
    if (const passwd* pw = getpwuid(getuid()); pw && pw->pw_dir) return pw->pw_dir;
    return fs::current_path();
}

fs::path configDir() {
    if (const char* dir = envOrNull("APOLLO_CONFIG_DIR")) return dir;
    return home() / ".apollo";
}

fs::path configFile()   { return configDir() / "apollo.conf"; }
fs::path commandsDir()  { return configDir() / "commands"; }
fs::path themesDir()    { return configDir() / "themes"; }
fs::path stateDir()     { return configDir() / "state"; }

fs::path legacyPropertiesFile() { return configDir() / "config.properties"; }

std::string expandUser(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    if (path.size() == 1) return home().string();
    if (path[1] != '/') return path; // ~user is somebody else's home; leave it
    return (home() / path.substr(2)).string();
}

std::string contractUser(const fs::path& path) {
    const std::string text = path.string();
    const std::string h = home().string();
    if (h.empty() || text.rfind(h, 0) != 0) return text;
    if (text.size() == h.size()) return "~";
    if (text[h.size()] != '/') return text;
    return "~" + text.substr(h.size());
}

bool ensureDir(const fs::path& dir, std::string* error) {
    std::error_code ec;
    if (fs::exists(dir, ec)) return fs::is_directory(dir, ec);

    fs::create_directories(dir, ec);
    if (ec) {
        if (error) *error = "cannot create " + dir.string() + ": " + ec.message();
        return false;
    }
    // The config can hold SSH passwords, so nobody else gets to look.
    fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);
    return true;
}

} // namespace apollo::paths
