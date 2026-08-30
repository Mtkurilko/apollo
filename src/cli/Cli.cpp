#include "cli/Cli.h"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>

#include "cli/Doctor.h"
#include "core/Commands.h"
#include "core/Control.h"
#include "core/Paths.h"
#include "core/Process.h"

namespace fs = std::filesystem;

namespace apollo::cli {
namespace {

void line(const std::string& left, const std::string& right) {
    std::cout << "  " << std::left << std::setw(28) << left << right << "\n";
}

// Mask on the key's suffix, not a list, so a new secret setting cannot be forgotten.
bool isSecret(const std::string& path) {
    return path.size() >= 8 && path.compare(path.size() - 8, 8, "password") == 0;
}

std::string maskIfSecret(const std::string& path, const std::string& value) {
    return isSecret(path) && !value.empty() ? "********" : value;
}

int configUsage() {
    std::cout << "apollo config — Apollo's settings\n\n"
                 "  apollo config                      open the settings editor\n"
                 "  apollo config list                 print every setting\n"
                 "  apollo config get <key>\n"
                 "  apollo config set <key> <value>\n"
                 "  apollo config unset <key>\n"
                 "  apollo config path                 where the file lives\n"
                 "  apollo config edit                 open it in $EDITOR\n"
                 "  apollo config check                report any problems, and exit non-zero\n"
                 "\n"
                 "SSH destinations:\n"
                 "  apollo config connections\n"
                 "  apollo config add <name> <user>@<host> [port]\n"
                 "  apollo config remove <name>\n"
                 "  apollo config default [<name>]\n"
                 "\n"
                 "Keys and commands:\n"
                 "  apollo config bind <mods>, <key>, <action> [, <arg>]\n"
                 "  apollo config unbind <mods>, <key>\n"
                 "  apollo config binds                print the key table\n"
                 "  apollo config actions              print everything a key can do\n"
                 "\n"
                 "Run `apollo config list` to see the settings and their current values.\n";
    return 0;
}

int listSettings(const Config& config) {
    std::string section;
    for (const auto& setting : Config::schema()) {
        const std::string here = setting.path.substr(0, setting.path.find('.'));
        if (here != section) {
            std::cout << (section.empty() ? "" : "\n") << here << "\n";
            section = here;
        }
        const std::string value = config.valueOf(setting);
        const bool isDefault = value == setting.defaultValue;
        std::cout << "  " << std::left << std::setw(26) << setting.path.substr(here.size() + 1)
                  << std::setw(16) << maskIfSecret(setting.path, value.empty() ? "(unset)" : value)
                  << (isDefault ? "" : "(changed)") << "\n";
    }

    if (!config.connections().empty()) {
        std::cout << "\nconnections\n";
        for (const auto& conn : config.connections()) {
            std::cout << "  " << std::left << std::setw(26) << conn.name << conn.describe();
            if (!conn.keyPath.empty()) std::cout << "  key " << conn.keyPath;
            else if (!conn.password.empty()) std::cout << "  password ********";
            if (conn.name == config.general().defaultConnection) std::cout << "  (default)";
            std::cout << "\n";
        }
    }
    return 0;
}

int listConnections(const Config& config) {
    const auto& connections = config.connections();
    if (connections.empty()) {
        std::cout << "No SSH destinations configured.\n"
                     "  apollo config add <name> <user>@<host>\n";
        return 0;
    }

    std::size_t width = 4;
    for (const auto& conn : connections) width = std::max(width, conn.name.size());

    for (const auto& conn : connections) {
        std::cout << "  " << std::left << std::setw(static_cast<int>(width) + 2) << conn.name
                  << std::setw(30) << conn.describe();
        if (!conn.keyPath.empty()) std::cout << "key";
        else if (!conn.password.empty()) std::cout << "password";
        else std::cout << "no credentials";
        if (conn.name == config.general().defaultConnection) std::cout << "  (default)";
        std::cout << "\n";
    }

    if (connections.size() == 1) {
        std::cout << "\n`apollo connect` uses " << connections.front().name
                  << " with no argument.\n";
    } else if (config.general().defaultConnection.empty()) {
        std::cout << "\nSeveral destinations, so name one: apollo connect "
                  << connections.front().name << "\n"
                  << "Or pick a default: apollo config default " << connections.front().name
                  << "\n";
    }
    return 0;
}

int runConfig(std::vector<std::string> args, Config& config, Outcome& outcome) {
    if (args.empty()) {
        outcome.launch = true;
        outcome.options.openConfig = true;
        return 0;
    }

    const std::string sub = args[0];
    args.erase(args.begin());

    const auto need = [&](std::size_t count, const std::string& usage) {
        if (args.size() >= count) return true;
        std::cerr << "Usage: apollo config " << usage << "\n";
        return false;
    };

    if (sub == "help" || sub == "--help" || sub == "-h") return configUsage();
    if (sub == "path") { std::cout << config.path().string() << "\n"; return 0; }
    if (sub == "list" || sub == "ls") return listSettings(config);
    if (sub == "connections") return listConnections(config);

    if (sub == "check") {
        if (config.issues().empty()) {
            std::cout << "No problems in " << paths::contractUser(config.path()) << "\n";
            return 0;
        }
        for (const auto& issue : config.issues()) std::cerr << "  " << issue << "\n";
        return 1;
    }

    if (sub == "actions") {
        for (const auto& action : knownActions()) {
            line(action.name + (action.takesArgument ? " <arg>" : ""), action.summary);
        }
        return 0;
    }

    if (sub == "binds") {
        for (const auto& bind : config.binds()) {
            const ActionInfo* action = findAction(bind.action);
            line(bind.chord.describe(),
                 bind.describeAction() + (action ? "   " + action->summary : ""));
        }
        std::cout << "\nLeader is " << config.general().leader.describe() << ".\n";
        return 0;
    }

    if (sub == "get") {
        if (!need(1, "get <key>")) return 1;
        const auto value = config.file().get(args[0]);
        if (!value) {
            if (const Config::Setting* setting = Config::setting(args[0])) {
                std::cout << setting->defaultValue << "\n";
                return 0;
            }
            std::cerr << "Not set, and not a setting Apollo knows: " << args[0] << "\n";
            return 1;
        }
        std::cout << maskIfSecret(args[0], *value) << "\n";
        return 0;
    }

    if (sub == "set") {
        if (!need(2, "set <key> <value>")) return 1;
        std::string value = args[1];
        for (std::size_t i = 2; i < args.size(); ++i) value += " " + args[i];

        std::string error;
        if (!config.set(args[0], value, &error)) { std::cerr << error << "\n"; return 1; }
        if (!config.save(&error)) { std::cerr << error << "\n"; return 1; }
        std::cout << args[0] << " = " << maskIfSecret(args[0], value) << "\n";

        for (const auto& issue : config.issues()) {
            if (issue.find(value) != std::string::npos) std::cerr << "  warning: " << issue << "\n";
        }
        return 0;
    }

    if (sub == "unset") {
        if (!need(1, "unset <key>")) return 1;
        if (!config.unset(args[0])) {
            if (config.file().get(args[0])) {
                std::cerr << args[0] << " comes from a file brought in with `source`.\n"
                          << "Edit that file, or override it here:\n"
                          << "  apollo config set " << args[0] << " <value>\n";
            } else {
                std::cerr << "Not set: " << args[0] << "\n";
            }
            return 1;
        }
        std::string error;
        if (!config.save(&error)) { std::cerr << error << "\n"; return 1; }
        std::cout << "unset " << args[0] << "\n";
        return 0;
    }

    if (sub == "edit") {
        const char* fromEnv = std::getenv("EDITOR");
        const std::string editor = !config.general().editor.empty() ? config.general().editor
                                   : (fromEnv && *fromEnv)         ? fromEnv
                                                                   : "";
        if (editor.empty()) {
            std::cerr << "Set $EDITOR, or `apollo config set general.editor <path>`.\n";
            return 1;
        }
        std::vector<char*> argv = {const_cast<char*>(editor.c_str()),
                                   const_cast<char*>(config.path().c_str()), nullptr};
        ::execvp(argv[0], argv.data());
        std::cerr << "Could not run " << editor << "\n";
        return 1;
    }

    if (sub == "add") {
        if (!need(2, "add <name> <user>@<host> [port]")) return 1;

        Connection conn;
        conn.name = args[0];
        const std::string address = args[1];
        const auto at = address.find('@');
        if (at == std::string::npos || at == 0 || at + 1 >= address.size()) {
            std::cerr << "Expected <user>@<host>, got: " << address << "\n";
            return 1;
        }
        conn.user = address.substr(0, at);
        conn.host = address.substr(at + 1);
        if (args.size() > 2) conn.port = ConfigFile::asInt(args[2], 22);

        std::string error;
        if (!config.addConnection(conn, &error)) { std::cerr << error << "\n"; return 1; }
        if (!config.save(&error)) { std::cerr << error << "\n"; return 1; }

        std::cout << "Added " << conn.name << " -> " << conn.describe() << "\n\n"
                  << "Now give it a credential:\n"
                  << "  apollo config set connection." << conn.name << ".key ~/.ssh/id_ed25519\n"
                  << "or, if there is no key to use:\n"
                  << "  apollo config set connection." << conn.name << ".password <password>\n";
        return 0;
    }

    if (sub == "remove" || sub == "rm") {
        if (!need(1, "remove <name>")) return 1;
        if (!config.removeConnection(args[0])) {
            std::cerr << "No connection named '" << args[0] << "'.\n";
            return 1;
        }
        std::string error;
        if (!config.save(&error)) { std::cerr << error << "\n"; return 1; }
        std::cout << "Removed " << args[0] << "\n";
        return 0;
    }

    if (sub == "default") {
        if (args.empty()) {
            const std::string current = config.general().defaultConnection;
            std::cout << (current.empty() ? "(no default set)" : current) << "\n";
            return 0;
        }
        std::string error;
        if (!config.setDefaultConnection(args[0], &error)) {
            std::cerr << error << "\n";
            return 1;
        }
        if (!config.save(&error)) { std::cerr << error << "\n"; return 1; }
        if (args[0].empty()) std::cout << "Cleared the default destination.\n";
        else std::cout << "Default destination is now " << args[0] << "\n";
        return 0;
    }

    if (sub == "bind") {
        if (!need(1, "bind <mods>, <key>, <action> [, <arg>]")) return 1;
        std::string spec = args[0];
        for (std::size_t i = 1; i < args.size(); ++i) spec += " " + args[i];

        std::string error;
        if (!config.addBind(spec, &error)) { std::cerr << error << "\n"; return 1; }
        if (!config.save(&error)) { std::cerr << error << "\n"; return 1; }
        std::cout << "bound " << spec << "\n";
        return 0;
    }

    if (sub == "unbind") {
        if (!need(1, "unbind <mods>, <key>")) return 1;
        std::string spec = args[0];
        for (std::size_t i = 1; i < args.size(); ++i) spec += " " + args[i];
        if (!config.removeBind(spec)) { std::cerr << "Not a key Apollo knows: " << spec << "\n"; return 1; }
        std::string error;
        if (!config.save(&error)) { std::cerr << error << "\n"; return 1; }
        std::cout << "unbound " << spec << "\n";
        return 0;
    }

    std::cerr << "Unknown: apollo config " << sub << "\n";
    configUsage();
    return 1;
}

int listCommands(const Config& config) {
    CommandRegistry registry;
    for (const auto& declared : config.commands()) {
        registry.addDeclared(declared.name, declared.exec, declared.summary,
                             "apollo.conf:" + std::to_string(declared.line + 1));
    }
    registry.scanDirectory(paths::commandsDir());

    if (registry.all().empty()) {
        std::cout << "No commands of your own yet.\n\n"
                     "Add one to " << paths::contractUser(paths::configFile()) << ":\n"
                     "  command = deploy, ./scripts/deploy.sh, \"Ship the current branch\"\n\n"
                     "Or drop an executable into "
                  << paths::contractUser(paths::commandsDir()) << " and it becomes\n"
                     "`apollo <its name>` with nothing else to do.\n";
        return 0;
    }

    for (const auto& command : registry.all()) {
        std::string summary = command.summary;
        if (summary.size() > 40) summary = summary.substr(0, 37) + "...";
        std::cout << "  " << std::left << std::setw(16) << command.name << std::setw(42)
                  << summary << command.origin << "\n";
    }
    return 0;
}

// --- talking to the Apollo we are already inside --------------------------- Without this,
// `apollo` typed in Apollo's own terminal starts a second Apollo nested in the first.

std::string controlMessageFor(const std::vector<std::string>& args) {
    if (args.empty()) return "home";

    const std::string& first = args[0];
    const std::string rest = args.size() > 1 ? args[1] : "";

    if (first == "quit" || first == "exit") return "quit";
    if (first == "home") return "home";
    if (first == "setup" || first == "onboard") return "setup";
    if (first == "reload") return "reload";
    if (first == "new-tab" || first == "tab") return "new-tab";
    if (first == "connect") return rest.empty() ? "connect" : "connect " + rest;
    if (first == "disconnect") return "disconnect";
    if (first == "config" && args.size() == 1) return "config";
    if (first == "help" && args.size() == 1) return "help";

    std::error_code ec;
    const fs::path directory = paths::expandUser(first);
    if (fs::is_directory(directory, ec)) {
        return "open " + fs::weakly_canonical(directory, ec).string();
    }
    return "";
}


std::vector<std::string> completionsFor(const std::vector<std::string>& words,
                                        const Config& config) {
    const auto commandNames = [&] {
        CommandRegistry registry;
        for (const auto& declared : config.commands()) {
            registry.addDeclared(declared.name, declared.exec, declared.summary, "apollo.conf");
        }
        registry.scanDirectory(paths::commandsDir());
        return registry.names();
    };
    const auto connectionNames = [&] {
        std::vector<std::string> names;
        for (const auto& conn : config.connections()) names.push_back(conn.name);
        return names;
    };

    const std::size_t at = words.size();

    if (at <= 2) {
        std::vector<std::string> out = {"connect", "disconnect", "config",  "setup", "doctor",
                                        "commands", "completions", "--help", "--version"};
        for (const auto& name : commandNames()) out.push_back(name);
        return out;
    }

    const std::string& command = words[1];

    if (command == "connect") return at == 3 ? connectionNames() : std::vector<std::string>{};
    if (command == "completions") {
        return at == 3 ? std::vector<std::string>{"zsh", "bash"} : std::vector<std::string>{};
    }

    if (command != "config") return {};

    if (at == 3) {
        return {"list",  "get",     "set",    "unset",  "path",   "edit",
                "check", "add",     "remove", "default", "bind",  "unbind",
                "binds", "actions", "connections"};
    }

    const std::string& sub = words[2];

    if (at == 4) {
        if (sub == "get" || sub == "set" || sub == "unset") {
            std::vector<std::string> keys;
            for (const auto& setting : Config::schema()) keys.push_back(setting.path);
            for (const auto& conn : config.connections()) {
                for (const char* field : {"host", "user", "key", "password", "port",
                                          "remote_dir", "jump", "forward_agent"}) {
                    keys.push_back("connection." + conn.name + "." + field);
                }
            }
            return keys;
        }
        if (sub == "remove" || sub == "default") return connectionNames();
        if (sub == "bind" || sub == "unbind") {
            std::vector<std::string> actions;
            for (const auto& action : knownActions()) actions.push_back(action.name);
            return actions;
        }
        return {};
    }

    if (at == 5 && sub == "set") {
        const std::string& key = words[3];
        if (key == "decoration.theme") return Config::availableThemes();
        if (const Config::Setting* setting = Config::setting(key)) {
            if (!setting->choices.empty()) return setting->choices;
            if (setting->type == Config::Setting::Type::Bool) return {"true", "false"};
        }
        return {};
    }
    return {};
}

int printCompletionScript(const std::string& shell) {
    // Both scripts contain `)" `, which would end an ordinary raw string early.
    if (shell == "zsh") {
        std::cout << R"APOLLO(#compdef apollo
# Apollo completion for zsh. Install with:
#   apollo completions zsh > "${fpath[1]}/_apollo"
# It asks Apollo what could come next, so it cannot go stale.
_apollo() {
    local -a candidates
    candidates=(${(f)"$(apollo __complete ${words[1,CURRENT]} 2>/dev/null)"})
    compadd -a candidates
}
compdef _apollo apollo
)APOLLO";
        return 0;
    }
    if (shell == "bash") {
        std::cout << R"APOLLO(# Apollo completion for bash. Install with:
#   apollo completions bash > ~/.local/share/bash-completion/completions/apollo
# or source it from your ~/.bashrc.
# It asks Apollo what could come next, so it cannot go stale.
_apollo() {
    local candidates
    candidates="$(apollo __complete "${COMP_WORDS[@]:0:$((COMP_CWORD + 1))}" 2>/dev/null)"
    COMPREPLY=($(compgen -W "${candidates}" -- "${COMP_WORDS[COMP_CWORD]}"))
}
complete -F _apollo apollo
)APOLLO";
        return 0;
    }

