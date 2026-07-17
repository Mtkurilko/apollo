#include "RemoteServer.h"
#include <cstdio>
#include <iostream>

RemoteServer::RemoteServer(const std::string& configFilePath)
    : connected(false), port("22"), remoteWorkingDir("~/APOLLO") {
    loadConfigFromProperties(configFilePath);
}

void RemoteServer::loadConfigFromProperties(const std::string& configFilePath) {
    PropertiesParser props(configFilePath);
    
    host = props.getProperty("ssh.host", "");
    user = props.getProperty("ssh.user", "");
    password = props.getProperty("ssh.password", "");
    port = props.getProperty("ssh.port", "22");
    
    if (host.empty() || user.empty() || password.empty()) {
        statusMessage = "ERROR: Missing SSH configuration (host, user, or password)";
    }
}

bool RemoteServer::connect() {
    if (host.empty() || user.empty() || password.empty()) {
        statusMessage = "ERROR: SSH configuration incomplete. Check apollo.properties";
        connected = false;
        return false;
    }
    
    // Test connection by running a simple command
    std::string testCmd = buildSSHCommand() + " 'echo \"Connection successful\"' 2>&1";
    
    FILE* pipe = popen(testCmd.c_str(), "r");
    if (!pipe) {
        statusMessage = "ERROR: Failed to execute SSH command";
        connected = false;
        return false;
    }
    
    char buffer[256];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    int status = pclose(pipe);
    
    if (status == 0 && result.find("Connection successful") != std::string::npos) {
        statusMessage = "Connected to " + user + "@" + host;
        connected = true;
        return true;
    } else {
        statusMessage = "ERROR: SSH connection failed. " + result;
        connected = false;
        return false;
    }
}

void RemoteServer::disconnect() {
    connected = false;
    statusMessage = "Disconnected";
    remoteWorkingDir = "~/APOLLO"; // Reset to default
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

std::string RemoteServer::buildSSHCommand() const {
    // Using sshpass to pass password automatically
    std::string cmd = "sshpass -p '" + password + "' ssh -o StrictHostKeyChecking=no ";
    if (port != "22") {
        cmd += "-p " + port + " ";
    }
    cmd += user + "@" + host;
    return cmd;
}

std::string RemoteServer::executeRemoteCommand(const std::string& command) {
    if (!connected) {
        return "ERROR: Not connected to remote server. Use 'apollo connect' first.";
    }
    
    // Check if this is a cd command to update our tracked directory
    std::string trimmedCmd = command;
    size_t start = trimmedCmd.find_first_not_of(" \t\n\r");
    if (start != std::string::npos) {
        trimmedCmd = trimmedCmd.substr(start);
    }
    
    bool isCdCommand = (trimmedCmd.rfind("cd ", 0) == 0);
    
    // Helper to escape spaces with backslashes for shell (but not for ~ paths)
    auto escapeForShell = [](const std::string& str) -> std::string {
        std::string result;
        bool isTildePath = !str.empty() && str[0] == '~';
        for (char c : str) {
            if (c == ' ' && !isTildePath) {
                result += '\\';
            }
            result += c;
        }
        return result;
    };
    
    // For cd commands with arguments that might have spaces, we need to properly escape them
    std::string cmdToExecute = command;
    if (isCdCommand) {
        // Extract the cd argument and make sure it's properly escaped
        size_t cdEnd = 2; // after "cd"
        while (cdEnd < command.size() && std::isspace(command[cdEnd])) {
            cdEnd++;
        }
        if (cdEnd < command.size()) {
            std::string cdArg = command.substr(cdEnd);
            // Remove any existing quotes
            if ((cdArg.front() == '"' && cdArg.back() == '"') || 
                (cdArg.front() == '\'' && cdArg.back() == '\'')) {
                cdArg = cdArg.substr(1, cdArg.length() - 2);
            }
            // Escape spaces with backslashes (but not for ~ paths)
            bool isTildePath = !cdArg.empty() && cdArg[0] == '~';
            if (isTildePath) {
                cmdToExecute = "cd " + cdArg;
            } else {
                cmdToExecute = "cd " + escapeForShell(cdArg);
            }
        }
    }
    
    // Build command with directory context
    std::string cmdWithContext;
    if (isCdCommand) {
        // For cd commands, execute and update our tracked directory
        // Don't escape tilde paths
        bool isTildePath = !remoteWorkingDir.empty() && remoteWorkingDir[0] == '~';
        std::string workingDirPart = isTildePath ? remoteWorkingDir : escapeForShell(remoteWorkingDir);
        cmdWithContext = "cd " + workingDirPart + " && " + cmdToExecute + " && pwd";
    } else {
        // For other commands, execute in the tracked directory
        bool isTildePath = !remoteWorkingDir.empty() && remoteWorkingDir[0] == '~';
        std::string workingDirPart = isTildePath ? remoteWorkingDir : escapeForShell(remoteWorkingDir);
        cmdWithContext = "cd " + workingDirPart + " && " + command;
    }
    
    // Use single quotes for outer SSH to prevent shell interpretation on local side
    std::string fullCmd = buildSSHCommand() + " '" + cmdWithContext + "' 2>&1";
    
    FILE* pipe = popen(fullCmd.c_str(), "r");
    if (!pipe) {
        return "ERROR: Failed to execute remote command";
    }
    
    char buffer[256];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    
    // If this was a cd command and succeeded, update our tracked directory
    if (isCdCommand && !result.empty() && result.find("ERROR") == std::string::npos) {
        // The last line should be the pwd output
        size_t lastNewline = result.find_last_of('\n');
        if (lastNewline != std::string::npos && lastNewline > 0) {
            size_t prevNewline = result.find_last_of('\n', lastNewline - 1);
            std::string newDir = result.substr(prevNewline == std::string::npos ? 0 : prevNewline + 1, lastNewline - (prevNewline == std::string::npos ? 0 : prevNewline + 1));
            // Trim whitespace
            size_t trimStart = newDir.find_first_not_of(" \t\r");
            if (trimStart != std::string::npos) {
                remoteWorkingDir = newDir.substr(trimStart);
            }
        }
    }
    
    return result;
}
