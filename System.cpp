#include "System.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "AppPaths.h"
#include "PropertiesParser.h"

namespace fs = std::filesystem;

System::System()
    : userAction(' '),
      apolloTexture(apollo::assetPath("ApolloHead.png")),
      apolloHead(apolloTexture),
      columnTexture(apollo::assetPath("column.png")),
      columnSprite(columnTexture),
      folderTexture(apollo::assetPath("folder.png")),
      folderSprite(folderTexture),
      title(terminal.font, "", 100),
      bootScreen(true),
      folderCount(0),
      fileCount(0),
      listingDirty(true),
      bootStep(Initial) {
    rootDir = fs::current_path();
    apolloHead.setColor(sf::Color(255, 255, 255, 0));
    apolloHead.setTexture(apolloTexture, true);
}

void System::requestRefresh() {
    listingDirty = true;
}

void System::refreshListing() {
    folderCount = 0;
    fileCount = 0;

    if (terminal.isRemoteConnected()) {
        for (const auto& item : terminal.getRemoteDirectoryListing()) {
            if (item.second) {
                if (folderCount < static_cast<int>(kMaxEntries)) folders[folderCount++] = item.first;
            } else {
                if (fileCount < static_cast<int>(kMaxEntries)) files[fileCount++] = item.first;
            }
        }
        return;
    }

    std::error_code ec;
    currDir = fs::current_path(ec);
    if (ec) return;

    std::vector<std::string> folderNames;
    std::vector<std::string> fileNames;
    for (auto it = fs::directory_iterator(currDir, ec); !ec && it != fs::end(it); it.increment(ec)) {
        if (fs::is_regular_file(it->status())) fileNames.push_back(it->path().filename().string());
        else folderNames.push_back(it->path().filename().string());
    }

    // Stable alphabetical order; directory_iterator makes no ordering promise,
    // so without this the grid reshuffles on every refresh.
    std::sort(folderNames.begin(), folderNames.end());
    std::sort(fileNames.begin(), fileNames.end());

    for (const auto& name : folderNames) {
        if (folderCount >= static_cast<int>(kMaxEntries)) break;
        folders[folderCount++] = name;
    }
    for (const auto& name : fileNames) {
        if (fileCount >= static_cast<int>(kMaxEntries)) break;
        files[fileCount++] = name;
    }

    lastWriteTime = fs::last_write_time(currDir, ec);
}

void System::update() {
    // Hand the render thread whatever the command worker produced this frame.
    terminal.pump();

    if (bootScreen) return;

    if (terminal.consumeDirectoryChanged()) listingDirty = true;

    // Locally, a directory mtime check twice a second is far cheaper than a
    // full re-read, and it catches changes made outside Apollo.
    if (!listingDirty && !terminal.isRemoteConnected() &&
        mtimeClock.getElapsedTime().asSeconds() >= 0.5f) {
        mtimeClock.restart();
        std::error_code ec;
        const fs::path here = fs::current_path(ec);
        if (!ec) {
            const auto stamp = fs::last_write_time(here, ec);
            if (!ec && (here != currDir || stamp != lastWriteTime)) listingDirty = true;
        }
    }

    if (listingDirty) {
        refreshListing();
        listingDirty = false;
    }
}

void System::setUserAction(char input) {
    userAction = input;
}

void System::drawColumns(sf::RenderWindow* window) {
    columnSprite.setScale({1.75f, 1.75f});
    columnSprite.setPosition({200.f, 200.f});
    window->draw(columnSprite);
    columnSprite.setPosition({75.f, 200.f});
    window->draw(columnSprite);
    columnSprite.setScale({-1.75f, 1.75f});
    columnSprite.setPosition({1300.f, 200.f});
    window->draw(columnSprite);
    columnSprite.setPosition({1425.f, 200.f});
    window->draw(columnSprite);
}

