#include "System.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "AppPaths.h"
#include "Version.h"

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

    // No configuration yet: onboard instead of asking for a password nobody
    // has set.
    if (!terminal.config().exists()) {
        beginSetup();
        bootTitleVisibleChars = bootTitleFull.size();
        bootTitleDone = true;
    }
}

// ---------------------------------------------------------------------------
// First-run wizard
// ---------------------------------------------------------------------------

void System::buildSetupFields() {
    Config& cfg = terminal.config();
    setupFields.clear();

    setupFields.push_back({"password", "Choose a password to unlock Apollo",
                           "This guards the boot screen. Press Enter when done.",
                           cfg.password(), true, false});

    setupFields.push_back({"root", "Which directory should Apollo open?",
                           "Press Enter to accept the default. It is created if missing.",
                           cfg.rootDir(), false, false});

    setupFields.push_back({"connName", "Name an SSH connection, or press Enter to skip",
                           "A short label like 'lab' or 'pi'. You can add more later with "
                           "`apollo config add`.",
                           "", false, true});

    setupFields.push_back({"connUserHost", "Who and where? (user@host)",
                           "For example alice@10.0.0.5", "", false, false});

    setupFields.push_back({"connPort", "SSH port", "Press Enter for the default.", "22", false, true});

    setupFields.push_back({"connKey", "Path to a private key, or press Enter to use a password",
                           "A key is safer: passwords are visible to `ps` while commands run.",
                           "~/.ssh/id_ed25519", false, true});

    setupFields.push_back({"connPassword", "Password for that host",
                           "Stored in ~/.apollo/config.properties, readable only by you.", "", true,
                           false});
}

void System::beginOnboarding() {
    beginSetup();
}

void System::beginSetup() {
    buildSetupFields();
    setupIndex = 0;
    setupAnswers.clear();
    setupDone.clear();
    setupError.clear();
    input.clear();
    bootStep = Setup;
    bootScreen = true;

    // Skip any leading field that does not apply to this configuration.
    while (setupIndex < setupFields.size() && !fieldApplies(setupIndex)) ++setupIndex;
}

bool System::fieldApplies(std::size_t index) const {
    if (index >= setupFields.size()) return false;
    const std::string& key = setupFields[index].key;

    const auto named = setupAnswers.find("connName");
    const bool addingConnection = named != setupAnswers.end() && !named->second.empty();

    if (key == "connUserHost" || key == "connPort" || key == "connKey") return addingConnection;
    if (key == "connPassword") {
        if (!addingConnection) return false;
        // Only ask for a password when no key was given.
        const auto key_it = setupAnswers.find("connKey");
        return key_it == setupAnswers.end() || key_it->second.empty();
    }
    return true;
}

void System::advanceSetup() {
    do {
        ++setupIndex;
    } while (setupIndex < setupFields.size() && !fieldApplies(setupIndex));

    if (setupIndex >= setupFields.size()) finishSetup();
}

void System::submitSetupAnswer() {
    if (setupIndex >= setupFields.size()) return;

    const SetupField& field = setupFields[setupIndex];
    std::string value = input;
    input.clear();

    // An empty answer takes the default, except where blank means "skip".
    if (value.empty() && !field.defaultValue.empty() && !field.optional) {
        value = field.defaultValue;
    }
    if (value.empty() && field.optional && field.key == "connPort") {
        value = field.defaultValue;
    }

    setupError.clear();

    if (field.key == "password") {
        if (value.empty()) {
            setupError = "A password is required.";
            return;
        }
    } else if (field.key == "root") {
        const fs::path expanded = apollo::expandUser(value);
        std::error_code ec;
        if (!fs::exists(expanded, ec)) {
            fs::create_directories(expanded, ec);
            if (ec) {
                setupError = "Could not create " + expanded.string();
                return;
            }
            setupDone.push_back("Created " + expanded.string());
        }
        value = expanded.string();
    } else if (field.key == "connName") {
        if (!value.empty() && !Config::isValidConnectionName(value)) {
            setupError = "Use letters, digits, hyphens and underscores only.";
            return;
        }
    } else if (field.key == "connUserHost") {
        if (value.find('@') == std::string::npos || value.front() == '@' || value.back() == '@') {
            setupError = "Expected user@host, for example alice@10.0.0.5";
            return;
        }
    } else if (field.key == "connPort") {
        if (!std::all_of(value.begin(), value.end(),
                         [](unsigned char c) { return std::isdigit(c); })) {
            setupError = "Port must be a number.";
            return;
        }
    } else if (field.key == "connKey") {
        if (!value.empty()) {
            std::error_code ec;
            if (!fs::exists(apollo::expandUser(value), ec)) {
                setupError = "No key at " + value + " — press Enter to use a password instead.";
                // Blank the default so the next Enter genuinely skips.
                setupFields[setupIndex].defaultValue.clear();
                return;
            }
        }
    } else if (field.key == "connPassword") {
        if (value.empty()) {
            setupError = "Enter a password, or go back and supply a key.";
            return;
        }
    }

    setupAnswers[field.key] = value;

    if (!field.mask && !value.empty() && field.key != "root") {
        setupDone.push_back(field.prompt + ": " + value);
    } else if (field.mask && !value.empty()) {
        setupDone.push_back(field.prompt + ": " + std::string(value.size(), '*'));
    }

    advanceSetup();
}

