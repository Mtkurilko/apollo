#include <SFML/Graphics.hpp>
#include <SFML/Window/Event.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "AppPaths.h"
#include "Config.h"
#include "ConfigCommand.h"
#include "System.h"
#include "Terminal.h"
#include "Version.h"

namespace fs = std::filesystem;

namespace {

void printUsage() {
    std::cout
        << "Apollo " APOLLO_VERSION " — a desktop shell for your workspace\n"
           "\n"
           "  apollo                    launch Apollo\n"
           "  apollo setup              (re-)run the first-run wizard\n"
           "  apollo config ...         view and edit configuration\n"
           "  apollo doctor             check that everything is installed correctly\n"
           "  apollo --version          print the version\n"
           "  apollo --help             this message\n"
           "\n"
           "Run `apollo config` for the full list of configuration commands.\n";
}

bool commandExists(const std::string& name) {
    return std::system(("command -v " + name + " >/dev/null 2>&1").c_str()) == 0;
}

int runDoctor() {
    Config cfg;
    cfg.load();

    int problems = 0;
    const auto ok = [](const std::string& msg) { std::cout << "  ok    " << msg << "\n"; };
    const auto warn = [&](const std::string& msg) {
        std::cout << "  warn  " << msg << "\n";
    };
    const auto bad = [&](const std::string& msg) {
        std::cout << "  FAIL  " << msg << "\n";
        ++problems;
    };

    std::cout << "Apollo " APOLLO_VERSION " doctor\n\n";

    // Assets
    const fs::path root = apollo::appRoot();
    if (fs::exists(root / "assets" / "GFSNeohellenic-Regular.ttf")) {
        ok("assets found at " + root.string());
    } else {
        bad("assets not found (looked in " + root.string() + ")");
        std::cout << "        Set APOLLO_HOME to the directory containing assets/, or reinstall.\n";
    }

    // Config
    if (cfg.exists()) {
        ok("config at " + apollo::configPath().string());
        if (cfg.password().empty()) warn("no boot password set — run `apollo setup`");
        if (cfg.rootDir().empty() || !fs::exists(cfg.rootDir())) {
            warn("apollo.root does not exist: " + cfg.rootDir());
        } else {
            ok("workspace " + cfg.rootDir());
        }
    } else {
        warn("no config yet — Apollo will run the setup wizard on first launch");
    }

    // Connections
    const auto names = cfg.connectionNames();
    if (names.empty()) {
        warn("no SSH connections configured (local mode only)");
    } else {
        bool needsSshpass = false;
        for (const auto& name : names) {
            const auto conn = cfg.connection(name);
            if (!conn) {
                warn("connection '" + name + "' is incomplete");
                continue;
            }
            if (!conn->keyPath.empty()) {
                if (fs::exists(apollo::expandUser(conn->keyPath))) {
                    ok("connection '" + name + "' -> " + conn->label() + " (key)");
                } else {
                    bad("connection '" + name + "' points at a missing key: " + conn->keyPath);
                }
            } else if (!conn->password.empty()) {
                needsSshpass = true;
                ok("connection '" + name + "' -> " + conn->label() + " (password)");
            } else {
                // Freshly added by `apollo config add`; not broken, just unfinished.
                warn("connection '" + name + "' has no key or password yet");
                std::cout << "        apollo config set connection." << name
                          << ".key ~/.ssh/id_ed25519\n";
            }
        }
        if (needsSshpass && !commandExists("sshpass")) {
            bad("sshpass is not installed, but a connection uses password auth");
            std::cout << "        brew install sshpass — or switch to a key, which is safer.\n";
        }
    }

    // Tools
    commandExists("ssh") ? ok("ssh available") : bad("ssh not found");

    std::cout << "\n" << (problems == 0 ? "No problems found.\n"
                                        : std::to_string(problems) + " problem(s) found.\n");
    return problems == 0 ? 0 : 1;
}

// Returns an exit code, or -1 to fall through and open the window.
int runCli(const std::vector<std::string>& args, bool& forceSetup) {
    const std::string& first = args[0];

    if (first == "--help" || first == "-h" || first == "help") {
        printUsage();
        return 0;
    }

    if (first == "--version" || first == "-v" || first == "version") {
        std::cout << "Apollo " APOLLO_VERSION "\n";
        return 0;
    }

    if (first == "doctor") return runDoctor();

    if (first == "setup" || first == "onboard") {
        forceSetup = true;
        return -1; // the wizard lives in the window
    }

    if (first == "config") {
        Config cfg;
        cfg.load();
        const ConfigCommandResult result =
            runConfigCommand(std::vector<std::string>(args.begin() + 1, args.end()), cfg);
        for (const auto& line : result.output) {
            (result.ok ? std::cout : std::cerr) << line << "\n";
        }
        return result.ok ? 0 : 1;
    }

    std::cerr << "Unknown command: " << first << "\n";
    printUsage();
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    bool forceSetup = false;

    if (argc > 1) {
        const std::vector<std::string> args(argv + 1, argv + argc);
        const int code = runCli(args, forceSetup);
        if (code >= 0) return code;
    }

    sf::RenderWindow window(sf::VideoMode({3024, 1964}), "Apollo");

    // Vsync instead of a sleep-based frame limiter: smoother pacing and no
    // coarse sleep jitter between a keystroke and the frame that shows it.
    window.setVerticalSyncEnabled(true);
    window.setKeyRepeatEnabled(true);

    System theSystem;
    if (forceSetup) theSystem.beginOnboarding();

    // The boot screen owns its own input buffer; once booted, the Terminal
    // owns the command line and nothing here needs to mirror it.
    std::string bootInput;

    sf::Clock clickTimer;
    sf::Time lastClickTime = sf::Time::Zero;
    const sf::Time doubleClickThreshold = sf::milliseconds(300);

    while (window.isOpen()) {
        while (const std::optional<sf::Event> event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
                break;
            }

            if (const auto* keyEvent = event->getIf<sf::Event::KeyPressed>()) {
                if (theSystem.isBooting()) {
                    // Enter and Backspace are keys, not text: routing them here
                    // avoids depending on which control code TextEntered emits.
                    if (keyEvent->code == sf::Keyboard::Key::Enter) {
                        theSystem.setInput(bootInput);
                        theSystem.setUserAction('S');
                        bootInput.clear();
                    } else if (keyEvent->code == sf::Keyboard::Key::Backspace) {
                        if (!bootInput.empty()) bootInput.pop_back();
                        theSystem.setInput(bootInput);
                    }
                } else {
                    theSystem.terminal.onKey(*keyEvent);
                }
                continue;
            }

            if (const auto* textEvent = event->getIf<sf::Event::TextEntered>()) {
                // Printable characters only; everything else arrives via KeyPressed.
                const char32_t c = textEvent->unicode;
                if (c < 32 || c > 126) continue;

                if (theSystem.isBooting()) {
                    bootInput.push_back(static_cast<char>(c));
                    theSystem.setInput(bootInput);
                } else {
                    theSystem.terminal.onText(c);
                }
                continue;
            }

            if (const auto* scrollEvent = event->getIf<sf::Event::MouseWheelScrolled>()) {
                if (!theSystem.isBooting()) theSystem.terminal.onScroll(scrollEvent->delta);
                continue;
            }

            if (const auto* mouseEvent = event->getIf<sf::Event::MouseButtonPressed>()) {
                if (mouseEvent->button != sf::Mouse::Button::Left) continue;
                if (theSystem.isBooting()) continue;

                const sf::Time now = clickTimer.getElapsedTime();
                const bool isDoubleClick = (now - lastClickTime) < doubleClickThreshold;
                lastClickTime = isDoubleClick ? sf::Time::Zero : now;
                if (!isDoubleClick) continue;

                const auto& pos = mouseEvent->position;
                if (theSystem.isRoot()) {
                    // Folder grid: 6 columns x 4 rows, 145px x 135px cells.
                    const int locX = (pos.x - 70) / 145;
                    const int locY = (pos.y - 230) / 135;
                    if (pos.x >= 70 && pos.y >= 230 && locX >= 0 && locX < 6 && locY >= 0 && locY < 4) {
                        theSystem.executeClick(locX, locY);
                    }
                } else {
                    // File list: a single 875px-wide column of 39px rows.
                    const int locY = (pos.y - 150) / 39;
                    if (pos.x >= 60 && pos.x < 60 + 875 && pos.y >= 150 && locY >= 0 && locY < 16) {
                        theSystem.executeClick(0, locY);
                    }
                }
            }
        }

        if (theSystem.terminal.quitRequested) window.close();
        if (!window.isOpen()) break;

        theSystem.update();

        window.clear(sf::Color(76, 89, 105));
        theSystem.display(&window);
        window.display();
    }

    return 0;
}
