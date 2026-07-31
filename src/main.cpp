#include <clocale>
#include <iostream>
#include <string>
#include <vector>

#include "cli/Cli.h"
#include "core/Config.h"
#include "core/Paths.h"
#include "ui/App.h"

int main(int argc, char** argv) {
    // wcwidth needs a UTF-8 locale to know that a CJK glyph is two columns
    // wide; without this every wide character would misalign the grid.
    std::setlocale(LC_CTYPE, "");

    const std::vector<std::string> args(argv + 1, argv + argc);

    apollo::Config config;
    config.load();

    // Only touch the filesystem for commands that are going to use a config.
    // `apollo --help` should be answerable without writing anything.
    const bool wantsConfig = args.empty() ||
                             (args[0] != "--version" && args[0] != "-v" && args[0] != "version" &&
                              args[0] != "--help" && args[0] != "-h" && args[0] != "help");
    bool freshInstall = false;
    if (wantsConfig && !config.exists()) {
        std::string note;
        freshInstall = apollo::cli::bootstrap(config, &note);
        if (!note.empty() && !args.empty() && args[0] != "config") {
            std::cout << note << "\n";
        }
        config.load();
    }

    apollo::cli::Outcome outcome = apollo::cli::dispatch(args, config);
    if (!outcome.launch) return outcome.code;

    if (freshInstall) outcome.options.runSetup = true;

    apollo::ui::App app(config, outcome.options);
    return app.run();
}
