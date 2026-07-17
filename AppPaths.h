#ifndef APP_PATHS_H
#define APP_PATHS_H

#include <filesystem>
#include <string>

namespace apollo {

// Directory containing assets/ and apollo.properties. Resolved once, in order:
//   $APOLLO_HOME, a parent of the executable, the compiled-in default, the cwd.
const std::filesystem::path& appRoot();

std::filesystem::path assetPath(const std::string& name);
std::filesystem::path configPath();

} // namespace apollo

#endif // APP_PATHS_H
