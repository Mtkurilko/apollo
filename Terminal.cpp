#include "Terminal.h"

#include <string>
#include <array>
#include <cstdio>
#include <iostream>
#include <filesystem>
#include <sstream>
#include <unistd.h>
#include <sys/stat.h>
#include <fstream>
#include <SFML/Graphics.hpp>
#include <vector>

namespace fs = std::filesystem;

Terminal::Terminal() : filePage(1), remoteServer(nullptr) {
    // Load the font once for use by System boot visuals and terminal rendering
    if (!font.openFromFile("/Users/you/Apollo/apollo_project/assets/GFSNeohellenic-Regular.ttf")) {
        // Silent fail for now; System boot text won't render if missing
        // Could add logging if desired
    }
    
    // Initialize remote server connection (lazy-loaded on first use)
    // Can be initialized here if apollo.properties exists
    std::string configPath = "/Users/you/Apollo/apollo_project/apollo.properties";
    if (fs::exists(configPath)) {
        remoteServer = std::make_unique<RemoteServer>(configPath);
    }
}

void Terminal::setCommand(std::string input) {
    command = input;
}

void Terminal::executeCommand() {
    bool clearHistory = false;
    std::vector<std::string> parts = tokenize(command);
    if (parts.empty()) return;
    std::string commandName = parts[0];
    std::string argument;
    if (parts.size() > 1) {
        argument.reserve(command.size());
        for (std::size_t i = 1; i < parts.size(); ++i) {
            if (i > 1) argument.push_back(' ');
            argument += parts[i];
        }
    }
    
    // apollo base command
    if (commandName == "apollo") {
        if (argument.empty()) {
            // If connected to remote, go to ~/APOLLO; otherwise go to local Apollo dir
            if (remoteServer && remoteServer->isConnected()) {
                command = "cd ~/APOLLO";
                commandName = "cd";
                argument = "~/APOLLO";
            } else {
                command = "cd /Users/you/Apollo";
                commandName = "cd";
                argument = "/Users/you/Apollo";
            }
        }
    }
    
    if (commandName == "cd") {
        if (argument.empty()) {
            std::string msg = "Usage: cd <directory>";
            std::cout << msg << std::endl;
            addHistory("APOLLO % " + command);
            addHistory(msg);
        } else {
            // Check if connected to remote - execute remotely
            if (remoteServer && remoteServer->isConnected()) {
                std::string result = remoteServer->executeRemoteCommand(command);
                addHistory("APOLLO % " + command);
                // Parse result to show directory change or error
                if (!result.empty()) {
                    std::istringstream rs(result);
                    std::string line;
                    while (std::getline(rs, line)) {
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        if (!line.empty()) addHistory(line);
                    }
                }
            } else {
                // Local cd
                if (chdir(argument.c_str()) == 0) {
                    std::string msg = "Directory changed: " + argument;
                    std::cout << msg << std::endl;
                    addHistory("APOLLO % " + command);
                    addHistory(msg);
                } else {
                    std::string err = std::string("cd failed: ") + argument;
                    perror(("cd failed going to " + argument).c_str());
                    addHistory("APOLLO % " + command);
                    addHistory(err);
                }
            }
        }
        return;
    }
    
    // custom commands later (ex. go back to apollo directory, exit apollo)
    if (commandName == "apollo") {
        if (argument == "exit") {
            // Disconnect from remote first if connected
            if (remoteServer && remoteServer->isConnected()) {
                addHistory("APOLLO % " + command);
                remoteServer->disconnect();
                addHistory("Disconnected from remote server.");
            }
            command = "killall launchApollo";
        } else if (argument == "clear") {
            command = "";
            clearHistory = true;
        } else if (argument == "connect") {
            // Connect to the remote server
            addHistory("APOLLO % " + command);
            if (!remoteServer) {
                addHistory("ERROR: RemoteServer not initialized. Check apollo.properties exists.");
            } else {
                if (remoteServer->connect()) {
                    addHistory(remoteServer->getConnectionStatus());
                    // Change to ~/APOLLO directory on remote server
                    std::string cdResult = remoteServer->executeRemoteCommand("cd ~/APOLLO 2>&1");
                    if (cdResult.find("APOLLO") != std::string::npos) {
                        addHistory("Remote directory: " + cdResult);
                    } else {
                        addHistory("Note: ~/APOLLO may not exist on remote server");
                        addHistory(cdResult);
                    }
                } else {
                    addHistory(remoteServer->getConnectionStatus());
                }
            }
            command = "";
            return;
        } else if (argument == "disconnect") {
            // Disconnect from remote server
            addHistory("APOLLO % " + command);
            if (!remoteServer) {
                addHistory("ERROR: RemoteServer not initialized.");
            } else if (!remoteServer->isConnected()) {
                addHistory("ERROR: Not connected to any remote server.");
            } else {
                remoteServer->disconnect();
                addHistory("Disconnected from remote server.");
            }
            command = "";
            return;
        } else if (argument == "p++") {
            addHistory("APOLLO % " + command);
            filePage++;
            command = "";
            return;
        } else if (argument == "p--") {
            addHistory("APOLLO % " + command);
            if (filePage > 1) {
                filePage--;
            } else {
                addHistory("ERROR: No pages to go back");
            }
            command = "";
            return;
        } else if (argument == "term") {
            // Open terminal - either SSH to remote or open local
            addHistory("APOLLO % " + command);
            if (remoteServer && remoteServer->isConnected()) {
                // Build SSH command with sshpass and navigate to remote working dir
                std::string host = remoteServer->getHost();
                std::string user = remoteServer->getUser();
                std::string password = remoteServer->getPassword();
                std::string port = remoteServer->getPort();
                std::string workingDir = remoteServer->getRemoteWorkingDir();
                
                // Escape spaces in working directory path
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
                
                bool isTildePath = !workingDir.empty() && workingDir[0] == '~';
                std::string escapedDir = isTildePath ? workingDir : escapeForShell(workingDir);
                
                // Create a temporary script to execute the SSH command
                std::string tempScript = "/tmp/apollo_ssh_" + std::to_string(getpid()) + ".command";
                std::ofstream scriptFile(tempScript);
                scriptFile << "#!/bin/bash\n";
                scriptFile << "sshpass -p '" << password << "' ssh -o StrictHostKeyChecking=no -p " << port << " " << user << "@" << host << " -t 'cd " << escapedDir << "; exec bash'\n";
                scriptFile.close();
                
                // Make script executable
                chmod(tempScript.c_str(), 0755);
                
                // Open the script in Terminal
                std::string cmd = "open -a Terminal \"" + tempScript + "\"";
                system(cmd.c_str());
            } else {
                // Local terminal
                std::filesystem::path currPath = fs::current_path();
                std::string cmd = "open -a Terminal \"" + currPath.string() + "\"";
                system(cmd.c_str());
            }
            command = "";
            return;
        } else if (argument == "termk") {
            // Open terminal and kill apollo
            addHistory("APOLLO % " + command);
            if (remoteServer && remoteServer->isConnected()) {
                // Build SSH command with sshpass and navigate to remote working dir
                std::string host = remoteServer->getHost();
                std::string user = remoteServer->getUser();
                std::string password = remoteServer->getPassword();
                std::string port = remoteServer->getPort();
                std::string workingDir = remoteServer->getRemoteWorkingDir();
                
                // Escape spaces in working directory path
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
                
                bool isTildePath = !workingDir.empty() && workingDir[0] == '~';
                std::string escapedDir = isTildePath ? workingDir : escapeForShell(workingDir);
                
                // Create a temporary script to execute the SSH command
                std::string tempScript = "/tmp/apollo_ssh_" + std::to_string(getpid()) + ".command";
                std::ofstream scriptFile(tempScript);
                scriptFile << "#!/bin/bash\n";
                scriptFile << "sshpass -p '" << password << "' ssh -o StrictHostKeyChecking=no -p " << port << " " << user << "@" << host << " -t 'cd " << escapedDir << "; exec bash'\n";
                scriptFile.close();
                
                // Make script executable
                chmod(tempScript.c_str(), 0755);
                
                // Open the script in Terminal
                std::string cmd = "open -a Terminal \"" + tempScript + "\"";
                system(cmd.c_str());
            } else {
                // Local terminal
                std::filesystem::path currPath = fs::current_path();
                std::string cmd = "open -a Terminal \"" + currPath.string() + "\"";
                system(cmd.c_str());
            }
            // Then kill apollo
            command = "apollo exit";
            executeCommand();
            return;
        } else if (argument == "save") {
            // Upload all modified files to remote server
            addHistory("APOLLO % " + command);
            if (!remoteServer || !remoteServer->isConnected()) {
                addHistory("ERROR: Not connected to remote server. apollo save only works when connected.");
                command = "";
                return;
            }
            
            std::string host = remoteServer->getHost();
            std::string user = remoteServer->getUser();
            std::string password = remoteServer->getPassword();
            std::string port = remoteServer->getPort();
            std::string workingDir = remoteServer->getRemoteWorkingDir();
            
            std::string tempDir = "/tmp/apollo_files";
            std::string filesUploaded = "";
            int uploadCount = 0;
            
            // Check if temp directory exists
            if (!fs::exists(tempDir)) {
                addHistory("No modified files to upload.");
                command = "";
                return;
            }
            
            // Upload all files in temp directory
            try {
                for (const auto& entry : fs::directory_iterator(tempDir)) {
                    if (fs::is_regular_file(entry)) {
                        std::string filename = entry.path().filename().string();
                        std::string localFile = tempDir + "/" + filename;
                        
                        // Build remote file path without escaping - let SSH handle it
                        std::string remoteFile = workingDir + "/" + filename;
                        
                        // Upload file via SCP with proper quoting
                        std::string scpCmd = "sshpass -p '" + password + "' scp -P " + port + " -o StrictHostKeyChecking=no \"" + localFile + "\" '" + user + "@" + host + ":" + remoteFile + "'";
                        int scpResult = system(scpCmd.c_str());
                        
                        if (scpResult == 0) {
                            filesUploaded += filename + ", ";
                            uploadCount++;
                            // Delete file after successful upload
                            try {
                                fs::remove(localFile);
                            } catch (const std::exception& e) {
                                addHistory("Warning: Could not delete " + filename + " from cache");
                            }
                        } else {
                            addHistory("ERROR: Failed to upload " + filename);
                        }
                    }
                }
            } catch (const std::exception& e) {
                addHistory("ERROR: " + std::string(e.what()));
                command = "";
                return;
            }
            
            if (uploadCount > 0) {
                // Remove trailing comma and space
                if (!filesUploaded.empty()) {
                    filesUploaded = filesUploaded.substr(0, filesUploaded.length() - 2);
                }
                addHistory("Uploaded " + std::to_string(uploadCount) + " file(s): " + filesUploaded);
            } else {
                addHistory("No files to upload.");
            }
            command = "";
            return;
        } else {
            std::string msg = "Unknown apollo command: " + argument;
            std::cout << msg << std::endl;
            addHistory("APOLLO % " + command);
            addHistory(msg);
            return;
        }
    }
    
    // Handle "open" command for files (local and remote)
    if (commandName == "open") {
        if (argument.empty()) {
            std::string msg = "Usage: open <filename>";
            std::cout << msg << std::endl;
            addHistory("APOLLO % " + command);
            addHistory(msg);
            return;
        }
        
        addHistory("APOLLO % " + command);
        
        if (remoteServer && remoteServer->isConnected()) {
            // Remote file: download via SCP, open in default editor, upload back
            std::string host = remoteServer->getHost();
            std::string user = remoteServer->getUser();
            std::string password = remoteServer->getPassword();
            std::string port = remoteServer->getPort();
            std::string workingDir = remoteServer->getRemoteWorkingDir();
            
            // Escape spaces in paths for shell
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
            
            bool isTildePath = !workingDir.empty() && workingDir[0] == '~';
            std::string escapedDir = isTildePath ? workingDir : escapeForShell(workingDir);
            std::string escapedFile = escapeForShell(argument);
            
            // Create temp directory for downloads
            std::string tempDir = "/tmp/apollo_files";
            system("mkdir -p /tmp/apollo_files");
            
            std::string localFile = tempDir + "/" + argument;
            
            // Check if file already exists in cache
            if (fs::exists(localFile)) {
                // File already downloaded, just open it
                std::string openCmd = "open \"" + localFile + "\"";
                system(openCmd.c_str());
                addHistory("File opened from cache: " + argument);
            } else {
                // Download file via SCP - use the unescaped path for the remote file
                // The SSH session will handle the escaping correctly
                std::string remoteFile = workingDir + "/" + argument;
                std::string scpSource = user + "@" + host + ":" + remoteFile;
                
                // Build SCP command with proper quoting
                std::string scpCmd = "sshpass -p '" + password + "' scp -P " + port + " -o StrictHostKeyChecking=no '" + user + "@" + host + ":" + remoteFile + "' \"" + localFile + "\"";
                int scpResult = system(scpCmd.c_str());
                
                if (scpResult == 0) {
                    // File downloaded successfully, open it
                    std::string openCmd = "open \"" + localFile + "\"";
                    system(openCmd.c_str());
                    addHistory("File opened: " + argument);
                    addHistory("Note: Use 'apollo save' to upload changes when done editing.");
                } else {
                    addHistory("ERROR: Failed to download file via SCP");
                }
            }
        } else {
            // Local file: open normally
            std::string openCmd = "open \"" + argument + "\"";
            system(openCmd.c_str());
            addHistory("File opened: " + argument);
        }
        command = "";
        return;
    }
    
    std::cout << "Executing: " << command << std::endl;
    
    // Check if connected to remote server - if so, execute remotely
    std::string result;
    if (remoteServer && remoteServer->isConnected()) {
        result = remoteServer->executeRemoteCommand(command);
    } else {
        // Execute locally
        // Make sure stderr is captured too
        std::string execCmd = command;
        if (execCmd.find("2>&1") == std::string::npos) execCmd += " 2>&1";
        char buffer[256];
        FILE* pipe = popen(execCmd.c_str(), "r");

        if (!pipe) {
            addHistory("APOLLO % " + command);
            addHistory("ERROR: popen() failed");
            throw std::runtime_error("popen() failed!");
        }
        
        // Read the output line by line (or chunk by chunk)
        try {
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                result += buffer;
            }
        } catch (...) {
            pclose(pipe);
            throw;
        }
        
        // Close the pipe and get the exit status
        pclose(pipe);
    }
    addHistory(std::string("APOLLO % ") + command);
    std::istringstream rs(result);
    std::string line;
    while (std::getline(rs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        addHistory("   "+line);
    }
    std::cout << result << std::endl;

    if (clearHistory) {
        historyCount = 0;
    } else if (argument == "termk") { // check for apollo kill after termk
        command = "apollo exit";
        executeCommand();
    }
}

