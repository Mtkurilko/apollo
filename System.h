#ifndef SYSTEM_H
#define SYSTEM_H

#include "Terminal.h"
#include <string>
#include <array>
#include <filesystem>
#include <SFML/Graphics.hpp>
#include <chrono>
#include <ctime>

class System {
public:
    System();
    void update();
    void setUserAction(char input);
    Terminal terminal;
    void setRootDir(std::string path);
    bool isRoot();
    int getItemCount();
    void display(sf::RenderWindow* window);
    bool isBooting();
    void setInput(std::string inString);
    void executeClick(int locationX, int locationY);
private:
    void checkBoot(sf::RenderWindow* window);
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
    long last_run_time;
    std::array<std::string, 1000> folders;
    std::array<std::string, 1000> files;
    int folderCount;
    int fileCount;
    enum Boot { Initial, Password, Anim, Exit, Finished };
    Boot bootStep;
    // boot typing
    std::string bootTitleFull = "APOLLO";
    std::size_t bootTitleVisibleChars = 0;
    bool bootTitleDone = false;
    std::string input;
    sf::Clock bootTitleTimer;
    float bootTitleDelay = 0.18f;
};

#endif // SYSTEM_H
