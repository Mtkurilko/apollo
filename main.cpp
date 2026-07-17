#include <SFML/Graphics.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>

#include <string>
#include <iostream>
#include <thread>
#include <chrono>
#include "System.h"
#include "Terminal.h"

int main()
{
    // create the window
    sf::RenderWindow window(sf::VideoMode({3024, 1964}), "Apollo");
    
    window.setFramerateLimit(60);
    
    System theSystem;
    theSystem.setRootDir("/Users/you/Apollo");
    
    sf::String playerInput;
    
    sf::Clock clickTimer;
    sf::Time lastClickTime = sf::Time::Zero;
    const sf::Time doubleClickThreshold = sf::milliseconds(300);
    
    // run the program as long as the window is open
    while (window.isOpen())
    {
        // clear the window with color
        window.clear(sf::Color(76, 89, 105));
        
        while (std::optional<sf::Event> event = window.pollEvent())
        {
            if (const auto* keyEvent = event->getIf<sf::Event::KeyPressed>())
            {
                if (keyEvent->code == sf::Keyboard::Key::Tab)
                {
                    std::string current = playerInput.toAnsiString();
                    theSystem.terminal.autocomplete(current);
                    playerInput = current;
                    theSystem.terminal.setCommand(current);
                }
            }
            // Close event
            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }
            
            // Text input
            if (const auto* textEvent = event->getIf<sf::Event::TextEntered>())
            {
                if (textEvent->unicode < 128)
                {
                    char entered = static_cast<char>(textEvent->unicode);
                    
                    std::string playerText = playerInput.toAnsiString();
                    
                    if (entered == '\b')
                    {
                        if (!playerInput.isEmpty())
                        {
                            playerInput.erase(playerInput.getSize() - 1);
                        }
                    }
                    
                    else if (entered == '\n')
                    {
                        std::string finalCommand = playerInput.toAnsiString();
                        
                        std::string currentText = playerInput.toAnsiString();
                        
                        if (theSystem.isBooting()) {
                            theSystem.setInput(currentText);
                            theSystem.setUserAction('S');
                        } else {
                            theSystem.terminal.setCommand(currentText);
                            
                            theSystem.terminal.executeCommand();
                        }
                        playerInput.clear();
                    }
                    
                    else if (entered != '\r')
                    {
                        if (entered != '\t') playerInput += entered; // Tab handled in KeyPressed
                    }
                    
                    std::string currentText = playerInput.toAnsiString();
                    
                    if (theSystem.isBooting()) {
                        std::thread myThread([&theSystem, &currentText]() {
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                            theSystem.setInput(currentText);
                        });
                        myThread.detach();
                    } else {
                        theSystem.terminal.setCommand(currentText);
                    }
                }
            }
            
            // Mouse button pressed
            if (const auto* mouseEvent = event->getIf<sf::Event::MouseButtonPressed>())
            {
                if (mouseEvent->button == sf::Mouse::Button::Left)
                {
                    // Check for double click
                    sf::Time currentTime = clickTimer.getElapsedTime();

                    if ((currentTime - lastClickTime) < doubleClickThreshold)
                    {
                        // check if in root
                        if (theSystem.isRoot()) {
                            // Check for folder click
                            std::cout << "(" << mouseEvent->position.x << ", " << mouseEvent->position.y << ")" << std::endl;
                            
                            int locX = (mouseEvent->position.x-70)/145;
                            int locY = (mouseEvent->position.y-230)/135;
                            
                            if ((locX >= 0 && locX < 6 && locY < 4 && locY >= 0) && (mouseEvent->position.x >= 70 && mouseEvent->position.y >= 230)) {
                                theSystem.executeClick(locX, locY);
                            }
                            //folderBox.setPosition(sf::Vector2f((70.f+140.f*col+5.f*col),(230.f+135.f*row))); folderBox size is 120,120
                        } else {
                            // See if clicked on folder or file
                            std::cout << "(" << mouseEvent->position.x << ", " << mouseEvent->position.y << ")" << std::endl;
                            int locX = (mouseEvent->position.x-60)/875; // No x movement
                            int locY = (mouseEvent->position.y-150)/39;
                            
                            // Do locX and locY
                            std::cout << "(" << locX << ", " << locY << ")" << std::endl;
                            
                            if ((locX == 0 && locY < 16 && locY >= 0) && (mouseEvent->position.x >= 60 && mouseEvent->position.y >= 150)) {
                                theSystem.executeClick(locX, locY);
                            }
                            //sf::Vector2f pos(510.f,(168.f+39.f*box));
                        }
                    } else {
                        lastClickTime = currentTime;
                    }
                }
            }
        }
        
        // update the system and display
        theSystem.update();
        theSystem.display(&window);
        

    // Title (and other boot visuals) are drawn internally by System during boot

        // end the current frame
        window.display();
    }
}
