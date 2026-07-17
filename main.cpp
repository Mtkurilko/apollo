#include <SFML/Graphics.hpp>
#include <SFML/Window/Event.hpp>

#include <string>

#include "System.h"
#include "Terminal.h"

int main() {
    sf::RenderWindow window(sf::VideoMode({3024, 1964}), "Apollo");

    // Vsync instead of a sleep-based frame limiter: smoother pacing and no
    // coarse sleep jitter between a keystroke and the frame that shows it.
    window.setVerticalSyncEnabled(true);
    window.setKeyRepeatEnabled(true);

    System theSystem;
    theSystem.setRootDir("/Users/you/Apollo");

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
