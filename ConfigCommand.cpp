#include "ConfigCommand.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

#include "AppPaths.h"

namespace fs = std::filesystem;

std::vector<std::string> configUsage() {
    return {
        "apollo config — view and edit Apollo's configuration",
        "",
        "  apollo config list                     show every setting",
        "  apollo config get <key>                read one setting",
        "  apollo config set <key> <value>        write one setting",
        "  apollo config unset <key>              clear one setting",
        "  apollo config path                     print the config file location",
        "  apollo config edit                     open the config file in an editor",
        "",
        "Connections:",
        "  apollo config connections              list SSH connections",
        "  apollo config add <name> <user>@<host> [port]",
        "  apollo config remove <name>",
        "  apollo config default [<name>]         show or set the default connection",
        "",
        "Keys:",
        "  apollo.password                        password for the Apollo boot screen",
        "  apollo.root                            directory Apollo opens in",
        "  apollo.defaultConnection               connection used by a bare `apollo connect`",
        "  connection.<name>.host                 hostname or IP",
        "  connection.<name>.user                 remote username",
        "  connection.<name>.key                  private key path (preferred)",
        "  connection.<name>.password             password, if no key is set",
        "  connection.<name>.port                 SSH port (default 22)",
        "  connection.<name>.remoteDir            directory to open remotely (default ~/APOLLO)",
    };
}

namespace {

ConfigCommandResult fail(std::vector<std::string> lines) {
    ConfigCommandResult r;
    r.output = std::move(lines);
    r.ok = false;
    return r;
}

// Splits "user@host" into its two halves.
bool splitUserHost(const std::string& spec, std::string& user, std::string& host) {
    const std::size_t at = spec.find('@');
    if (at == std::string::npos || at == 0 || at + 1 >= spec.size()) return false;
    user = spec.substr(0, at);
    host = spec.substr(at + 1);
    return true;
}

} // namespace

