#include "cli/Doctor.h"

#include <unistd.h>

#include <filesystem>
#include <iostream>

#include "core/Paths.h"
#include "core/Process.h"
#include "net/Ssh.h"

namespace fs = std::filesystem;

namespace apollo::cli {
namespace {

int failures = 0;

void ok(const std::string& message) { std::cout << "  ok    " << message << "\n"; }
void warn(const std::string& message, const std::string& fix = "") {
    std::cout << "  warn  " << message << "\n";
    if (!fix.empty()) std::cout << "        " << fix << "\n";
}
void bad(const std::string& message, const std::string& fix = "") {
    std::cout << "  FAIL  " << message << "\n";
    if (!fix.empty()) std::cout << "        " << fix << "\n";
    ++failures;
}

} // namespace

int doctor(Config& config) {
    failures = 0;
    std::cout << "apollo " APOLLO_VERSION " doctor\n\n";

    // --- config ---
    if (config.exists()) {
        ok("config " + paths::contractUser(config.path()));

        std::error_code ec;
        const auto permissions = fs::status(config.path(), ec).permissions();
        const bool othersCanRead =
            (permissions & (fs::perms::group_read | fs::perms::others_read)) != fs::perms::none;
        bool holdsSecret = false;
        for (const auto& conn : config.connections()) holdsSecret |= !conn.password.empty();
        if (othersCanRead && holdsSecret) {
            bad("the config holds a password and is readable by others",
                "chmod 600 " + config.path().string());
        }
    } else {
        warn("no config yet", "apollo setup");
    }

    if (config.issues().empty()) {
        ok("the config has no problems");
    } else {
        for (const auto& issue : config.issues()) warn(issue);
    }

    const std::string workspace = paths::expandUser(config.general().workspace);
    std::error_code ec;
    if (fs::is_directory(workspace, ec)) {
        ok("workspace " + config.general().workspace +
           (workspace == config.general().workspace ? "" : "  (" + workspace + ")"));
    }
    else bad("workspace does not exist: " + config.general().workspace,
             "apollo config set general.workspace ~");

    // --- terminal ---
    if (const char* term = std::getenv("TERM"); term && *term) {
        ok(std::string("TERM is ") + term);
    } else {
        warn("TERM is not set; colors may be wrong");
    }
    if (::isatty(STDOUT_FILENO)) ok("running on a terminal");
    else warn("standard output is not a terminal; apollo needs one to draw");

    const char* colorterm = std::getenv("COLORTERM");
    if (colorterm && std::string(colorterm).find("truecolor") != std::string::npos) {
        ok("24 bit color available");
    } else {
        warn("COLORTERM does not advertise truecolor; themes will be approximated",
             "most modern terminals set this themselves");
    }

    // --- shell integration ---
    const fs::path snippet = paths::configDir() / "shell-integration.sh";
    if (config.terminal().shellIntegration) {
        if (fs::exists(snippet, ec)) ok("shell integration installed");
        else warn("shell integration is on but not installed",
                  "apollo setup, and answer yes at the last step");
    }

    // --- commands ---
    if (fs::is_directory(paths::commandsDir(), ec)) {
        int count = 0;
        for (const auto& entry : fs::directory_iterator(paths::commandsDir(), ec)) {
            if (entry.is_regular_file(ec) && ::access(entry.path().c_str(), X_OK) == 0) ++count;
        }
        ok(std::to_string(count) + " command(s) in " +
           paths::contractUser(paths::commandsDir()));
    } else {
        ok("no custom commands directory yet (" +
           paths::contractUser(paths::commandsDir()) + ")");
    }

    // --- connections ---
    const auto& connections = config.connections();
    if (connections.empty()) {
        ok("no SSH destinations configured (local only)");
    } else {
        if (!process::which("ssh")) bad("ssh is not on PATH");

        bool needsSshpass = false;
        for (const auto& conn : connections) {
            if (!conn.valid()) {
                bad("connection '" + conn.name + "' is missing a host or a user");
                continue;
            }
            if (!conn.keyPath.empty()) {
                if (fs::exists(paths::expandUser(conn.keyPath), ec)) {
                    ok("connection '" + conn.name + "' -> " + conn.describe() + " (key)");
                } else {
                    bad("connection '" + conn.name + "' points at a missing key: " + conn.keyPath,
                        "apollo config set connection." + conn.name + ".key ~/.ssh/id_ed25519");
                }
            } else if (!conn.password.empty()) {
                needsSshpass = true;
                warn("connection '" + conn.name + "' uses a password",
                     "a key is safer: apollo config set connection." + conn.name +
                         ".key ~/.ssh/id_ed25519");
            } else {
                warn("connection '" + conn.name + "' has no credentials yet",
                     "apollo config set connection." + conn.name + ".key ~/.ssh/id_ed25519");
            }
        }
        if (needsSshpass && !process::which("sshpass")) {
            bad("a connection uses a password but sshpass is not installed",
                "brew install sshpass — or switch that connection to a key");
        }

        if (connections.size() > 1 && config.general().defaultConnection.empty()) {
            warn("several destinations and no default; `apollo connect` will ask for a name",
                 "apollo config default " + connections.front().name);
        }
    }

    std::cout << "\n"
              << (failures == 0 ? "Nothing broken.\n"
                                : std::to_string(failures) + " problem(s) to fix.\n");
    return failures;
}

} // namespace apollo::cli