void System::checkBoot(sf::RenderWindow* window) {
    apolloHead.setScale({1.6f, 1.6f});
    apolloTexture.setSmooth(false);
    columnTexture.setSmooth(false);

    switch (bootStep) {
        case Initial: {
            if (!bootTitleDone) {
                if (bootTitleVisibleChars < bootTitleFull.size()) {
                    if (bootTitleTimer.getElapsedTime().asSeconds() >= bootTitleDelay) {
                        ++bootTitleVisibleChars;
                        bootTitleTimer.restart();
                        apolloHead.setColor(
                            sf::Color(255, 255, 255, static_cast<std::uint8_t>(42 * bootTitleVisibleChars)));
                    }
                } else {
                    apolloHead.setColor(sf::Color(255, 255, 255, 255));
                    if (bootTitleTimer.getElapsedTime().asSeconds() >= 1.f) {
                        bootTitleDone = true;
                        bootStep = Password;
                    }
                }
            }
            apolloHead.setPosition({525.f, 350.f});
            title.setCharacterSize(100);
            title.setFillColor(sf::Color::White);
            title.setPosition({580.f, 180.f});
            title.setString(bootTitleFull.substr(0, bootTitleVisibleChars));
            window->draw(title);
            window->draw(apolloHead);
            drawColumns(window);
            break;
        }

        case Password: {
            title.setCharacterSize(100);
            title.setFillColor(sf::Color::White);
            title.setPosition({580.f, 160.f});
            title.setString(bootTitleFull);
            window->draw(title);

            apolloHead.setPosition({525.f, 310.f});
            window->draw(apolloHead);
            drawColumns(window);

            sf::Text password(terminal.font, "Password: " + std::string(input.length(), '*'), 40);
            password.setFillColor(sf::Color::White);
            password.setPosition({530.f, 650.f});
            window->draw(password);

            if (userAction == 'S') {
                PropertiesParser props(apollo::configPath().string());
                if (input == props.getProperty("apollo.password", "")) {
                    bootStep = Anim;
                }
                userAction = ' ';
            }
            break;
        }

        case Anim:
            bootStep = Exit;
            break;

        case Exit:
            bootStep = Finished;
            break;

        case Finished:
            bootScreen = false;
            listingDirty = true;
            break;
    }
}

void System::setRootDir(std::string path) {
    rootDir = path;
    currDir = rootDir;

    terminal.setCommand("cd " + currDir.string());
    terminal.executeCommand();
    terminal.setCommand("");
}

bool System::isRoot() {
    if (terminal.isRemoteConnected()) {
        if (RemoteServer* remote = terminal.getRemoteServer()) {
            const std::string workingDir = remote->getRemoteWorkingDir();
            return workingDir == "~/APOLLO" ||
                   (workingDir.size() >= 7 && workingDir.compare(workingDir.size() - 7, 7, "/APOLLO") == 0);
        }
    }
    return rootDir == currDir;
}

int System::getItemCount() {
    return folderCount + fileCount;
}

bool System::isBooting() {
    return bootScreen;
}

void System::setInput(std::string inString) {
    input = std::move(inString);
}

void System::executeClick(int locationX, int locationY) {
    if (isRoot()) {
        const int onFolder = (locationY * 6) + locationX;
        if (onFolder < folderCount) {
            terminal.setCommand("cd " + folders[onFolder]);
            terminal.executeCommand();
            terminal.setCommand("");
        }
        return;
    }

    const int index = locationY + (16 * (terminal.filePage - 1));
    std::string target;
    bool isDirectory = false;

    if (index < folderCount) {
        target = folders[index];
        isDirectory = true;
    } else if (index - folderCount < fileCount) {
        target = files[index - folderCount];
    }

    if (target.empty()) return;

    terminal.setCommand((isDirectory ? "cd " : "open ") + target);
    terminal.executeCommand();
    terminal.setCommand("");
}

void System::drawChrome(sf::RenderWindow* window) {
    const sf::Vector2f boxPos(20.f, 32.f);
    const sf::Vector2f boxSize(955.f, 774.f);

    sf::RectangleShape apolloView(boxSize);
    apolloView.setFillColor(sf::Color(18, 18, 18));
    apolloView.setOutlineThickness(2.f);
    apolloView.setOutlineColor(sf::Color(36, 36, 36));
    apolloView.setPosition(boxPos);
    window->draw(apolloView);

    title.setCharacterSize(65);
    title.setFillColor(sf::Color::White);
    title.setString(bootTitleFull);

    const sf::FloatRect titleBounds = title.getLocalBounds();
    const float titleX = (boxSize.x - titleBounds.size.x) / 2.0f + boxPos.x;
    title.setPosition({titleX, 40.f});
    window->draw(title);

    if (terminal.isRemoteConnected()) {
        sf::Text remoteIndicator(terminal.font, "[REMOTE]", 35);
        remoteIndicator.setFillColor(sf::Color(255, 140, 0));
        remoteIndicator.setPosition({titleX + titleBounds.size.x + 10.f, 55.f});
        window->draw(remoteIndicator);
    }
}

