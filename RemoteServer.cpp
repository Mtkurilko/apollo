#include "RemoteServer.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include "AppPaths.h"

namespace {

std::string runCapture(const std::string& cmd) {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {};
    char buffer[512];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) result += buffer;
    pclose(pipe);
    return result;
}

// Escape spaces for a remote shell, except on ~-rooted paths where the tilde
// must reach the remote shell unquoted so it still expands.
std::string escapeForShell(const std::string& str) {
    const bool isTildePath = !str.empty() && str[0] == '~';
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        if (c == ' ' && !isTildePath) result += '\\';
        result += c;
    }
    return result;
}

} // namespace

RemoteServer::RemoteServer(Connection connection)
    : conn(std::move(connection)), connected(false) {
    remoteWorkingDir = conn.remoteDir.empty() ? "~/APOLLO" : conn.remoteDir;
    // macOS caps AF_UNIX paths near 104 bytes and ssh refuses anything longer,
    // so the socket name stays short.
    controlPath = "/tmp/apollo-" + conn.name + "-" + std::to_string(getpid());
}

RemoteServer::~RemoteServer() {
    if (connected) closeControlMaster();
}

std::string RemoteServer::sshPrefix() const {
    std::string cmd;

    // Key auth keeps the password out of the process table entirely; sshpass is
    // only a fallback for hosts that have nothing else.
    if (conn.keyPath.empty() && !conn.password.empty()) {
        cmd = "sshpass -p '" + conn.password + "' ";
    }

    cmd += "ssh -o StrictHostKeyChecking=no ";

    // Connection multiplexing: the first command pays for the TCP handshake,
    // key exchange and auth; the rest reuse the socket.
    cmd += "-o ControlMaster=auto ";
    cmd += "-o ControlPath=" + controlPath + " ";
    cmd += "-o ControlPersist=300 ";
    cmd += "-o ConnectTimeout=8 ";

    if (!conn.keyPath.empty()) {
        cmd += "-i \"" + apollo::expandUser(conn.keyPath).string() + "\" ";
    }
    if (conn.port != "22") cmd += "-p " + conn.port + " ";

    cmd += conn.user + "@" + conn.host;
    return cmd;
}

std::string RemoteServer::scpPrefix() const {
    std::string cmd;
    if (conn.keyPath.empty() && !conn.password.empty()) {
        cmd = "sshpass -p '" + conn.password + "' ";
    }
    cmd += "scp -o StrictHostKeyChecking=no ";
    cmd += "-o ControlPath=" + controlPath + " ";
    if (!conn.keyPath.empty()) {
        cmd += "-i \"" + apollo::expandUser(conn.keyPath).string() + "\" ";
    }
    if (conn.port != "22") cmd += "-P " + conn.port + " ";
    return cmd;
}

void RemoteServer::closeControlMaster() const {
    const std::string cmd = "ssh -o ControlPath=" + controlPath + " -O exit " + conn.user + "@" +
                            conn.host + " >/dev/null 2>&1";
    std::system(cmd.c_str());
}

bool RemoteServer::connect() {
    if (!conn.valid()) {
        statusMessage = "ERROR: Connection '" + conn.name + "' is missing a host or user.";
        connected = false;
        return false;
    }
    if (conn.keyPath.empty() && conn.password.empty()) {
        statusMessage = "ERROR: Connection '" + conn.name + "' has no key or password. Set one with:"
                        " apollo config set connection." + conn.name + ".key ~/.ssh/id_ed25519";
        connected = false;
        return false;
    }

    // This first call also establishes the ControlMaster socket.
    const std::string result = runCapture(sshPrefix() + " 'echo \"Connection successful\"' 2>&1");

    if (result.find("Connection successful") != std::string::npos) {
        statusMessage = "Connected to " + conn.label() + " as '" + conn.name + "'";
        connected = true;
        return true;
    }

    statusMessage = "ERROR: SSH connection to " + conn.label() + " failed. " + result;
    connected = false;
    return false;
}

void RemoteServer::disconnect() {
    if (connected) closeControlMaster();
    connected = false;
    statusMessage = "Disconnected";
    remoteWorkingDir = conn.remoteDir.empty() ? "~/APOLLO" : conn.remoteDir;
}

bool RemoteServer::isConnected() const {
    return connected;
}

std::string RemoteServer::getConnectionStatus() const {
    return statusMessage;
}

std::string RemoteServer::getRemoteWorkingDir() const {
    return remoteWorkingDir;
}

std::string RemoteServer::executeRemoteCommand(const std::string& command) {
    if (!connected) {
        return "ERROR: Not connected to remote server. Use 'apollo connect' first.";
    }

    std::string trimmedCmd = command;
    const std::size_t start = trimmedCmd.find_first_not_of(" \t\n\r");
    if (start != std::string::npos) trimmedCmd = trimmedCmd.substr(start);

    const bool isCdCommand = (trimmedCmd.rfind("cd ", 0) == 0);

    std::string cmdToExecute = command;
    if (isCdCommand) {
        std::size_t cdEnd = 2;
        while (cdEnd < command.size() && std::isspace(static_cast<unsigned char>(command[cdEnd]))) {
            ++cdEnd;
        }
        if (cdEnd < command.size()) {
            std::string cdArg = command.substr(cdEnd);
            if (cdArg.size() >= 2 && ((cdArg.front() == '"' && cdArg.back() == '"') ||
                                      (cdArg.front() == '\'' && cdArg.back() == '\''))) {
                cdArg = cdArg.substr(1, cdArg.length() - 2);
            }
            cmdToExecute = "cd " + escapeForShell(cdArg);
        }
    }

    const std::string workingDirPart = escapeForShell(remoteWorkingDir);
    const std::string cmdWithContext =
        isCdCommand ? "cd " + workingDirPart + " && " + cmdToExecute + " && pwd"
                    : "cd " + workingDirPart + " && " + command;

    std::string result = runCapture(sshPrefix() + " '" + cmdWithContext + "' 2>&1");

    if (isCdCommand && !result.empty() && result.find("ERROR") == std::string::npos) {
        // The trailing `pwd` reports the directory we actually landed in.
        const std::size_t end = result.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) {
            const std::size_t lineStart = result.find_last_of('\n', end);
            const std::size_t from = (lineStart == std::string::npos) ? 0 : lineStart + 1;
            const std::string newDir = result.substr(from, end - from + 1);
            const std::size_t trimStart = newDir.find_first_not_of(" \t\r");
            if (trimStart != std::string::npos && newDir[trimStart] == '/') {
                remoteWorkingDir = newDir.substr(trimStart);
                // The pwd line was bookkeeping, not output the user asked for.
                result.erase(from);
            }
        }
    }

    return result;
}