void System::finishSetup() {
    Config& cfg = terminal.config();

    cfg.setPassword(setupAnswers["password"]);
    cfg.setRootDir(setupAnswers["root"]);

    const std::string name = setupAnswers["connName"];
    if (!name.empty()) {
        Connection conn;
        conn.name = name;
        const std::string userHost = setupAnswers["connUserHost"];
        const std::size_t at = userHost.find('@');
        conn.user = userHost.substr(0, at);
        conn.host = userHost.substr(at + 1);
        conn.port = setupAnswers.count("connPort") ? setupAnswers["connPort"] : "22";
        conn.keyPath = setupAnswers["connKey"];
        conn.password = setupAnswers["connPassword"];
        cfg.putConnection(conn);
        if (cfg.defaultConnection().empty()) cfg.setDefaultConnection(name);
    }

    if (!cfg.save()) {
        setupError = cfg.lastError();
        // Stay on the last question rather than losing what was typed.
        setupIndex = setupFields.size() - 1;
        return;
    }

    bootStep = Finished;
}

// ---------------------------------------------------------------------------
// Directory model
// ---------------------------------------------------------------------------

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

void System::applyRootDir() {
    rootDir = apollo::expandUser(terminal.config().rootDir());
    std::error_code ec;
    if (!fs::exists(rootDir, ec)) fs::create_directories(rootDir, ec);

    currDir = rootDir;
    terminal.setCommand("cd " + rootDir.string());
    terminal.executeCommand();
    terminal.setCommand("");

    // The cd echoes into the scrollback; start the session with a clean slate.
    terminal.clearScrollback();
    terminal.printWelcome();

    listingDirty = true;
}

