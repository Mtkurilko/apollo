#ifndef SYSTEM_H
#define SYSTEM_H

#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <SFML/Graphics.hpp>

#include "Terminal.h"

class System {
public:
    System();

    void update();
    void display(sf::RenderWindow* window);

    void setUserAction(char input);
    void setInput(std::string inString);

    bool isRoot();
    bool isBooting();
    // The setup wizard echoes typed characters itself, so main must not.
    bool isMaskingInput() const;
    int getItemCount();
    void executeClick(int locationX, int locationY);

    // Forces a directory re-read on the next update().
    void requestRefresh();

    // Enters the first-run wizard (used by `apollo setup` from the shell).
    void beginOnboarding();

    Terminal terminal;

private:
    static constexpr std::size_t kMaxEntries = 1000;

    // One question in the first-run wizard.
    struct SetupField {
        std::string key;
        std::string prompt;
        std::string help;
        std::string defaultValue;
        bool mask = false;
        bool optional = false;
    };

    void checkBoot(sf::RenderWindow* window);
    void refreshListing();
    void drawChrome(sf::RenderWindow* window);
    void drawColumns(sf::RenderWindow* window);
    void drawSetup(sf::RenderWindow* window);

    void beginSetup();
    void buildSetupFields();
    bool fieldApplies(std::size_t index) const;
    void advanceSetup();
    void submitSetupAnswer();
    void finishSetup();
    void applyRootDir();

    char userAction;
    std::filesystem::path rootDir;
    std::filesystem::path currDir;

    sf::Texture apolloTexture;
    sf::Sprite apolloHead;
    sf::Texture columnTexture;
    sf::Sprite columnSprite;
    sf::Texture folderTexture;
    sf::Sprite folderSprite;
    sf::Text title;

    bool bootScreen;

    std::array<std::string, kMaxEntries> folders;
    std::array<std::string, kMaxEntries> files;
    int folderCount;
    int fileCount;

    // Directory listings refresh on events (cd, finished command, connect),
    // not on a timer. Locally we also watch the directory's mtime so changes
    // made outside Apollo still show up.
    bool listingDirty;
    sf::Clock mtimeClock;
    std::filesystem::file_time_type lastWriteTime;

    enum Boot { Setup, Initial, Password, Anim, Exit, Finished };
    Boot bootStep;

    // --- first-run wizard -------------------------------------------------
    std::vector<SetupField> setupFields;
    std::size_t setupIndex = 0;
    std::map<std::string, std::string> setupAnswers;
    std::string setupError;
    std::vector<std::string> setupDone; // confirmations shown above the prompt

    // --- boot typing animation -------------------------------------------
    std::string bootTitleFull = "APOLLO";
    std::size_t bootTitleVisibleChars = 0;
    bool bootTitleDone = false;
    std::string input;
    sf::Clock bootTitleTimer;
    float bootTitleDelay = 0.18f;
};

#endif // SYSTEM_H