void System::display(sf::RenderWindow* window) {
    if (bootScreen) {
        checkBoot(window);
        return;
    }

    drawChrome(window);

    if (isRoot()) {
        sf::RectangleShape folderBox({120.f, 120.f});
        folderBox.setFillColor(sf::Color(20, 20, 20));
        folderBox.setOutlineThickness(2.f);
        folderBox.setOutlineColor(sf::Color(36, 36, 36));

        sf::Text folderName(terminal.font);
        folderName.setFillColor(sf::Color::White);
        folderName.setCharacterSize(14);

        const sf::FloatRect spriteRect = folderSprite.getLocalBounds();
        folderSprite.setOrigin({spriteRect.position.x + spriteRect.size.x / 2.0f,
                                spriteRect.position.y + spriteRect.size.y / 2.0f});
        folderSprite.setScale({1.75f, 1.75f});

        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 6; col++) {
                const int onFolder = (row * 6) + col;
                if (folderCount <= onFolder) continue;

                folderBox.setPosition({70.f + 145.f * col, 230.f + 135.f * row});
                window->draw(folderBox);

                folderSprite.setPosition({132.5f + 145.f * col, 285.f + 135.f * row});
                window->draw(folderSprite);

                folderName.setString(folders[onFolder]);
                const sf::FloatRect rect = folderName.getLocalBounds();
                folderName.setOrigin({std::round(rect.position.x + rect.size.x / 2.0f),
                                      std::round(rect.position.y + rect.size.y / 2.0f)});
                folderName.setPosition({std::round(130.f + 145.f * col), std::round(335.f + 135.f * row)});
                window->draw(folderName);
            }
        }
    } else {
        sf::RectangleShape fileBox({875.f, 32.f});
        fileBox.setFillColor(sf::Color(20, 20, 20));
        fileBox.setOutlineThickness(2.f);
        fileBox.setOutlineColor(sf::Color(36, 36, 36));

        sf::Text fileName(terminal.font);
        fileName.setCharacterSize(20);

        const int filePage = terminal.filePage;

        for (int box = 0; box < 16; box++) {
            const int index = box + (16 * (filePage - 1));
            std::string entry;

            if (index < folderCount) {
                fileName.setFillColor(sf::Color(235, 235, 180));
                entry = folders[index];
            } else if (index - folderCount < fileCount) {
                fileName.setFillColor(sf::Color::White);
                entry = files[index - folderCount];
            }
            if (entry.empty()) continue;

            fileBox.setPosition({60.f, 150.f + 39.f * box});
            window->draw(fileBox);

            fileName.setString(entry);
            const sf::FloatRect rect = fileName.getLocalBounds();
            fileName.setOrigin({std::round(rect.position.x + rect.size.x / 2.0f),
                                std::round(rect.position.y + rect.size.y / 2.0f)});
            fileName.setPosition({510.f, std::round(168.f + 39.f * box)});
            window->draw(fileName);
        }

        // Page indicator, drawn once rather than once per row.
        const int total = folderCount + fileCount;
        int totalPages = total / 16;
        if (total % 16 != 0) totalPages++;
        if (totalPages < 1) totalPages = 1;

        sf::Text pageString(terminal.font,
                            "Page: " + std::to_string(filePage) + "/" + std::to_string(totalPages), 18);
        pageString.setFillColor(sf::Color::White);
        const sf::FloatRect pageRect = pageString.getLocalBounds();
        pageString.setOrigin({std::round(pageRect.position.x + pageRect.size.x / 2.0f),
                              std::round(pageRect.position.y + pageRect.size.y / 2.0f)});
        pageString.setPosition({510.f, std::round(168.f + 39.f * 16)});
        window->draw(pageString);
    }

    terminal.draw(window);
}
