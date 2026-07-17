#include "System.h"

#include <iostream>
#include <string>
#include <filesystem>
#include <vector>
#include <SFML/Graphics.hpp>
#include <chrono>
#include <thread>
#include <ctime>
#include <string>
#include "PropertiesParser.h"

namespace fs = std::filesystem;

System::System() :
bootScreen(true),
bootStep(Initial),
folderCount(0),
fileCount(0),
userAction(' '),
apolloTexture("/Users/you/Apollo/apollo_project/assets/ApolloHead.png"),
apolloHead(apolloTexture),
columnTexture("/Users/you/Apollo/apollo_project/assets/column.png"),
columnSprite(columnTexture),
title(terminal.font, "", 100),
folderTexture("/Users/you/Apollo/apollo_project/assets/folder.png"),
folderSprite(folderTexture)
{
    rootDir = std::filesystem::current_path();
    apolloHead.setColor(sf::Color(255, 255, 255, 0));
    apolloHead.setTexture(apolloTexture, true);
}

void System::update() {
    if (bootScreen) {
        // Boot animation timing handled in checkBoot() called from display()
    } else {
        long current_time = static_cast<long>(std::time(nullptr));

        if ((current_time-last_run_time) > 1) {
            folderCount = 0;
            fileCount = 0;
            last_run_time = current_time;
            
            // Check if connected to remote server
            if (terminal.isRemoteConnected()) {
                // Get remote directory listing
                auto remoteListing = terminal.getRemoteDirectoryListing();
                for (const auto& item : remoteListing) {
                    if (item.second) { // is directory
                        folders[folderCount++] = item.first;
                    } else { // is file
                        files[fileCount++] = item.first;
                    }
                }
            } else {
                // Local directory listing
                currDir = fs::current_path();
                std::vector<std::string> folderNames;
                std::vector<std::string> fileNames;
                
                // Iterate through the directory entries
                for (const auto& entry : fs::directory_iterator(currDir)) {
                    if (fs::is_regular_file(entry)) {
                        fileNames.push_back(entry.path().filename().string());
                    } else {
                        folderNames.push_back(entry.path().filename().string());
                    }
                }
                
                // Set file names to proper locations
                for (const std::string& filename : folderNames) {
                    folders[folderCount] = filename;
                    folderCount++;
                }
                for (const std::string& filename : fileNames) {
                    files[fileCount] = filename;
                    fileCount++;
                }
            }
        }
    }
}

void System::setUserAction(char input) {
    userAction = input;
}