ConfigCommandResult runConfigCommand(const std::vector<std::string>& args, Config& cfg) {
    ConfigCommandResult result;

    if (args.empty() || args[0] == "help" || args[0] == "--help") {
        result.output = configUsage();
        return result;
    }

    const std::string& sub = args[0];

    // --- read-only ---------------------------------------------------------

    if (sub == "path") {
        result.output.push_back(apollo::configPath().string());
        return result;
    }

    if (sub == "list") {
        const auto entries = cfg.listing();
        if (entries.empty()) {
            result.output.push_back("No configuration yet. Run `apollo setup` to get started.");
            return result;
        }
        for (const auto& [key, value] : entries) result.output.push_back(key + " = " + value);
        return result;
    }

    if (sub == "connections") {
        const auto names = cfg.connectionNames();
        if (names.empty()) {
            result.output.push_back("No connections configured.");
            result.output.push_back("Add one with: apollo config add <name> <user>@<host>");
            return result;
        }
        const std::string preferred = cfg.defaultConnection();

        // Pad the names so the destinations line up in a column.
        std::size_t width = 0;
        for (const auto& name : names) width = std::max(width, name.size());

        for (const auto& name : names) {
            const auto conn = cfg.connection(name);
            std::string line = "  " + name + std::string(width - name.size() + 2, ' ') +
                               (conn ? conn->label() : "(incomplete)");
            if (conn && conn->port != "22") line += ":" + conn->port;
            if (conn && !conn->keyPath.empty()) line += "  [key]";
            else if (conn && !conn->password.empty()) line += "  [password]";
            else if (conn) line += "  [no auth set]";
            if (name == preferred) line += "  (default)";
            result.output.push_back(line);
        }
        return result;
    }

    if (sub == "get") {
        if (args.size() < 2) return fail({"Usage: apollo config get <key>"});
        const std::string& key = args[1];
        if (!cfg.isKnownKey(key)) {
            return fail({"Unknown key: " + key, "Run `apollo config list` to see what is set."});
        }
        result.output.push_back(cfg.get(key, "(unset)"));
        return result;
    }

    if (sub == "edit") {
        const std::string path = apollo::configPath().string();
        const char* editor = std::getenv("EDITOR");
        // No EDITOR set: hand it to the desktop rather than failing.
        const std::string cmd = editor && *editor
                                    ? std::string(editor) + " \"" + path + "\""
                                    : "open \"" + path + "\"";
        if (std::system(cmd.c_str()) != 0) return fail({"Could not open " + path});
        result.output.push_back("Opened " + path);
        return result;
    }

    // --- mutating ----------------------------------------------------------

    if (sub == "set") {
        if (args.size() < 3) return fail({"Usage: apollo config set <key> <value>"});
        const std::string& key = args[1];
        if (!cfg.isKnownKey(key)) {
            return fail({"Unknown key: " + key, "Run `apollo config` to see the supported keys."});
        }
        // Re-join, so values containing spaces survive tokenizing.
        std::string value = args[2];
        for (std::size_t i = 3; i < args.size(); ++i) value += " " + args[i];

        cfg.set(key, value);
        result.changed = true;
        result.output.push_back(key + " = " + (key.find("password") != std::string::npos ? "********" : value));
    } else if (sub == "unset") {
        if (args.size() < 2) return fail({"Usage: apollo config unset <key>"});
        if (!cfg.unset(args[1])) return fail({"Not set: " + args[1]});
        result.changed = true;
        result.output.push_back("Unset " + args[1]);
    } else if (sub == "add") {
        if (args.size() < 3) {
            return fail({"Usage: apollo config add <name> <user>@<host> [port]",
                         "Example: apollo config add lab alice@10.0.0.5"});
        }
        const std::string& name = args[1];
        if (!Config::isValidConnectionName(name)) {
            return fail({"Invalid connection name: " + name,
                         "Use letters, digits, hyphens and underscores only."});
        }
        if (cfg.connection(name)) {
            return fail({"A connection named '" + name + "' already exists.",
                         "Change it with `apollo config set connection." + name + ".host <value>`,",
                         "or remove it first with `apollo config remove " + name + "`."});
        }

        Connection conn;
        conn.name = name;
        if (!splitUserHost(args[2], conn.user, conn.host)) {
            return fail({"Expected <user>@<host>, got: " + args[2]});
        }
        if (args.size() > 3) conn.port = args[3];

        cfg.putConnection(conn);
        // First connection becomes the default, so `apollo connect` just works.
        if (cfg.connectionNames().size() == 1) cfg.setDefaultConnection(name);

        result.changed = true;
        result.output.push_back("Added connection '" + name + "' -> " + conn.label() +
                                (conn.port == "22" ? "" : ":" + conn.port));
        result.output.push_back("");
        result.output.push_back("Set up authentication with one of:");
        result.output.push_back("  apollo config set connection." + name + ".key ~/.ssh/id_ed25519");
        result.output.push_back("  apollo config set connection." + name + ".password <password>");
    } else if (sub == "remove") {
        if (args.size() < 2) return fail({"Usage: apollo config remove <name>"});
        if (!cfg.removeConnection(args[1])) return fail({"No connection named '" + args[1] + "'."});
        result.changed = true;
        result.output.push_back("Removed connection '" + args[1] + "'");
    } else if (sub == "default") {
        if (args.size() < 2) {
            const std::string preferred = cfg.defaultConnection();
            result.output.push_back(preferred.empty() ? "(no default connection set)" : preferred);
            return result;
        }
        if (!cfg.connection(args[1])) return fail({"No connection named '" + args[1] + "'."});
        cfg.setDefaultConnection(args[1]);
        result.changed = true;
        result.output.push_back("Default connection is now '" + args[1] + "'");
    } else {
        return fail({"Unknown config subcommand: " + sub, "Run `apollo config` for usage."});
    }

    if (result.changed && !cfg.save()) {
        return fail({"ERROR: " + cfg.lastError()});
    }
    return result;
}
