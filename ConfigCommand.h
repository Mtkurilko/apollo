#ifndef CONFIG_COMMAND_H
#define CONFIG_COMMAND_H

#include <string>
#include <vector>

#include "Config.h"

struct ConfigCommandResult {
    std::vector<std::string> output;
    bool ok = true;
    bool changed = false; // the config file was rewritten
};

// Runs `config <subcommand> ...` (args excludes the leading "config") against
// `cfg`, saving it when something changed. One implementation backs both the
// shell CLI (`apollo config ...`) and the in-app terminal, so the two can never
// drift apart.
ConfigCommandResult runConfigCommand(const std::vector<std::string>& args, Config& cfg);

std::vector<std::string> configUsage();

#endif // CONFIG_COMMAND_H
