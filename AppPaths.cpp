#include "AppPaths.h"

#include <cstdlib>
#include <vector>
#include <mach-o/dyld.h>

#ifndef APOLLO_DEFAULT_HOME
#define APOLLO_DEFAULT_HOME ""
#endif

namespace fs = std::filesystem;

namespace apollo {

namespace {

bool looksLikeRoot(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p / "assets" / "GFSNeohellenic-Regular.ttf", ec);
}

fs::path executableDir() {
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size); // asks for the required buffer size
    std::vector<char> buf(size + 1, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};

    std::error_code ec;
    const fs::path p = fs::canonical(fs::path(buf.data()), ec);
    if (ec) return {};
    return p.parent_path();
}

fs::path resolveRoot() {
    if (const char* env = std::getenv("APOLLO_HOME")) {
        const fs::path p = expandUser(env);
        if (looksLikeRoot(p)) return p;
    }

    const fs::path exeDir = executableDir();

    // Running straight out of the source tree (or a build/ subdir of it).
    for (fs::path p = exeDir; !p.empty() && p != p.root_path(); p = p.parent_path()) {
        if (looksLikeRoot(p)) return p;
    }

    // Installed layout: <prefix>/bin/apollo alongside <prefix>/share/apollo.
    if (!exeDir.empty()) {
        const fs::path shared = exeDir.parent_path() / "share" / "apollo";
        if (looksLikeRoot(shared)) return shared;
    }

    const fs::path baked(APOLLO_DEFAULT_HOME);
    if (!baked.empty() && looksLikeRoot(baked)) return baked;

    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    return ec ? fs::path(".") : cwd;
}

fs::path resolveConfigDir() {
    if (const char* env = std::getenv("APOLLO_CONFIG_DIR")) {
        if (*env) return expandUser(env);
    }
    return homeDir() / ".apollo";
}

} // namespace

fs::path homeDir() {
    if (const char* home = std::getenv("HOME")) {
        if (*home) return fs::path(home);
    }
    return fs::path("/tmp");
}

fs::path expandUser(const std::string& path) {
    if (path.empty() || path[0] != '~') return fs::path(path);
    if (path.size() == 1) return homeDir();
    if (path[1] != '/') return fs::path(path); // ~otheruser is not supported
    return homeDir() / path.substr(2);
}

const fs::path& appRoot() {
    static const fs::path root = resolveRoot();
    return root;
}

fs::path assetPath(const std::string& name) {
    return appRoot() / "assets" / name;
}

const fs::path& configDir() {
    static const fs::path dir = resolveConfigDir();
    return dir;
}

fs::path configPath() {
    return configDir() / "config.properties";
}

fs::path legacyConfigPath() {
    return appRoot() / "apollo.properties";
}

bool ensureConfigDir() {
    std::error_code ec;
    fs::create_directories(configDir(), ec);
    if (ec) return false;
    // The file holds SSH passwords; keep it out of other accounts' reach.
    fs::permissions(configDir(), fs::perms::owner_all, fs::perm_options::replace, ec);
    return true;
}

} // namespace apollo
