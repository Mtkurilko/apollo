#include "net/Ssh.h"

#include <unistd.h>

#include <algorithm>
#include <cctype>

#include "core/Paths.h"
#include "core/Process.h"

namespace apollo {

std::string Connection::describe() const {
    std::string out = label();
    if (port != 22) out += ":" + std::to_string(port);
    return out;
}

namespace ssh {
namespace {

void addSharedOptions(std::vector<std::string>& argv, const Connection& conn,
                      int timeoutSeconds) {
    argv.push_back("-o"); argv.push_back("ControlMaster=auto");
    argv.push_back("-o"); argv.push_back("ControlPath=" + controlPath(conn));
    argv.push_back("-o"); argv.push_back("ControlPersist=300");
    argv.push_back("-o"); argv.push_back("ConnectTimeout=" + std::to_string(timeoutSeconds));

    if (conn.port != 22) { argv.push_back("-p"); argv.push_back(std::to_string(conn.port)); }
    if (!conn.keyPath.empty()) {
        argv.push_back("-i");
        argv.push_back(paths::expandUser(conn.keyPath));
        // With a key configured, do not silently fall back to typing a password.
        argv.push_back("-o"); argv.push_back("IdentitiesOnly=yes");
    }
    if (!conn.jump.empty()) { argv.push_back("-J"); argv.push_back(conn.jump); }
    if (conn.forwardAgent) argv.push_back("-A");
}

// Prefixes sshpass when, and only when, the connection actually needs it.
Invocation begin(const Connection& conn) {
    if (conn.usesPassword()) {
        return {{"sshpass", "-e", "ssh"}, {"SSHPASS=" + conn.password}};
    }
    return {{"ssh"}, {}};
}

} // namespace

std::string controlPath(const Connection& conn) {
    std::string name = conn.name.empty() ? conn.host : conn.name;
    std::replace_if(name.begin(), name.end(),
                    [](unsigned char c) { return !std::isalnum(c) && c != '-' && c != '_'; },
                    '_');
    if (name.size() > 24) name.resize(24);
    return "/tmp/apollo." + std::to_string(::getuid()) + "." + name;
}

std::string quoteRemote(const std::string& text) { return process::shellQuote(text); }

Invocation interactive(const Connection& conn) {
    Invocation call = begin(conn);
    addSharedOptions(call.argv, conn, 12);
    call.argv.push_back("-t"); // force a pty, so vim and friends work over the link
    call.argv.push_back(conn.label());

    if (!conn.remoteDir.empty()) {
        call.argv.push_back("cd " + quoteRemote(conn.remoteDir) +
                            " 2>/dev/null; exec \"$SHELL\" -l");
    }
    return call;
}

Invocation command(const Connection& conn, const std::string& remote) {
    Invocation call = begin(conn);
    addSharedOptions(call.argv, conn, 8);
    call.argv.push_back("-o");
    call.argv.push_back("BatchMode=" + std::string(conn.usesPassword() ? "no" : "yes"));
    call.argv.push_back(conn.label());
    call.argv.push_back(remote);
    return call;
}

void closeMaster(const Connection& conn) {
    std::vector<std::string> argv = {"ssh", "-o", "ControlPath=" + controlPath(conn),
                                     "-O", "exit", conn.label()};
    process::run(argv, std::chrono::seconds(3));
}

Probe probe(const Connection& conn, int timeoutSeconds) {
    if (!conn.valid()) return {false, "missing host or user"};

    if (conn.usesPassword() && !process::which("sshpass")) {
        return {false, "sshpass is not installed, and this connection uses a password"};
    }
    if (!conn.keyPath.empty()) {
        const std::string key = paths::expandUser(conn.keyPath);
        if (::access(key.c_str(), R_OK) != 0) return {false, "cannot read key " + conn.keyPath};
    }

    auto call = command(conn, "true");
    for (std::size_t i = 0; i + 1 < call.argv.size(); ++i) {
        if (call.argv[i + 1].rfind("ConnectTimeout=", 0) == 0) {
            call.argv[i + 1] = "ConnectTimeout=" + std::to_string(timeoutSeconds);
        }
    }

    const auto result = process::run(call.argv, std::chrono::seconds(timeoutSeconds + 4),
                                     "", call.env);
    if (result.timedOut) return {false, "timed out after " + std::to_string(timeoutSeconds) + "s"};
    if (result.ok()) return {true, "reachable"};

    std::string message = result.err.empty() ? "ssh exited " + std::to_string(result.exitCode)
                                             : result.err;
    if (const auto newline = message.find('\n'); newline != std::string::npos) {
        message.resize(newline);
    }
    return {false, message};
}

} // namespace ssh
} // namespace apollo