void System::checkBoot(sf::RenderWindow* window) {
    sf::Text password(terminal.font, "", 40);
    apolloHead.setScale({1.6f, 1.6f});
    apolloTexture.setSmooth(false);
    columnTexture.setSmooth(false);
    sf::Vector2f pos(525.f, 350.f);
    pos.x = std::round(pos.x);
    pos.y = std::round(pos.y);
    apolloHead.setPosition(pos);
    
    switch (bootStep) {
        case Initial: {
            if (!bootTitleDone) {
                if (bootTitleVisibleChars < bootTitleFull.size()) {
                    if (bootTitleTimer.getElapsedTime().asSeconds() >= bootTitleDelay) {
                        ++bootTitleVisibleChars;
                        bootTitleTimer.restart();
                        apolloHead.setColor(sf::Color(255, 255, 255, (42*bootTitleVisibleChars)));
                    }
                } else {
                    apolloHead.setColor(sf::Color(255, 255, 255, 255));
                    if (bootTitleTimer.getElapsedTime().asSeconds() >= 1.f) {
                        bootTitleDone = true;
                        bootStep = Password;
                    }
                }
            }
            title.setFillColor(sf::Color::White);
            title.setPosition(sf::Vector2f(580.f, 180.f));
            title.setString(bootTitleFull.substr(0, bootTitleVisibleChars));
            window->draw(title);
            window->draw(apolloHead);
            // Draw columns
            columnSprite.setScale({1.75f, 1.75f});
            columnSprite.setPosition(sf::Vector2f(200.f, 200.f));
            window->draw(columnSprite);
            columnSprite.setPosition(sf::Vector2f(75.f, 200.f));
            window->draw(columnSprite);
            columnSprite.setScale({-1.75f, 1.75f});
            columnSprite.setPosition(sf::Vector2f(1300.f, 200.f));
            window->draw(columnSprite);
            columnSprite.setPosition(sf::Vector2f(1425.f, 200.f));
            window->draw(columnSprite);
            
            break; }
        case Password: {
            // Still display the title
            title.setFillColor(sf::Color::White);
            title.setPosition(sf::Vector2f(580.f, 160.f));
            title.setString(bootTitleFull);
            window->draw(title);
            sf::Vector2f pos(525.f, 310.f);
            pos.x = std::round(pos.x);
            pos.y = std::round(pos.y);
            apolloHead.setPosition(pos);
            window->draw(apolloHead);
            columnSprite.setScale({1.75f, 1.75f});
            columnSprite.setPosition(sf::Vector2f(200.f, 200.f));
            window->draw(columnSprite);
            columnSprite.setPosition(sf::Vector2f(75.f, 200.f));
            window->draw(columnSprite);
            columnSprite.setScale({-1.75f, 1.75f});
            columnSprite.setPosition(sf::Vector2f(1300.f, 200.f));
            window->draw(columnSprite);
            columnSprite.setPosition(sf::Vector2f(1425.f, 200.f));
            window->draw(columnSprite);
            
            // Create masked password
            std::string masked_password(input.length(), '*');
            password.setFillColor(sf::Color::White);
            password.setPosition(sf::Vector2f(530.f, 650.f));
            password.setString("Password: " + masked_password);
            window->draw(password);
            
            
            // onSubmit
            if (userAction == 'S') {
                // Grab the props
                PropertiesParser props("/Users/you/Apollo/apollo_project/apollo.properties");
                
                if (input == props.getProperty("apollo.password", "")) { // PASSWORD FOR THE SYSTEM
                    bootStep = Anim;
                }
                userAction = ' ';
            }
            break; }
        case Anim:
            // Anim break
            
            
            // Then exit
            bootStep = Exit;
            break;
        case Exit:
            // Reached explicit exit state; mark finished
            bootStep = Finished;
            break;
        case Finished:
            bootScreen = false;
            break;
    }
}

void System::setRootDir(std::string path) {
    // Assign to member rootDir (avoid shadowing with a new local variable)
    rootDir = path;
    
    // sert current to root
    currDir = rootDir;
    
    terminal.setCommand("cd " +currDir.string());
    terminal.executeCommand();
    terminal.setCommand("");
}

bool System::isRoot() {
    // If connected to remote, check if at ~/APOLLO or the expanded form
    if (terminal.isRemoteConnected()) {
        RemoteServer* remote = terminal.getRemoteServer();
        if (remote) {
            std::string workingDir = remote->getRemoteWorkingDir();
            // Check if we're at ~/APOLLO or the actual expanded path containing APOLLO
            return workingDir == "~/APOLLO" || 
                   (workingDir.find("/APOLLO") != std::string::npos && 
                    workingDir.find("/APOLLO") == workingDir.length() - 7);
        }
    }
    // Otherwise check local filesystem
    return (rootDir == currDir);
}

int System::getItemCount() {
    return (folderCount+fileCount);
}

bool System::isBooting() {
    return bootScreen;
}

void System::setInput(std::string inString) {
    input = inString;
}

void System::executeClick(int locationX, int locationY) {
    // Check if rootView
    if (isRoot()) {
        int onFolder = (locationY*6)+locationX; // the folder to cd into
        
        if (onFolder < folderCount) {
            terminal.setCommand("cd "+folders[onFolder]);
            terminal.executeCommand();
            terminal.setCommand("");
        }
    } else {
        std::string fileString = "";
        int filePage = terminal.filePage;
        int box = locationY;
        bool isDirectory = false;
        
        if (box+(16*(filePage-1)) < folderCount) {
            fileString = folders[box+(16*(filePage-1))];
            isDirectory = true; // folders array contains only directories
        } else if (box+(16*(filePage-1))-folderCount < fileCount) {
            fileString = files[box+(16*(filePage-1))-folderCount];
            isDirectory = false;
        }
        
        if (fileString != "") {
            if (isDirectory) {
                terminal.setCommand("cd "+fileString);
                terminal.executeCommand();
                terminal.setCommand("");
            } else {
                // For files, use appropriate open command
                if (terminal.isRemoteConnected()) {
                    // Remote file: use scp to download, open in local editor, then upload back
                    terminal.setCommand("open "+fileString);
                    terminal.executeCommand();
                    terminal.setCommand("");
                } else {
                    // Local file: open normally
                    terminal.setCommand("open "+fileString);
                    terminal.executeCommand();
                    terminal.setCommand("");
                }
            }
        }
    }
}

