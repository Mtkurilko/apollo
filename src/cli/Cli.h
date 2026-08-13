// The command line.
//
// Everything here either answers on the terminal and exits, or hands back a
// set of options for the full screen application to start with.
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

// Creates ~/.apollo and a starter config when there is none, and converts a
// pre-0.3 properties file if one is lying around. Returns true when it wrote
// a config, which is the signal to run the wizard.
bool bootstrap(Config& config, std::string* note = nullptr);

void printUsage();

} // namespace apollo::cli
