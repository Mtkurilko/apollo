// SSH destinations. `apollo connect` runs a real ssh in the pty instead of
// parsing its output, so vim/htop/colors all just work.
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

// A command plus the environment it needs.
struct Invocation {
    std::vector<std::string> argv;
    std::vector<std::string> env;
};

std::string controlPath(const Connection& conn);

Invocation interactive(const Connection& conn);

Invocation command(const Connection& conn, const std::string& remote);

// scp with the shared master. `args` are scp's own flags, then the paths.
// `batch` never prompts: for callers that don't own a terminal to prompt on.
Invocation copy(const Connection& conn, const std::vector<std::string>& args, bool batch);

void closeMaster(const Connection& conn);

// Escapes one argument for the remote /bin/sh, which only sees a string.
std::string quoteRemote(const std::string& text);

// Same but for a path. Leaves a leading ~ or ~user alone so the remote shell
// still expands it. Quote it and `cd` goes looking for a directory named "~".
std::string quoteRemotePath(const std::string& path);

} // namespace ssh
} // namespace apollo
