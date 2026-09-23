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

// scp takes the same options as ssh, except the port is -P and there's no
// agent to forward.
void addSharedOptions(std::vector<std::string>& argv, const Connection& conn,
                      int timeoutSeconds, bool forScp = false) {
    argv.push_back("-o"); argv.push_back("ControlMaster=auto");
    argv.push_back("-o"); argv.push_back("ControlPath=" + controlPath(conn));
    argv.push_back("-o"); argv.push_back("ControlPersist=300");
    argv.push_back("-o"); argv.push_back("ConnectTimeout=" + std::to_string(timeoutSeconds));

    if (conn.port != 22) {
        argv.push_back(forScp ? "-P" : "-p");
        argv.push_back(std::to_string(conn.port));
    }
    if (!conn.keyPath.empty()) {
        argv.push_back("-i");
        argv.push_back(paths::expandUser(conn.keyPath));
        // Key configured, so don't quietly fall back to asking for a password.
        argv.push_back("-o"); argv.push_back("IdentitiesOnly=yes");
    }
    if (!conn.jump.empty()) { argv.push_back("-J"); argv.push_back(conn.jump); }
    if (conn.forwardAgent && !forScp) argv.push_back("-A");
}

// Only reaches for sshpass when the connection actually needs it.
Invocation begin(const Connection& conn, const std::string& program = "ssh") {
    if (conn.usesPassword()) {
        return {{"sshpass", "-e", program}, {"SSHPASS=" + conn.password}};
    }
    return {{program}, {}};
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

std::string quoteRemotePath(const std::string& path) {
    if (path.empty()) return "''";
    if (path[0] != '~') return quoteRemote(path);

    // Only a plain ~ or ~name goes through bare. Anything else in there could
    // be something we really don't want the remote shell running.
    const std::size_t slash = path.find('/');
    const std::string head = path.substr(0, slash);
    for (std::size_t i = 1; i < head.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(head[i]);
        if (!std::isalnum(c) && c != '_' && c != '-' && c != '.') return quoteRemote(path);
    }
    if (slash == std::string::npos) return head;
    // The slash stays bare. Quoted, it hides the tilde from every shell but zsh.
    if (slash + 1 == path.size()) return head + "/";
    return head + "/" + quoteRemote(path.substr(slash + 1));
}

Invocation interactive(const Connection& conn) {
    Invocation call = begin(conn);
    addSharedOptions(call.argv, conn, 12);
    call.argv.push_back("-t"); // force a pty, so vim and friends work over the link
    call.argv.push_back(conn.label());

    // Shell reports its pid on the way in. Lets the browser ask the machine
    // where that shell is without installing anything over there.
    // exec keeps the pid, so $$ here is already the interactive shell's.
    std::string start = "printf '\\033]777;apollo;pid;%s\\033\\\\' $$; ";
    if (!conn.remoteDir.empty()) {
        start += "cd " + quoteRemotePath(conn.remoteDir) + " 2>/dev/null; ";
    }
    start += "exec \"${SHELL:-/bin/sh}\" -l";
    call.argv.push_back(start);
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

Invocation copy(const Connection& conn, const std::vector<std::string>& args, bool batch) {
    Invocation call = begin(conn, "scp");
    addSharedOptions(call.argv, conn, 12, true);
    if (batch) {
        call.argv.push_back("-o");
        call.argv.push_back("BatchMode=" + std::string(conn.usesPassword() ? "no" : "yes"));
    }
    call.argv.insert(call.argv.end(), args.begin(), args.end());
    return call;
}

void closeMaster(const Connection& conn) {
    std::vector<std::string> argv = {"ssh", "-o", "ControlPath=" + controlPath(conn),
                                     "-O", "exit", conn.label()};
    process::run(argv, std::chrono::seconds(3));
}

} // namespace ssh
} // namespace apollo
