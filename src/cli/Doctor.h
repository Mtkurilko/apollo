#pragma once

#include "core/Config.h"

namespace apollo::cli {

// Checks the installation and prints what it finds. Returns the number of
// outright failures; warnings do not count.
int doctor(Config& config);

} // namespace apollo::cli