std::vector<std::string> Terminal::tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuotes = false;
    char quoteChar = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '\\') {
            if (i + 1 < line.size()) {
                cur.push_back(line[i+1]);
                ++i;
                continue;
            }
        }
        if ((c == '"' || c == '\'') ) {
            if (!inQuotes) { inQuotes = true; quoteChar = c; continue; }
            if (c == quoteChar) { inQuotes = false; quoteChar = 0; continue; }
        }
        if (std::isspace(static_cast<unsigned char>(c)) && !inQuotes) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

void Terminal::draw(sf::RenderWindow* window) {
    // box
    sf::Vector2f boxPos(995.f, 32.f);
    sf::Vector2f boxSize(500.f, 774.f);
    sf::RectangleShape terminalView(boxSize);
    terminalView.setFillColor(sf::Color(18, 18, 18));
    terminalView.setOutlineThickness(2.f);
    terminalView.setOutlineColor(sf::Color(36, 36, 36));
    terminalView.setPosition(boxPos);
    window->draw(terminalView);

    // history area inside box (padding)
    float padX = 18.f;
    float padY = 18.f;
    float usableHeight = boxSize.y - 70.f; // leave space for current command line
    float perLine = static_cast<float>(lineSize) + lineSpacing;
    int maxLines = static_cast<int>(usableHeight / perLine);
    if (maxLines < 1) return;
    int start = historyCount - maxLines;
    if (start < 0) start = 0;
    sf::Text lineTxt(font, "", lineSize);
    float y = boxPos.y + padY;
    float maxPixelWidth = boxSize.x - 2 * padX;
    for (int i = start; i < historyCount; ++i) {
        std::string s = history[i];
        lineTxt.setString(s);
        // approximate width control
        float approxWidth = static_cast<float>(s.size()) * (lineSize * 0.4f);
        if (approxWidth > maxPixelWidth) {
            std::size_t keep = static_cast<std::size_t>(maxPixelWidth / (lineSize * 0.4f));
            if (keep > 3 && keep < s.size()) {
                s = s.substr(0, keep - 3) + "...";
                lineTxt.setString(s);
            }
        }
        lineTxt.setPosition(sf::Vector2f(boxPos.x + padX, y));
        window->draw(lineTxt);
        y += perLine;
    }

    // current command line
    sf::Text curr(font, "APOLLO % " + command, lineSize);
    curr.setPosition(sf::Vector2f(boxPos.x + padX, boxPos.y + boxSize.y - 48.f));
    window->draw(curr);
}

