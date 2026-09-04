// Where Apollo keeps things. Everything of yours lives under ~/.apollo.
#pragma once

#include <filesystem>
#include <string>

namespace apollo::paths {

// $HOME, or the passwd entry if HOME isn't set.
std::filesystem::path home();

std::filesystem::path configDir();

std::filesystem::path configFile();

std::filesystem::path commandsDir();

std::filesystem::path themesDir();

std::filesystem::path legacyPropertiesFile();

// Written once the wizard has actually run. An interrupted first run gets
// offered again instead of being skipped forever.
std::filesystem::path setupMarker();
bool setupDone();
void markSetupDone();

std::string expandUser(const std::string& path);

std::string contractUser(const std::filesystem::path& path);

bool ensureDir(const std::filesystem::path& dir, std::string* error = nullptr);

} // namespace apollo::paths
