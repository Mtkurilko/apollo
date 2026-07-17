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
    fs::path p = fs::canonical(fs::path(buf.data()), ec);
    if (ec) return {};
    return p.parent_path();
}

fs::path resolveRoot() {
    if (const char* env = std::getenv("APOLLO_HOME")) {
        fs::path p(env);
        if (looksLikeRoot(p)) return p;
    }

    // The executable may live inside the source tree, or in a build/ subdir.
    for (fs::path p = executableDir(); !p.empty() && p != p.root_path(); p = p.parent_path()) {
        if (looksLikeRoot(p)) return p;
    }

    fs::path baked(APOLLO_DEFAULT_HOME);
    if (!baked.empty() && looksLikeRoot(baked)) return baked;

    std::error_code ec;
    fs::path cwd = fs::current_path(ec);
    return ec ? fs::path(".") : cwd;
}

} // namespace

const fs::path& appRoot() {
    static const fs::path root = resolveRoot();
    return root;
}

fs::path assetPath(const std::string& name) {
    return appRoot() / "assets" / name;
}

fs::path configPath() {
    return appRoot() / "apollo.properties";
}

} // namespace apollo
