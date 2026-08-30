#include <algorithm>
#include <clocale>
#include <iostream>
#include <string>
#include <vector>

#include "cli/Cli.h"
#include "core/Config.h"
#include "core/Paths.h"
#include "ui/App.h"

int main(int argc, char** argv) {
    // wcwidth needs a UTF-8 locale, or every wide character misaligns the grid.
    std::setlocale(LC_CTYPE, "");

    const std::vector<std::string> args(argv + 1, argv + argc);

    apollo::Config config;
    config.load();

    static const std::vector<std::string> readOnly = {
        "--version", "-v", "version", "--help", "-h", "help", "completions", "__complete",
    };
    const bool wantsConfig =
        args.empty() || std::find(readOnly.begin(), readOnly.end(), args[0]) == readOnly.end();
    if (wantsConfig && !config.exists()) {
        std::string note;
        apollo::cli::bootstrap(config, &note);
        if (!note.empty() && !args.empty() && args[0] != "config") {
            std::cout << note << "\n";
        }
        config.load();
    }

    apollo::cli::Outcome outcome = apollo::cli::dispatch(args, config);
    if (!outcome.launch) return outcome.code;

    // The wizard leaves a marker behind, so a first run that was interrupted
    // gets another chance rather than never being offered again.
    if (!apollo::paths::setupDone() && outcome.options.connect.empty() &&
        outcome.options.command.empty()) {
        outcome.options.runSetup = true;
    }

    apollo::ui::App app(config, outcome.options);
    return app.run();
}