void System::display(sf::RenderWindow* window) {
    // use window to display stuff
    if (bootScreen) {
        checkBoot(window);
    }
    else if (isRoot()) {
        // Draw Apollo Box
        sf::Vector2f boxPos(20.f, 32.f);
        sf::Vector2f boxSize(955.f, 774.f);
        sf::RectangleShape apolloView(boxSize);
        apolloView.setFillColor(sf::Color(18, 18, 18));
        apolloView.setOutlineThickness(2.f);
        apolloView.setOutlineColor(sf::Color(36, 36, 36));
        apolloView.setPosition(boxPos);
        window->draw(apolloView);
        
        // Draw background
        
        // Title with remote indicator
        title.setCharacterSize(65);
        title.setFillColor(sf::Color::White);
        title.setString(bootTitleFull);
        
        // Center title based on its width
        sf::FloatRect titleBounds = title.getLocalBounds();
        float titleX = (boxSize.x - titleBounds.size.x) / 2.0f + boxPos.x;
        title.setPosition(sf::Vector2f(titleX, 40.f));
        window->draw(title);
        
        // Draw [REMOTE] indicator separately in smaller orange text
        if (terminal.isRemoteConnected()) {
            sf::Text remoteIndicator(terminal.font, "[REMOTE]", 35);
            remoteIndicator.setFillColor(sf::Color(255, 140, 0)); // Orange
            sf::FloatRect indicatorBounds = remoteIndicator.getLocalBounds();
            float indicatorX = titleX + titleBounds.size.x + 10.f;
            remoteIndicator.setPosition(sf::Vector2f(indicatorX, 55.f));
            window->draw(remoteIndicator);
        }
        
        // Draw Folders
        // - start with hitboxes
        sf::Vector2f folderBoxSize(120.f, 120.f);
        sf::RectangleShape folderBox(folderBoxSize);
        folderBox.setFillColor(sf::Color(20, 20, 20));
        folderBox.setOutlineThickness(2.f);
        folderBox.setOutlineColor(sf::Color(36, 36, 36));
        // - draw text and file icon
        sf::Text folderName(terminal.font);
        folderName.setFillColor(sf::Color::White);
        folderName.setCharacterSize(14);
        
        sf::FloatRect folderSpriteRect = folderSprite.getLocalBounds();
        folderSprite.setOrigin(sf::Vector2f(
            folderSpriteRect.position.x + folderSpriteRect.size.x / 2.0f,
            folderSpriteRect.position.y + folderSpriteRect.size.y / 2.0f
        ));
        folderSprite.setScale({1.75f, 1.75f});
        
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 6; col++) {
                // define folder number tracker
                int onFolder = (row*6)+col;
                
                if (folderCount > onFolder) {
                    folderBox.setPosition(sf::Vector2f((70.f+140.f*col+5.f*col),(230.f+135.f*row)));
                    window->draw(folderBox);
                    
                    folderSprite.setPosition(sf::Vector2f((132.5f+140.f*col+5.f*col),(285.f+135.f*row)));
                    window->draw(folderSprite);
                    
                    folderName.setString(folders[onFolder]);
                    sf::FloatRect folderRect = folderName.getLocalBounds();
                    sf::Vector2f origin(
                        folderRect.position.x + folderRect.size.x / 2.0f,
                        folderRect.position.y + folderRect.size.y / 2.0f
                    );

                    // Snap origin to integer pixels too
                    origin.x = std::round(origin.x);
                    origin.y = std::round(origin.y);
                    folderName.setOrigin(origin);
                    
                    // Center it normally
                    sf::Vector2f pos((130.f + 140.f * col + 5.f * col), (335.f + 135.f * row));

                    // Snap to pixel grid
                    pos.x = std::round(pos.x);
                    pos.y = std::round(pos.y);

                    folderName.setPosition(pos);
                    window->draw(folderName);
                }
            }
        }
        
        // draw the terminal
        terminal.draw(window);
    } else {
        // Draw Apollo Box
        sf::Vector2f boxPos(20.f, 32.f);
        sf::Vector2f boxSize(955.f, 774.f);
        sf::RectangleShape apolloView(boxSize);
        apolloView.setFillColor(sf::Color(18, 18, 18));
        apolloView.setOutlineThickness(2.f);
        apolloView.setOutlineColor(sf::Color(36, 36, 36));
        apolloView.setPosition(boxPos);
        window->draw(apolloView);
        // Draw background
        // Title with remote indicator
        title.setCharacterSize(65);
        title.setFillColor(sf::Color::White);
        title.setString(bootTitleFull);
        
        // Center title based on its width
        sf::FloatRect titleBounds = title.getLocalBounds();
        float titleX = (boxSize.x - titleBounds.size.x) / 2.0f + boxPos.x;
        title.setPosition(sf::Vector2f(titleX, 40.f));
        window->draw(title);
        
        // Draw [REMOTE] indicator separately in smaller orange text
        if (terminal.isRemoteConnected()) {
            sf::Text remoteIndicator(terminal.font, "[REMOTE]", 35);
            remoteIndicator.setFillColor(sf::Color(255, 140, 0)); // Orange
            sf::FloatRect indicatorBounds = remoteIndicator.getLocalBounds();
            float indicatorX = titleX + titleBounds.size.x + 10.f;
            remoteIndicator.setPosition(sf::Vector2f(indicatorX, 55.f));
            window->draw(remoteIndicator);
        }
        // Draw files
        sf::Vector2f fileBoxSize(875.f, 32.f);
        sf::RectangleShape fileBox(fileBoxSize);
        fileBox.setFillColor(sf::Color(20, 20, 20));
        fileBox.setOutlineThickness(2.f);
        fileBox.setOutlineColor(sf::Color(36, 36, 36));
        
        sf::FloatRect fileRect = fileBox.getLocalBounds();
        sf::Vector2f origin(
            fileRect.position.x + fileRect.size.x / 2.0f,
            fileRect.position.y + fileRect.size.y / 2.0f
        );
        
        sf::Text fileName(terminal.font);
        fileName.setFillColor(sf::Color::White);
        fileName.setCharacterSize(20);
        
        sf::Text pageString(terminal.font);
        pageString.setFillColor(sf::Color::White);
        pageString.setCharacterSize(18);
        
        // Repeat over files shown
        for (int box = 0; box < 16; box++) {
            std::string fileString = "";
            int filePage = terminal.filePage;
            if (box+(16*(filePage-1)) < folderCount) {
                // Is a folder
                fileName.setFillColor(sf::Color(235,235,180));
                fileString = folders[box+(16*(filePage-1))];
            } else if (box+(16*(filePage-1))-folderCount < fileCount) {
                // Is a file
                fileName.setFillColor(sf::Color::White);
                fileString = files[box+(16*(filePage-1))-folderCount];
            }
            
            if (fileString != "") {
                fileBox.setPosition(sf::Vector2f(60.f,(150.f+39.f*box)));
                window->draw(fileBox);
                
                fileName.setString(fileString);
                sf::FloatRect fileNameRect = fileName.getLocalBounds();
                sf::Vector2f origin(
                    fileNameRect.position.x + fileNameRect.size.x / 2.0f,
                    fileNameRect.position.y + fileNameRect.size.y / 2.0f
                );
                origin.x = std::round(origin.x);
                origin.y = std::round(origin.y);
                fileName.setOrigin(origin);
                
                sf::Vector2f pos(510.f,(168.f+39.f*box));

                // Snap to pixel grid
                pos.x = std::round(pos.x);
                pos.y = std::round(pos.y);

                fileName.setPosition(pos);
                
                window->draw(fileName);
                
                // Draw Page Number Indicator
                int totalPages = (folderCount+fileCount)/16;
                if ((folderCount+fileCount) % 16 != 0) totalPages++; // Add one if not on page turn
                
                pageString.setString("Page: "+std::to_string(filePage)+"/"+std::to_string(totalPages));
                
                sf::FloatRect pageNameRect = pageString.getLocalBounds();
                sf::Vector2f originPage(
                    pageNameRect.position.x + pageNameRect.size.x / 2.0f,
                    pageNameRect.position.y + pageNameRect.size.y / 2.0f
                );
                originPage.x = std::round(originPage.x);
                originPage.y = std::round(originPage.y);
                pageString.setOrigin(originPage);
                
                sf::Vector2f posPage(510.f,(168.f+39.f*16));

                // Snap to pixel grid
                posPage.x = std::round(posPage.x);
                posPage.y = std::round(posPage.y);

                pageString.setPosition(posPage);
                
                window->draw(pageString);
            }
        }
        
        // draw the terminal
        terminal.draw(window);
    }
}