void Terminal::addHistory(std::string input) {
    if (input.empty()) return;
    if (historyCount < static_cast<int>(history.size())) {
        history[historyCount++] = input;
    } else {
        for (std::size_t i = 1; i < history.size(); ++i) history[i-1] = history[i];
        history.back() = input;
    }
}

bool Terminal::isRemoteConnected() const {
    return remoteServer && remoteServer->isConnected();
}

RemoteServer* Terminal::getRemoteServer() const {
    return remoteServer.get();
}

std::vector<std::pair<std::string, bool>> Terminal::getRemoteDirectoryListing() {
    std::vector<std::pair<std::string, bool>> result;
    if (!remoteServer || !remoteServer->isConnected()) {
        return result;
    }
    
    // Use ls -1F to list files with type indicators
    std::string lsOutput = remoteServer->executeRemoteCommand("ls -1F");
    std::istringstream stream(lsOutput);
    std::string line;
    
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        
        bool isDirectory = false;
        if (!line.empty() && line.back() == '/') {
            isDirectory = true;
            line.pop_back(); // Remove trailing /
        } else if (!line.empty() && (line.back() == '*' || line.back() == '@' || line.back() == '=' || line.back() == '|')) {
            line.pop_back(); // Remove type indicator
        }
        
        if (!line.empty()) {
            result.push_back({line, isDirectory});
        }
    }
    
    return result;
}

