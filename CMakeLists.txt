cmake_minimum_required(VERSION 3.20)

project(apollo
    VERSION 0.3.0
    DESCRIPTION "A terminal-native workspace: file browser, real terminal, one config"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# FTXUI, pinned and built statically into the binary.
#
# Vendoring it this way means `apollo` has no runtime dependency to install and
# no package version to drift: the only thing a machine needs is a compiler.
# ---------------------------------------------------------------------------
include(FetchContent)

set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG        v7.0.3
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(ftxui)

# ---------------------------------------------------------------------------
# Core library. Everything except main() lives here so the tests can link it.
# ---------------------------------------------------------------------------
add_library(apollo_core STATIC
    src/core/Paths.cpp
    src/core/ConfigFile.cpp
    src/core/Config.cpp
    src/core/Control.cpp
    src/core/Layout.cpp
    src/core/Theme.cpp
    src/core/Keys.cpp
    src/core/Commands.cpp
    src/core/Process.cpp
    src/net/Ssh.cpp
    src/net/RemoteFs.cpp
    src/term/Pty.cpp
    src/term/Screen.cpp
    src/term/VtParser.cpp
    src/term/Session.cpp
)

target_include_directories(apollo_core PUBLIC src)
target_compile_definitions(apollo_core PUBLIC
    APOLLO_VERSION="${PROJECT_VERSION}")
target_compile_options(apollo_core PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter)

# ---------------------------------------------------------------------------
# The apollo binary.
# ---------------------------------------------------------------------------
add_executable(apollo
    src/main.cpp
    src/cli/Cli.cpp
    src/cli/Doctor.cpp
    src/ui/App.cpp
    src/ui/Boot.cpp
    src/ui/TerminalView.cpp
    src/ui/BrowserView.cpp
    src/ui/Palette.cpp
    src/ui/ConfigView.cpp
    src/ui/Onboard.cpp
    src/ui/Widgets.cpp
)

# ftxui::component already carries dom and screen; naming all three makes the
# linker warn about duplicates.
target_link_libraries(apollo PRIVATE apollo_core ftxui::component)

target_compile_options(apollo PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter)

# ---------------------------------------------------------------------------
# Tests: a single self-contained binary, no framework to install.
# ---------------------------------------------------------------------------
enable_testing()
# The tests link the dom half of FTXUI so the pieces that draw can be rendered
# to an off-screen buffer and checked, rather than only being looked at.
add_executable(apollo_tests
    tests/tests.cpp
    src/ui/Boot.cpp
    src/ui/BrowserView.cpp
    src/ui/Widgets.cpp)
target_link_libraries(apollo_tests PRIVATE apollo_core ftxui::dom)
target_compile_options(apollo_tests PRIVATE -Wall -Wextra)
add_test(NAME apollo_tests COMMAND apollo_tests)

# ---------------------------------------------------------------------------
# Install: one binary, plus the example config used as a starting point.
# ---------------------------------------------------------------------------
# One binary, and nothing else: the starter config is written by `apollo`
# itself on first run, so there is no data directory to find at runtime and
# no way for the two to drift apart.
include(GNUInstallDirs)
install(TARGETS apollo RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
