#ifndef REMOTE_SERVER_H
#define REMOTE_SERVER_H

#include <string>

#include "Config.h"

// A single SSH session against one configured Connection. Commands ride a
// shared ControlMaster socket, so only the first one pays for the handshake.
class RemoteServer {
public:
    explicit RemoteServer(Connection conn);
    ~RemoteServer();

    RemoteServer(const RemoteServer&) = delete;
    RemoteServer& operator=(const RemoteServer&) = delete;

    bool connect();
    void disconnect();
    bool isConnected() const;

    std::string getConnectionStatus() const;
    std::string getRemoteWorkingDir() const;
    std::string getName() const { return conn.name; }
    std::string getHost() const { return conn.host; }
    std::string getUser() const { return conn.user; }
    std::string getPassword() const { return conn.password; }
    std::string getKeyPath() const { return conn.keyPath; }
    std::string getPort() const { return conn.port; }
    std::string getLabel() const { return conn.label(); }

    std::string executeRemoteCommand(const std::string& command);

    // The ssh/scp prefix callers need to reach this host, sharing the same
    // multiplexed socket.
    std::string sshPrefix() const;
    std::string scpPrefix() const;

private:
    Connection conn;
    bool connected;
    std::string statusMessage;
    std::string remoteWorkingDir;
    std::string controlPath;

    void closeControlMaster() const;
};

#endif // REMOTE_SERVER_H
