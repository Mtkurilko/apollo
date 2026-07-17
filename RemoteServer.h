#ifndef REMOTE_SERVER_H
#define REMOTE_SERVER_H

#include <string>
#include "PropertiesParser.h"

class RemoteServer {
public:
    RemoteServer(const std::string& configFilePath);
    
    bool connect();
    void disconnect();
    bool isConnected() const;
    std::string getConnectionStatus() const;
    std::string getRemoteWorkingDir() const;
    std::string getHost() const;
    std::string getUser() const;
    std::string getPassword() const;
    std::string getPort() const;
    std::string executeRemoteCommand(const std::string& command);
    
private:
    std::string host;
    std::string user;
    std::string password;
    std::string port;
    bool connected;
    std::string statusMessage;
    std::string remoteWorkingDir;  // Track remote current directory
    
    void loadConfigFromProperties(const std::string& configFilePath);
    std::string buildSSHCommand() const;
};

#endif // REMOTE_SERVER_H
