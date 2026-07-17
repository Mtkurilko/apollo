#ifndef TERMINAL_H
#define TERMINAL_H

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <SFML/Graphics.hpp>

#include "RemoteServer.h"

class Terminal {
public:
    Terminal();
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    // --- input ------------------------------------------------------------
    // Returns true when the terminal consumed the key.
    bool onKey(const sf::Event::KeyPressed& key);
    void onText(char32_t unicode);
    void onScroll(float delta);

    // --- driving the terminal programmatically (used by System) -----------
    void setCommand(std::string input);
    void executeCommand();

    // --- per-frame --------------------------------------------------------
    // Moves any output produced by the worker thread into the scrollback.
    void pump();
    void draw(sf::RenderWindow* window);

    void addHistory(std::string input);
    void autocomplete(std::string& line);

    bool isRemoteConnected() const;
    std::vector<std::pair<std::string, bool>> getRemoteDirectoryListing();
    RemoteServer* getRemoteServer() const;

    // True once after anything that may have changed the working directory.
    bool consumeDirectoryChanged();
    bool isBusy() const { return commandRunning.load(std::memory_order_relaxed); }

    sf::Font font;     // Apollo chrome; System draws with this
    sf::Font monoFont; // terminal pane
    int filePage;
    bool quitRequested = false;

private:
    // --- scrollback -------------------------------------------------------
    static constexpr std::size_t kMaxScrollback = 10000;
    std::deque<std::string> scrollback;
    int scrollOffset = 0; // wrapped lines scrolled up from the bottom

    // --- current input line ----------------------------------------------
    std::string command;
    std::size_t cursor = 0;

    // --- command history --------------------------------------------------
    static constexpr std::size_t kMaxCommandHistory = 500;
    std::vector<std::string> cmdHistory;
    int historyPos = -1;     // -1 means "editing a fresh line"
    std::string stashedLine; // the fresh line, held while browsing history

    // --- asynchronous execution -------------------------------------------
    std::thread worker;
    std::mutex outMutex;
    std::vector<std::string> pendingOutput;
    std::atomic<bool> commandRunning{false};
    std::atomic<int> childPid{-1};
    std::atomic<bool> dirChanged{true};

    // --- layout -----------------------------------------------------------
    unsigned int lineSize = 17;
    float lineSpacing = 3.f;
    float glyphAdvance = 9.f; // monospace cell width, measured from the font
    sf::Vector2f boxPos{995.f, 32.f};
    sf::Vector2f boxSize{500.f, 774.f};
    float padX = 14.f;
    float padY = 14.f;
    sf::Clock cursorBlink;

    std::unique_ptr<RemoteServer> remoteServer;

    // --- helpers ----------------------------------------------------------
    std::vector<std::string> tokenize(const std::string& line);
    void submit();
    bool runBuiltin(const std::string& name, const std::string& argument);
    void runAsync(const std::string& shellCommand);
    void runRemote(const std::string& shellCommand);
    void cancelCommand();
    void reapWorker();
    void pushLine(std::string s);
    void appendOutputBlock(const std::string& text);

    std::size_t columns() const;
    std::vector<std::string> wrap(const std::string& s, std::size_t cols) const;
    void recallHistory(int direction);
    void rememberCommand(const std::string& line);
    void scrollToBottom();
    std::size_t prevWordBoundary() const;
    std::size_t nextWordBoundary() const;

    void openTerminalApp();
};

#endif // TERMINAL_H