    std::cerr << "Usage: apollo completions zsh|bash\n";
    return 1;
}

} // namespace

void printUsage() {
    std::cout << "Apollo " APOLLO_VERSION " — a terminal workspace\n\n"
                 "  apollo                      open Apollo here\n"
                 "  apollo <directory>          open Apollo there\n"
                 "  apollo connect [name]       open it connected over SSH\n"
                 "  apollo config               settings, in an editor\n"
                 "  apollo config <subcommand>  settings, from the shell\n"
                 "  apollo setup                run the first-run wizard again\n"
                 "  apollo doctor               check the installation\n"
                 "  apollo commands             list the commands you have added\n"
                 "  apollo <command>            run one of them\n"
                 "  apollo completions zsh      shell completion, generated from the schema\n"
                 "  apollo --version, --help\n"
                 "\n"
                 "Inside Apollo's own terminal these act on the running window:\n"
                 "  apollo                      go back to the workspace\n"
                 "  apollo <directory>          take both panes there\n"
                 "  apollo quit                 leave Apollo\n"
                 "  apollo config               open the settings\n"
                 "  apollo new-tab              another terminal tab\n"
                 "  apollo connect [name]       another tab, connected\n"
                 "  apollo disconnect           put this tab back on the local machine\n";
}

bool bootstrap(Config& config, std::string* note) {
    std::error_code ec;
    if (fs::exists(paths::configFile(), ec)) return false;

    const fs::path legacy = paths::legacyPropertiesFile();
    if (fs::exists(legacy, ec)) {
        if (const auto converted = migrateLegacyConfig(legacy)) {
            std::string error;
            if (paths::ensureDir(paths::configDir(), &error)) {
                ConfigFile file;
                file.parse(*converted, paths::configFile().string());
                if (file.save(paths::configFile(), &error)) {
                    if (note) {
                        *note = "Converted " + paths::contractUser(legacy) + " into " +
                                paths::contractUser(paths::configFile()) +
                                ". The old file was left alone.";
                    }
                    config.load();
                    paths::markSetupDone();
                    return false; // nothing to set up: the settings came across
                }
            }
        }
    }

    std::string error;
    if (!config.writeDefault(&error)) {
        if (note) *note = error;
        return false;
    }
    if (note) *note = "Wrote " + paths::contractUser(paths::configFile());
    return true;
}

