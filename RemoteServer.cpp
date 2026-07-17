#include "RemoteServer.h"

#include <cctype>
#include <cstdio>
#include <iostream>
#include <unistd.h>

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

RemoteServer::RemoteServer(const std::string& configFilePath)
    : port("22"), connected(false), remoteWorkingDir("~/APOLLO") {
    loadConfigFromProperties(configFilePath);
    // Short path: macOS caps AF_UNIX socket paths at ~104 bytes, and ssh
    // refuses a ControlPath that would overflow it.
    controlPath = "/tmp/apollo-ssh-" + std::to_string(getpid());
}

RemoteServer::~RemoteServer() {
    if (connected) closeControlMaster();
}

void RemoteServer::loadConfigFromProperties(const std::string& configFilePath) {
    PropertiesParser props(configFilePath);

    host = props.getProperty("ssh.host", "");
    user = props.getProperty("ssh.user", "");
    password = props.getProperty("ssh.password", "");
    keyPath = props.getProperty("ssh.key", "");
    port = props.getProperty("ssh.port", "22");

    if (host.empty() || user.empty() || (password.empty() && keyPath.empty())) {
        statusMessage = "ERROR: Missing SSH configuration (host, user, and key or password)";
    }
}

std::string RemoteServer::buildSSHCommand() const {
    std::string cmd;

    // Key auth keeps the password out of the process table entirely; fall back
    // to sshpass only when no key is configured.
    if (keyPath.empty()) {
        cmd = "sshpass -p '" + password + "' ";
    }

    cmd += "ssh -o StrictHostKeyChecking=no ";

    // Connection multiplexing. The first command pays for the TCP handshake,
    // key exchange and auth; every command after it reuses the same socket,
    // which takes a few hundred milliseconds down to a few milliseconds.
    cmd += "-o ControlMaster=auto ";
    cmd += "-o ControlPath=" + controlPath + " ";
    cmd += "-o ControlPersist=300 ";
    cmd += "-o ConnectTimeout=8 ";

    if (!keyPath.empty()) cmd += "-i " + keyPath + " ";
    if (port != "22") cmd += "-p " + port + " ";

    cmd += user + "@" + host;
    return cmd;
}

void RemoteServer::closeControlMaster() const {
    // Tears down the shared socket so a later connect() starts clean.
    const std::string cmd =
        "ssh -o ControlPath=" + controlPath + " -O exit " + user + "@" + host + " >/dev/null 2>&1";
    system(cmd.c_str());
}

bool RemoteServer::connect() {
    if (host.empty() || user.empty() || (password.empty() && keyPath.empty())) {
        statusMessage = "ERROR: SSH configuration incomplete. Check apollo.properties";
        connected = false;
        return false;
    }

    // This first call also establishes the ControlMaster socket that every
    // subsequent command rides on.
    const std::string testCmd = buildSSHCommand() + " 'echo \"Connection successful\"' 2>&1";
    const std::string result = runCapture(testCmd);

    if (result.find("Connection successful") != std::string::npos) {
        statusMessage = "Connected to " + user + "@" + host;
        connected = true;
        return true;
    }

    statusMessage = "ERROR: SSH connection failed. " + result;
    connected = false;
    return false;
}

void RemoteServer::disconnect() {
    if (connected) closeControlMaster();
    connected = false;
    statusMessage = "Disconnected";
    remoteWorkingDir = "~/APOLLO";
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

std::string RemoteServer::getHost() const {
    return host;
}

std::string RemoteServer::getUser() const {
    return user;
}

std::string RemoteServer::getPassword() const {
    return password;
}

std::string RemoteServer::getPort() const {
    return port;
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

    const std::string fullCmd = buildSSHCommand() + " '" + cmdWithContext + "' 2>&1";
    std::string result = runCapture(fullCmd);

    if (isCdCommand && !result.empty() && result.find("ERROR") == std::string::npos) {
        // The trailing `pwd` reports the directory we actually landed in.
        std::size_t end = result.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) {
            const std::size_t lineStart = result.find_last_of('\n', end);
            std::string newDir =
                result.substr(lineStart == std::string::npos ? 0 : lineStart + 1,
                              end - (lineStart == std::string::npos ? 0 : lineStart + 1) + 1);
            const std::size_t trimStart = newDir.find_first_not_of(" \t\r");
            if (trimStart != std::string::npos && newDir[trimStart] == '/') {
                remoteWorkingDir = newDir.substr(trimStart);
                // The pwd line was bookkeeping, not output the user asked for.
                result.erase(lineStart == std::string::npos ? 0 : lineStart);
            }
        }
    }

    return result;
}
