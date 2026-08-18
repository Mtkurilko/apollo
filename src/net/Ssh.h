// SSH destinations.
//
// Apollo never parses SSH's output to fake a remote shell: `apollo connect`
// spawns a real `ssh` inside the terminal's pty, so the remote session is as
// complete as the one you would get from your own shell. The only thing Apollo
// runs on its own is the odd `ls` for the file browser, and those reuse the
// interactive session's multiplexed socket rather than opening a connection.
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
    // "lab  alice@10.0.0.5:2222"
    std::string describe() const;
    bool usesPassword() const { return keyPath.empty() && !password.empty(); }
};

namespace ssh {

// A command to run, and the environment it needs. Passwords travel in `env`
// rather than in argv, so `ps` never shows them.
struct Invocation {
    std::vector<std::string> argv;
    std::vector<std::string> env;
};

// The socket the first connection creates and the rest reuse. Kept short:
// macOS caps unix socket paths near 104 bytes and ssh does not warn, it fails.
std::string controlPath(const Connection& conn);

// An interactive login. Runs under a pty on the far side (-t) so full screen
// programs work exactly as they do locally.
Invocation interactive(const Connection& conn);

// One non-interactive command. Reuses the multiplexed socket, so this costs a
// round trip rather than a handshake.
Invocation command(const Connection& conn, const std::string& remote);

// Closes the shared connection. Safe to call when nothing is open.
void closeMaster(const Connection& conn);

struct Probe {
    bool ok = false;
    std::string message;
};
// Tries to reach the host, without prompting for anything.
Probe probe(const Connection& conn, int timeoutSeconds = 8);

// Escapes one argument for a remote /bin/sh, which sees a single string
// however carefully argv is built on this side. The rules are a local shell's
// rules, which is why it is the same function.
std::string quoteRemote(const std::string& text);

} // namespace ssh
} // namespace apollo