Outcome dispatch(const std::vector<std::string>& args, Config& config) {
    Outcome outcome;

    if (const std::string socket = control::socketFromEnvironment(); !socket.empty()) {
        if (const std::string message = controlMessageFor(args); !message.empty()) {
            if (control::send(socket, message)) return outcome;
        }
    } else if (!args.empty() &&
               (args[0] == "quit" || args[0] == "exit" || args[0] == "disconnect")) {
        std::cerr << "Apollo is not running in this terminal.\n";
        outcome.code = 1;
        return outcome;
    }

    if (args.empty()) {
        outcome.launch = true;
        return outcome;
    }

    const std::string first = args[0];
    const std::vector<std::string> rest(args.begin() + 1, args.end());

    if (first == "--help" || first == "-h" || first == "help") {
        printUsage();
        return outcome;
    }
    if (first == "--version" || first == "-v" || first == "version") {
        std::cout << "apollo " APOLLO_VERSION "\n";
        return outcome;
    }
    if (first == "doctor") {
        outcome.code = doctor(config) == 0 ? 0 : 1;
        return outcome;
    }
    if (first == "completions") {
        outcome.code = printCompletionScript(rest.empty() ? "" : rest[0]);
        return outcome;
    }
    if (first == "__complete") {
        for (const auto& candidate : completionsFor(rest, config)) {
            std::cout << candidate << "\n";
        }
        return outcome;
    }
    if (first == "setup" || first == "onboard") {
        outcome.launch = true;
        outcome.options.runSetup = true;
        return outcome;
    }
    if (first == "config") {
        outcome.code = runConfig(rest, config, outcome);
        return outcome;
    }
    if (first == "commands") {
        outcome.code = listCommands(config);
        return outcome;
    }
    if (first == "connect") {
        outcome.launch = true;
        outcome.options.connect = rest.empty() ? " " : rest[0];
        // A space means "resolve it for me": empty would mean "no connection".
        if (outcome.options.connect == " ") outcome.options.connect.clear();

        std::string error;
        if (!config.resolveConnection(rest.empty() ? "" : rest[0], error)) {
            // Nothing named and several to choose from: open the window and
            // let it ask, rather than printing a list to a shell.
            if (rest.empty() && config.connections().size() > 1) {
                outcome.options.connect.clear();
                outcome.options.chooseConnection = true;
                return outcome;
            }
            std::cerr << error << "\n";
            outcome.launch = false;
            outcome.code = 1;
        } else if (!rest.empty()) {
            outcome.options.connect = rest[0];
        } else {
            outcome.options.connect = config.resolveConnection("", error)->name;
        }
        return outcome;
    }

    std::error_code ec;
    if (fs::is_directory(paths::expandUser(first), ec)) {
        outcome.launch = true;
        outcome.options.workspace = first;
        return outcome;
    }

    CommandRegistry registry;
    for (const auto& declared : config.commands()) {
        registry.addDeclared(declared.name, declared.exec, declared.summary, "apollo.conf");
    }
    registry.scanDirectory(paths::commandsDir());

    if (const Command* command = registry.find(first)) {
        std::string line = command->exec;
        for (const auto& argument : rest) line += " " + argument;
        std::vector<char*> argv = {const_cast<char*>("/bin/sh"), const_cast<char*>("-c"),
                                   const_cast<char*>(line.c_str()), nullptr};
        ::execv("/bin/sh", argv.data());
        std::cerr << "could not run " << command->name << "\n";
        outcome.code = 1;
        return outcome;
    }

    std::cerr << "Unknown command: " << first << "\n\n";
    printUsage();
    outcome.code = 1;
    return outcome;
}

} // namespace apollo::cli
