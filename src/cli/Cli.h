// The command line.
#pragma once

#include <string>
#include <vector>

#include "core/Config.h"
#include "ui/App.h"

namespace apollo::cli {

struct Outcome {
    int code = 0;
    bool launch = false;
    ui::App::Options options;
};

Outcome dispatch(const std::vector<std::string>& args, Config& config);

bool bootstrap(Config& config, std::string* note = nullptr);

void printUsage();

} // namespace apollo::cli