void Terminal::autocomplete(std::string& line) {
    // Tokenize and determine last arg to complete
    auto parts = tokenize(line);
    if (parts.empty()) return;
    // Only complete arguments (not the command word)
    if (parts.size() < 2) return;
    std::string prefix = parts.back();

    // Resolve search directory and prefix name
    fs::path base = fs::current_path();
    fs::path parent;
    std::string namePrefix = prefix;
    fs::path p(prefix);
    if (p.has_parent_path()) {
        parent = p.parent_path();
        namePrefix = p.filename().string();
    }
    fs::path dir = parent.empty() ? base : (parent.is_absolute() ? parent : base / parent);

    std::vector<std::string> matches;
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::end(it); it.increment(ec)) {
        const auto& entry = *it;
        std::string fname = entry.path().filename().string();
        if (fname.rfind(namePrefix, 0) == 0) {
            if (entry.is_directory()) fname += "/";
            matches.push_back(fname);
        }
    }
    if (matches.empty()) return;

    if (matches.size() == 1) {
        // Replace last part and rebuild string
        parts.back() = matches.front();
        std::string rebuilt;
        rebuilt.reserve(line.size() + 32);
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i) rebuilt.push_back(' ');
            const std::string& seg = parts[i];
            if (seg.find(' ') != std::string::npos) {
                rebuilt.push_back('"');
                rebuilt += seg;
                rebuilt.push_back('"');
            } else {
                rebuilt += seg;
            }
        }
        line = rebuilt;
        return;
    }

    // Multiple candidates: print to history and do not modify input
    addHistory("APOLLO % " + line);
    for (auto& m : matches) addHistory(m);
}
