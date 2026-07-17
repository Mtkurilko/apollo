#ifndef APP_PATHS_H
#define APP_PATHS_H

#include <filesystem>
#include <string>

namespace apollo {

// Directory containing assets/. Resolved once, in order:
//   $APOLLO_HOME, a parent of the executable, <exe>/../share/apollo,
//   the compile-time default, the cwd.
const std::filesystem::path& appRoot();
std::filesystem::path assetPath(const std::string& name);

// Per-user configuration, so an installed Apollo serves every account on the
// machine: $APOLLO_CONFIG_DIR, else ~/.apollo.
const std::filesystem::path& configDir();
std::filesystem::path configPath();
bool ensureConfigDir();

// Pre-0.2 layout: apollo.properties sitting beside the sources.
std::filesystem::path legacyConfigPath();

std::filesystem::path homeDir();

// Replaces a leading ~ with the user's home directory.
std::filesystem::path expandUser(const std::string& path);

} // namespace apollo

#endif // APP_PATHS_H
