#ifndef TERMINAL_H
#define TERMINAL_H

#include <string>
#include <array>
#include <SFML/Graphics.hpp>
#include <memory>
#include "RemoteServer.h"

class Terminal {
public:
    Terminal();
    void setCommand(std::string input);
    void executeCommand();
    void addHistory(std::string input);
    void draw(sf::RenderWindow* window);
    sf::Font font;
    int filePage;
    void autocomplete(std::string& line);
    bool isRemoteConnected() const;
    std::vector<std::pair<std::string, bool>> getRemoteDirectoryListing();
    RemoteServer* getRemoteServer() const;
private:
    std::string command;
    std::array<std::string, 50> history; // rolling buffer
    int historyCount = 0;
    unsigned int lineSize = 22; // font size
    float lineSpacing = 4.f;    // extra spacing in px
    float left = 40.f;          // origin x
    float top = 480.f;          // origin y
    float maxHeight = 1200.f;   // visible height
    float maxWidth = 1800.f;    // max line width in px
    std::vector<std::string> tokenize(const std::string& line);
    std::unique_ptr<RemoteServer> remoteServer;
};

#endif // TERMINAL_H