void System::update() {
    // Hand the render thread whatever the command worker produced this frame.
    terminal.pump();

    if (terminal.consumeOnboardRequest()) beginSetup();

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

bool System::isMaskingInput() const {
    if (bootStep == Password) return true;
    return bootStep == Setup && setupIndex < setupFields.size() && setupFields[setupIndex].mask;
}

// ---------------------------------------------------------------------------
// Boot screen
// ---------------------------------------------------------------------------

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

void System::drawSetup(sf::RenderWindow* window) {
    title.setCharacterSize(100);
    title.setFillColor(sf::Color::White);
    title.setPosition({580.f, 120.f});
    title.setString(bootTitleFull);
    window->draw(title);

    apolloHead.setScale({1.1f, 1.1f});
    apolloHead.setColor(sf::Color(255, 255, 255, 255));
    apolloHead.setPosition({690.f, 250.f});
    window->draw(apolloHead);
    drawColumns(window);

    sf::Text heading(terminal.font, "First-time setup", 34);
    heading.setFillColor(sf::Color(235, 235, 180));
    heading.setPosition({300.f, 520.f});
    window->draw(heading);

    sf::Text subheading(terminal.font,
                        "Answer a few questions. Everything here can be changed later with "
                        "`apollo config`.",
                        22);
    subheading.setFillColor(sf::Color(150, 150, 150));
    subheading.setPosition({300.f, 562.f});
    window->draw(subheading);

    float y = 610.f;

    // Confirmations for what has already been answered.
    sf::Text doneLine(terminal.font, "", 20);
    doneLine.setFillColor(sf::Color(120, 160, 120));
    for (const auto& line : setupDone) {
        doneLine.setString("  " + line);
        doneLine.setPosition({300.f, std::round(y)});
        window->draw(doneLine);
        y += 28.f;
    }
    y += 20.f;

    if (setupIndex >= setupFields.size()) return;
    const SetupField& field = setupFields[setupIndex];

    sf::Text prompt(terminal.font, field.prompt, 28);
    prompt.setFillColor(sf::Color::White);
    prompt.setPosition({300.f, std::round(y)});
    window->draw(prompt);
    y += 36.f;

    if (!field.help.empty()) {
        sf::Text help(terminal.font, field.help, 20);
        help.setFillColor(sf::Color(150, 150, 150));
        help.setPosition({300.f, std::round(y)});
        window->draw(help);
        y += 32.f;
    }

    const std::string shown = field.mask ? std::string(input.size(), '*') : input;
    std::string answerLine = "> " + shown;
    if (input.empty() && !field.defaultValue.empty()) {
        answerLine += field.mask ? "" : "[" + field.defaultValue + "]";
    }

    sf::Text answer(terminal.font, answerLine, 26);
    answer.setFillColor(input.empty() && !field.defaultValue.empty() ? sf::Color(130, 130, 130)
                                                                     : sf::Color(235, 235, 180));
    answer.setPosition({300.f, std::round(y)});
    window->draw(answer);
    y += 44.f;

    if (!setupError.empty()) {
        sf::Text error(terminal.font, setupError, 22);
        error.setFillColor(sf::Color(230, 120, 100));
        error.setPosition({300.f, std::round(y)});
        window->draw(error);
    }

    sf::Text progress(terminal.font,
                      "Step " + std::to_string(setupIndex + 1) + " of " +
                          std::to_string(setupFields.size()),
                      18);
    progress.setFillColor(sf::Color(110, 110, 110));
    progress.setPosition({300.f, 760.f});
    window->draw(progress);
}

void System::checkBoot(sf::RenderWindow* window) {
    apolloHead.setScale({1.6f, 1.6f});
    apolloTexture.setSmooth(false);
    columnTexture.setSmooth(false);

    switch (bootStep) {
        case Setup:
            drawSetup(window);
            if (userAction == 'S') {
                submitSetupAnswer();
                userAction = ' ';
            }
            break;

        case Initial: {
            if (!bootTitleDone) {
                if (bootTitleVisibleChars < bootTitleFull.size()) {
                    if (bootTitleTimer.getElapsedTime().asSeconds() >= bootTitleDelay) {
                        ++bootTitleVisibleChars;
                        bootTitleTimer.restart();
                        apolloHead.setColor(sf::Color(
                            255, 255, 255, static_cast<std::uint8_t>(42 * bootTitleVisibleChars)));
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

            if (!setupError.empty()) {
                sf::Text error(terminal.font, setupError, 24);
                error.setFillColor(sf::Color(230, 120, 100));
                error.setPosition({530.f, 700.f});
                window->draw(error);
            }

            if (userAction == 'S') {
                if (input == terminal.config().password()) {
                    setupError.clear();
                    bootStep = Anim;
                } else {
                    setupError = "Incorrect password.";
                    input.clear();
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
            applyRootDir();
            break;
    }
}

// ---------------------------------------------------------------------------
// Browser
// ---------------------------------------------------------------------------

bool System::isRoot() {
    if (terminal.isRemoteConnected()) {
        if (RemoteServer* remote = terminal.getRemoteServer()) {
            const std::string workingDir = remote->getRemoteWorkingDir();
            return workingDir == "~/APOLLO" ||
                   (workingDir.size() >= 7 &&
                    workingDir.compare(workingDir.size() - 7, 7, "/APOLLO") == 0);
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

    // Name the live connection, since Apollo can hold several.
    if (RemoteServer* remote = terminal.getRemoteServer()) {
        if (remote->isConnected()) {
            sf::Text remoteIndicator(terminal.font, "[" + remote->getName() + "]", 35);
            remoteIndicator.setFillColor(sf::Color(255, 140, 0));
            remoteIndicator.setPosition({titleX + titleBounds.size.x + 10.f, 55.f});
            window->draw(remoteIndicator);
        }
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
                folderName.setPosition(
                    {std::round(130.f + 145.f * col), std::round(335.f + 135.f * row)});
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

        sf::Text pageString(
            terminal.font, "Page: " + std::to_string(filePage) + "/" + std::to_string(totalPages), 18);
        pageString.setFillColor(sf::Color::White);
        const sf::FloatRect pageRect = pageString.getLocalBounds();
        pageString.setOrigin({std::round(pageRect.position.x + pageRect.size.x / 2.0f),
                              std::round(pageRect.position.y + pageRect.size.y / 2.0f)});
        pageString.setPosition({510.f, std::round(168.f + 39.f * 16)});
        window->draw(pageString);
    }

    terminal.draw(window);
}
