// Where Apollo keeps its files. Everything user-owned is under ~/.apollo.
#pragma once

#include <filesystem>
#include <string>

namespace apollo::paths {

// $HOME, or the passwd entry if HOME is unset.
std::filesystem::path home();

std::filesystem::path configDir();

std::filesystem::path configFile();

std::filesystem::path commandsDir();

std::filesystem::path themesDir();

std::filesystem::path stateDir();

std::filesystem::path legacyPropertiesFile();

// Written once the wizard has been through, so an interrupted first run does
// not mean it is never offered again.
std::filesystem::path setupMarker();
bool setupDone();
void markSetupDone();

std::string expandUser(const std::string& path);

std::string contractUser(const std::filesystem::path& path);

bool ensureDir(const std::filesystem::path& dir, std::string* error = nullptr);

} // namespace apollo::paths
