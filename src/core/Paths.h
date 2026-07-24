// Where Apollo keeps its things.
//
// Everything user-owned lives under ~/.apollo. Nothing is resolved relative to
// the executable or the build directory, so a binary copied anywhere still
// works and `apollo` behaves the same however it was installed.
#pragma once

#include <filesystem>
#include <string>

namespace apollo::paths {

// $HOME, or the passwd entry if HOME is unset.
std::filesystem::path home();

// ~/.apollo, overridable with $APOLLO_CONFIG_DIR (used by the tests).
std::filesystem::path configDir();

// ~/.apollo/apollo.conf — the one file a user edits.
std::filesystem::path configFile();

// ~/.apollo/commands — executables here become `apollo <name>`.
std::filesystem::path commandsDir();

// ~/.apollo/themes — *.conf files here become selectable themes.
std::filesystem::path themesDir();

// ~/.apollo/state — history and other things Apollo owns, not the user.
std::filesystem::path stateDir();

// Pre-0.3 config files, read once to migrate and then left alone.
std::filesystem::path legacyPropertiesFile();

// Replaces a leading ~ with the home directory. Leaves everything else alone.
std::string expandUser(const std::string& path);

// The inverse, for display: /Users/you/src -> ~/src.
std::string contractUser(const std::filesystem::path& path);

// Creates a directory the current user alone can read, if it does not exist.
bool ensureDir(const std::filesystem::path& dir, std::string* error = nullptr);

} // namespace apollo::paths
