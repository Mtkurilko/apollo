// SSH destinations. `apollo connect` spawns a real ssh in the pty rather
// than parsing its output.
#pragma once

#include <string>
#include <vector>

namespace apollo {

struct Connection {
    std::string name;
    std::string host;
    std::string user;
    std::string keyPath;   // preferred
    std::string password;  // fallback, needs sshpass
    std::string remoteDir; // where a session starts, empty means the login dir
    std::string jump;      // -J, for hosts behind a bastion
    int port = 22;
    bool forwardAgent = false;

    bool valid() const { return !host.empty() && !user.empty(); }
    std::string label() const { return user + "@" + host; }
    std::string describe() const;
    bool usesPassword() const { return keyPath.empty() && !password.empty(); }
};

namespace ssh {

// A command to run, and the environment it needs.
struct Invocation {
    std::vector<std::string> argv;
    std::vector<std::string> env;
};

std::string controlPath(const Connection& conn);

Invocation interactive(const Connection& conn);

Invocation command(const Connection& conn, const std::string& remote);

void closeMaster(const Connection& conn);

struct Probe {
    bool ok = false;
    std::string message;
};
Probe probe(const Connection& conn, int timeoutSeconds = 8);

// Escapes one argument for a remote /bin/sh, which only ever sees a string.
std::string quoteRemote(const std::string& text);

} // namespace ssh
} // namespace apollo
