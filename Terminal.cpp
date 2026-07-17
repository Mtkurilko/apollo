#include "Terminal.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include <csignal>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "AppPaths.h"

namespace fs = std::filesystem;

namespace {

// Preferred terminal typefaces, best first. A proportional font makes every
// columnar command (`ls -l`, `git status`, `ps`) misalign, so we look for a
// real monospace face before falling back to the bundled UI font.
const char* const kMonoCandidates[] = {
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Monaco.ttf",
    "/System/Library/Fonts/Supplemental/Courier New.ttf",
};

std::string stripCarriageReturn(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}

// Escape spaces for a remote shell, except on ~-rooted paths where the tilde
// must stay unquoted for the remote shell to expand it.
std::string escapeForShell(const std::string& str) {
    const bool isTildePath = !str.empty() && str[0] == '~';
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        if (c == ' ' && !isTildePath) result += '\\';
        result += c;
    }
    return result;
}

} // namespace

Terminal::Terminal() : filePage(1), remoteServer(nullptr) {
    if (!font.openFromFile(apollo::assetPath("GFSNeohellenic-Regular.ttf").string())) {
        std::cerr << "Apollo: could not load UI font from " << apollo::appRoot() << std::endl;
    }

    bool monoLoaded = false;
    for (const char* candidate : kMonoCandidates) {
        if (fs::exists(candidate) && monoFont.openFromFile(candidate)) {
            monoLoaded = true;
            break;
        }
    }
    if (!monoLoaded) monoFont = font; // last resort: proportional, but legible

    // Every cell in a monospace face has the same advance, so one measurement
    // gives us exact column math without laying out text every frame.
    glyphAdvance = monoFont.getGlyph(U'M', lineSize, false).advance;
    if (glyphAdvance <= 0.f) glyphAdvance = static_cast<float>(lineSize) * 0.6f;

    const fs::path configPath = apollo::configPath();
    if (fs::exists(configPath)) {
        remoteServer = std::make_unique<RemoteServer>(configPath.string());
    }
}

Terminal::~Terminal() {
    cancelCommand();
    if (worker.joinable()) worker.join();
}

// ---------------------------------------------------------------------------
// Scrollback
// ---------------------------------------------------------------------------

void Terminal::pushLine(std::string s) {
    scrollback.push_back(std::move(s));
    while (scrollback.size() > kMaxScrollback) scrollback.pop_front();
}

void Terminal::addHistory(std::string input) {
    if (input.empty()) return;
    pushLine(std::move(input));
}

void Terminal::appendOutputBlock(const std::string& text) {
    std::istringstream rs(text);
    std::string line;
    while (std::getline(rs, line)) pushLine("   " + stripCarriageReturn(line));
}

void Terminal::pump() {
    std::vector<std::string> batch;
    {
        std::lock_guard<std::mutex> lock(outMutex);
        if (pendingOutput.empty()) return;
        batch.swap(pendingOutput);
    }
    for (auto& line : batch) pushLine(std::move(line));
    if (scrollOffset > 0) scrollOffset += static_cast<int>(batch.size());
}

bool Terminal::consumeDirectoryChanged() {
    return dirChanged.exchange(false, std::memory_order_relaxed);
}

void Terminal::scrollToBottom() {
    scrollOffset = 0;
}

// ---------------------------------------------------------------------------
// Asynchronous command execution
// ---------------------------------------------------------------------------

void Terminal::reapWorker() {
    if (worker.joinable() && !commandRunning.load()) worker.join();
}

void Terminal::runAsync(const std::string& shellCommand) {
    reapWorker();
    if (worker.joinable()) return; // a command is still live
    commandRunning.store(true);

    worker = std::thread([this, shellCommand]() {
        int fds[2];
        if (pipe(fds) != 0) {
            std::lock_guard<std::mutex> lock(outMutex);
            pendingOutput.push_back("   ERROR: pipe() failed");
            commandRunning.store(false);
            return;
        }

        const pid_t pid = fork();
        if (pid < 0) {
            close(fds[0]);
            close(fds[1]);
            std::lock_guard<std::mutex> lock(outMutex);
            pendingOutput.push_back("   ERROR: fork() failed");
            commandRunning.store(false);
            return;
        }

        if (pid == 0) {
            // Child: own process group so Ctrl+C can signal the whole job.
            setpgid(0, 0);
            dup2(fds[1], STDOUT_FILENO);
            dup2(fds[1], STDERR_FILENO);
            close(fds[0]);
            close(fds[1]);
            execl("/bin/sh", "sh", "-c", shellCommand.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }

        close(fds[1]);
        setpgid(pid, pid); // racy with the child's own call; whichever wins is fine
        childPid.store(pid);

        // Stream output line by line so long-running commands appear as they go
        // instead of landing in one lump when the process exits.
        std::string buffer;
        char chunk[4096];
        ssize_t n;
        while ((n = read(fds[0], chunk, sizeof(chunk))) > 0) {
            buffer.append(chunk, static_cast<std::size_t>(n));
            std::size_t nl;
            std::vector<std::string> lines;
            while ((nl = buffer.find('\n')) != std::string::npos) {
                lines.push_back("   " + stripCarriageReturn(buffer.substr(0, nl)));
                buffer.erase(0, nl + 1);
            }
            if (!lines.empty()) {
                std::lock_guard<std::mutex> lock(outMutex);
                for (auto& l : lines) pendingOutput.push_back(std::move(l));
            }
        }
        if (!buffer.empty()) {
            std::lock_guard<std::mutex> lock(outMutex);
            pendingOutput.push_back("   " + stripCarriageReturn(buffer));
        }
        close(fds[0]);

        int status = 0;
        waitpid(pid, &status, 0);
        childPid.store(-1);
        dirChanged.store(true);
        commandRunning.store(false);
    });
}

void Terminal::runRemote(const std::string& shellCommand) {
    reapWorker();
    if (worker.joinable()) return;
    commandRunning.store(true);

    RemoteServer* remote = remoteServer.get();
    worker = std::thread([this, remote, shellCommand]() {
        const std::string result = remote->executeRemoteCommand(shellCommand);
        std::vector<std::string> lines;
        std::istringstream rs(result);
        std::string line;
        while (std::getline(rs, line)) lines.push_back("   " + stripCarriageReturn(line));
        {
            std::lock_guard<std::mutex> lock(outMutex);
            for (auto& l : lines) pendingOutput.push_back(std::move(l));
        }
        dirChanged.store(true);
        commandRunning.store(false);
    });
}

void Terminal::cancelCommand() {
    const int pid = childPid.load();
    if (pid > 0) killpg(static_cast<pid_t>(pid), SIGINT);
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

std::size_t Terminal::prevWordBoundary() const {
    std::size_t i = cursor;
    while (i > 0 && std::isspace(static_cast<unsigned char>(command[i - 1]))) --i;
    while (i > 0 && !std::isspace(static_cast<unsigned char>(command[i - 1]))) --i;
    return i;
}

std::size_t Terminal::nextWordBoundary() const {
    std::size_t i = cursor;
    while (i < command.size() && std::isspace(static_cast<unsigned char>(command[i]))) ++i;
    while (i < command.size() && !std::isspace(static_cast<unsigned char>(command[i]))) ++i;
    return i;
}

void Terminal::rememberCommand(const std::string& line) {
    if (line.empty()) return;
    if (!cmdHistory.empty() && cmdHistory.back() == line) return;
    cmdHistory.push_back(line);
    if (cmdHistory.size() > kMaxCommandHistory) cmdHistory.erase(cmdHistory.begin());
}

void Terminal::recallHistory(int direction) {
    if (cmdHistory.empty()) return;

    if (historyPos == -1) {
        if (direction > 0) return; // already on the fresh line
        stashedLine = command;
        historyPos = static_cast<int>(cmdHistory.size()) - 1;
    } else {
        const int next = historyPos + direction;
        if (next < 0) return;
        if (next >= static_cast<int>(cmdHistory.size())) {
            historyPos = -1;
            command = stashedLine;
            cursor = command.size();
            scrollToBottom();
            return;
        }
        historyPos = next;
    }

    command = cmdHistory[static_cast<std::size_t>(historyPos)];
    cursor = command.size();
    scrollToBottom();
}

bool Terminal::onKey(const sf::Event::KeyPressed& key) {
    using Key = sf::Keyboard::Key;
    cursorBlink.restart(); // keep the caret solid while typing

    if (key.control) {
        switch (key.code) {
            case Key::A: cursor = 0; return true;
            case Key::E: cursor = command.size(); return true;
            case Key::K: command.erase(cursor); return true;
            case Key::U: command.erase(0, cursor); cursor = 0; return true;
            case Key::W: {
                const std::size_t start = prevWordBoundary();
                command.erase(start, cursor - start);
                cursor = start;
                return true;
            }
            case Key::C:
                if (isBusy()) {
                    cancelCommand();
                    pushLine("^C");
                } else {
                    pushLine("APOLLO % " + command + "^C");
                    command.clear();
                    cursor = 0;
                }
                historyPos = -1;
                scrollToBottom();
                return true;
            case Key::L:
                scrollback.clear();
                scrollToBottom();
                return true;
            case Key::D:
                if (command.empty()) quitRequested = true;
                return true;
            default: break;
        }
    }

    switch (key.code) {
        case Key::Left:
            cursor = key.alt ? prevWordBoundary() : (cursor > 0 ? cursor - 1 : 0);
            return true;
        case Key::Right:
            cursor = key.alt ? nextWordBoundary()
                             : (cursor < command.size() ? cursor + 1 : command.size());
            return true;
        case Key::Home: cursor = 0; return true;
        case Key::End: cursor = command.size(); return true;
        case Key::Up: recallHistory(-1); return true;
        case Key::Down: recallHistory(+1); return true;
        case Key::PageUp: onScroll(+8.f); return true;
        case Key::PageDown: onScroll(-8.f); return true;
        case Key::Backspace:
            if (cursor > 0) {
                command.erase(cursor - 1, 1);
                --cursor;
            }
            scrollToBottom();
            return true;
        case Key::Delete:
            if (cursor < command.size()) command.erase(cursor, 1);
            return true;
        case Key::Tab:
            autocomplete(command);
            cursor = command.size();
            scrollToBottom();
            return true;
        case Key::Enter:
            submit();
            return true;
        default:
            return false;
    }
}

void Terminal::onText(char32_t unicode) {
    if (unicode < 32 || unicode > 126) return; // control chars come via onKey
    command.insert(cursor, 1, static_cast<char>(unicode));
    ++cursor;
    historyPos = -1;
    cursorBlink.restart();
    scrollToBottom();
}

void Terminal::onScroll(float delta) {
    // One notch moves three lines, matching the platform convention.
    scrollOffset += static_cast<int>(delta * 3.f);
    if (scrollOffset < 0) scrollOffset = 0;
    const int maxScroll = static_cast<int>(scrollback.size());
    if (scrollOffset > maxScroll) scrollOffset = maxScroll;
}

void Terminal::submit() {
    if (isBusy()) {
        pushLine("   (a command is still running — press Ctrl+C to cancel)");
        return;
    }
    rememberCommand(command);
    historyPos = -1;
    stashedLine.clear();
    scrollToBottom();
    executeCommand();
    command.clear();
    cursor = 0;
}

// ---------------------------------------------------------------------------
// Command execution
// ---------------------------------------------------------------------------

void Terminal::setCommand(std::string input) {
    command = std::move(input);
    cursor = command.size();
}

void Terminal::openTerminalApp() {
    if (remoteServer && remoteServer->isConnected()) {
        const std::string workingDir = remoteServer->getRemoteWorkingDir();
        const bool isTildePath = !workingDir.empty() && workingDir[0] == '~';
        const std::string escapedDir = isTildePath ? workingDir : escapeForShell(workingDir);

        const std::string tempScript = "/tmp/apollo_ssh_" + std::to_string(getpid()) + ".command";
        std::ofstream scriptFile(tempScript);
        scriptFile << "#!/bin/bash\n";
        scriptFile << "sshpass -p '" << remoteServer->getPassword() << "' ssh "
                   << "-o StrictHostKeyChecking=no -p " << remoteServer->getPort() << " "
                   << remoteServer->getUser() << "@" << remoteServer->getHost()
                   << " -t 'cd " << escapedDir << "; exec bash'\n";
        scriptFile.close();
        chmod(tempScript.c_str(), 0755);

        const std::string cmd = "open -a Terminal \"" + tempScript + "\"";
        system(cmd.c_str());
    } else {
        const std::string cmd = "open -a Terminal \"" + fs::current_path().string() + "\"";
        system(cmd.c_str());
    }
}

bool Terminal::runBuiltin(const std::string& name, const std::string& argument) {
    if (name != "apollo") return false;

    const std::string echo = "APOLLO % apollo" + (argument.empty() ? "" : " " + argument);

    if (argument.empty()) {
        // Bare `apollo` returns to the Apollo root, local or remote.
        pushLine(echo);
        setCommand(remoteServer && remoteServer->isConnected()
                       ? "cd ~/APOLLO"
                       : "cd " + apollo::appRoot().parent_path().string());
        executeCommand();
        return true;
    }

    if (argument == "exit") {
        pushLine(echo);
        if (remoteServer && remoteServer->isConnected()) {
            remoteServer->disconnect();
            pushLine("Disconnected from remote server.");
        }
        quitRequested = true;
        return true;
    }

    if (argument == "clear") {
        scrollback.clear();
        scrollToBottom();
        return true;
    }

    if (argument == "connect") {
        pushLine(echo);
        if (!remoteServer) {
            pushLine("ERROR: RemoteServer not initialized. Check apollo.properties exists.");
        } else if (remoteServer->connect()) {
            pushLine(remoteServer->getConnectionStatus());
            const std::string cdResult = remoteServer->executeRemoteCommand("cd ~/APOLLO 2>&1");
            if (cdResult.find("APOLLO") != std::string::npos) {
                pushLine("Remote directory: " + stripCarriageReturn(cdResult));
            } else {
                pushLine("Note: ~/APOLLO may not exist on remote server");
                appendOutputBlock(cdResult);
            }
            dirChanged.store(true);
        } else {
            pushLine(remoteServer->getConnectionStatus());
        }
        return true;
    }

    if (argument == "disconnect") {
        pushLine(echo);
        if (!remoteServer) {
            pushLine("ERROR: RemoteServer not initialized.");
        } else if (!remoteServer->isConnected()) {
            pushLine("ERROR: Not connected to any remote server.");
        } else {
            remoteServer->disconnect();
            pushLine("Disconnected from remote server.");
            dirChanged.store(true);
        }
        return true;
    }

    if (argument == "p++") {
        pushLine(echo);
        filePage++;
        return true;
    }

    if (argument == "p--") {
        pushLine(echo);
        if (filePage > 1) filePage--;
        else pushLine("ERROR: No pages to go back");
        return true;
    }

    if (argument == "term" || argument == "termk") {
        pushLine(echo);
        openTerminalApp();
        if (argument == "termk") quitRequested = true;
        return true;
    }

    if (argument == "save") {
        pushLine(echo);
        if (!remoteServer || !remoteServer->isConnected()) {
            pushLine("ERROR: Not connected to remote server. apollo save only works when connected.");
            return true;
        }

        const std::string tempDir = "/tmp/apollo_files";
        if (!fs::exists(tempDir)) {
            pushLine("No modified files to upload.");
            return true;
        }

        const std::string password = remoteServer->getPassword();
        const std::string workingDir = remoteServer->getRemoteWorkingDir();
        std::string filesUploaded;
        int uploadCount = 0;

        try {
            for (const auto& entry : fs::directory_iterator(tempDir)) {
                if (!fs::is_regular_file(entry)) continue;
                const std::string filename = entry.path().filename().string();
                const std::string localFile = tempDir + "/" + filename;
                const std::string remoteFile = workingDir + "/" + filename;

                const std::string scpCmd =
                    "sshpass -p '" + password + "' scp -P " + remoteServer->getPort() +
                    " -o StrictHostKeyChecking=no \"" + localFile + "\" '" +
                    remoteServer->getUser() + "@" + remoteServer->getHost() + ":" + remoteFile + "'";

                if (system(scpCmd.c_str()) == 0) {
                    if (uploadCount++) filesUploaded += ", ";
                    filesUploaded += filename;
                    std::error_code ec;
                    fs::remove(localFile, ec);
                    if (ec) pushLine("Warning: Could not delete " + filename + " from cache");
                } else {
                    pushLine("ERROR: Failed to upload " + filename);
                }
            }
        } catch (const std::exception& e) {
            pushLine("ERROR: " + std::string(e.what()));
            return true;
        }

        if (uploadCount > 0) {
            pushLine("Uploaded " + std::to_string(uploadCount) + " file(s): " + filesUploaded);
        } else {
            pushLine("No files to upload.");
        }
        return true;
    }

    pushLine(echo);
    pushLine("Unknown apollo command: " + argument);
    return true;
}

void Terminal::executeCommand() {
    const std::vector<std::string> parts = tokenize(command);
    if (parts.empty()) return;

    const std::string& name = parts[0];
    std::string argument;
    for (std::size_t i = 1; i < parts.size(); ++i) {
        if (i > 1) argument.push_back(' ');
        argument += parts[i];
    }

    if (runBuiltin(name, argument)) return;

    // `cd` stays synchronous: the GUI reads the working directory every frame,
    // so it has to be correct before the next draw.
    if (name == "cd") {
        pushLine("APOLLO % " + command);
        if (argument.empty()) {
            pushLine("Usage: cd <directory>");
            return;
        }
        if (remoteServer && remoteServer->isConnected()) {
            appendOutputBlock(remoteServer->executeRemoteCommand(command));
        } else {
            fs::path target = argument;
            if (!argument.empty() && argument[0] == '~') {
                if (const char* home = std::getenv("HOME")) {
                    target = fs::path(home) / argument.substr(argument.size() > 1 ? 2 : 1);
                }
            }
            if (chdir(target.c_str()) == 0) {
                pushLine("Directory changed: " + target.string());
            } else {
                pushLine("cd failed: " + target.string());
            }
        }
        dirChanged.store(true);
        return;
    }

    if (name == "open") {
        pushLine("APOLLO % " + command);
        if (argument.empty()) {
            pushLine("Usage: open <filename>");
            return;
        }

        if (remoteServer && remoteServer->isConnected()) {
            const std::string tempDir = "/tmp/apollo_files";
            std::error_code ec;
            fs::create_directories(tempDir, ec);
            const std::string localFile = tempDir + "/" + argument;

            if (fs::exists(localFile)) {
                system(("open \"" + localFile + "\"").c_str());
                pushLine("File opened from cache: " + argument);
                return;
            }

            const std::string remoteFile = remoteServer->getRemoteWorkingDir() + "/" + argument;
            const std::string scpCmd =
                "sshpass -p '" + remoteServer->getPassword() + "' scp -P " +
                remoteServer->getPort() + " -o StrictHostKeyChecking=no '" +
                remoteServer->getUser() + "@" + remoteServer->getHost() + ":" + remoteFile +
                "' \"" + localFile + "\"";

            if (system(scpCmd.c_str()) == 0) {
                system(("open \"" + localFile + "\"").c_str());
                pushLine("File opened: " + argument);
                pushLine("Note: Use 'apollo save' to upload changes when done editing.");
            } else {
                pushLine("ERROR: Failed to download file via SCP");
            }
        } else {
            system(("open \"" + argument + "\"").c_str());
            pushLine("File opened: " + argument);
        }
        return;
    }

    // Everything else is a pass-through shell command; run it off the render
    // thread so the window stays interactive and output streams in.
    pushLine("APOLLO % " + command);
    if (remoteServer && remoteServer->isConnected()) {
        runRemote(command);
    } else {
        std::string execCmd = command;
        if (execCmd.find("2>&1") == std::string::npos) execCmd += " 2>&1";
        runAsync(execCmd);
    }
}

std::vector<std::string> Terminal::tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuotes = false;
    char quoteChar = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '\\' && i + 1 < line.size()) {
            cur.push_back(line[++i]);
            continue;
        }
        if (c == '"' || c == '\'') {
            if (!inQuotes) {
                inQuotes = true;
                quoteChar = c;
                continue;
            }
            if (c == quoteChar) {
                inQuotes = false;
                quoteChar = 0;
                continue;
            }
        }
        if (std::isspace(static_cast<unsigned char>(c)) && !inQuotes) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// ---------------------------------------------------------------------------
// Autocomplete
// ---------------------------------------------------------------------------

void Terminal::autocomplete(std::string& line) {
    auto parts = tokenize(line);
    const bool trailingSpace = !line.empty() && std::isspace(static_cast<unsigned char>(line.back()));
    if (trailingSpace) parts.push_back("");
    if (parts.size() < 2) return; // command-name completion not supported yet

    const std::string prefix = parts.back();

    fs::path parent;
    std::string namePrefix = prefix;
    const fs::path p(prefix);
    if (p.has_parent_path()) {
        parent = p.parent_path();
        namePrefix = p.filename().string();
    }
    const fs::path base = fs::current_path();
    const fs::path dir = parent.empty() ? base : (parent.is_absolute() ? parent : base / parent);

    std::vector<std::string> matches;
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::end(it); it.increment(ec)) {
        std::string fname = it->path().filename().string();
        if (fname.rfind(namePrefix, 0) != 0) continue;
        if (it->is_directory()) fname += "/";
        matches.push_back(std::move(fname));
    }
    if (matches.empty()) return;
    std::sort(matches.begin(), matches.end());

    // Extend to the longest common prefix, the way a real shell does, instead
    // of only completing when exactly one candidate matches.
    std::string common = matches.front();
    for (const auto& m : matches) {
        std::size_t i = 0;
        while (i < common.size() && i < m.size() && common[i] == m[i]) ++i;
        common.resize(i);
    }

    if (common.size() > namePrefix.size() || matches.size() == 1) {
        parts.back() = parent.empty() ? common : (parent / common).string();
        std::string rebuilt;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i) rebuilt.push_back(' ');
            const std::string& seg = parts[i];
            if (seg.find(' ') != std::string::npos) rebuilt += '"' + seg + '"';
            else rebuilt += seg;
        }
        line = rebuilt;
        if (matches.size() == 1) return;
    }

    pushLine("APOLLO % " + line);
    for (const auto& m : matches) pushLine("   " + m);
}

// ---------------------------------------------------------------------------
// Remote helpers
// ---------------------------------------------------------------------------

bool Terminal::isRemoteConnected() const {
    return remoteServer && remoteServer->isConnected();
}

RemoteServer* Terminal::getRemoteServer() const {
    return remoteServer.get();
}

std::vector<std::pair<std::string, bool>> Terminal::getRemoteDirectoryListing() {
    std::vector<std::pair<std::string, bool>> result;
    if (!isRemoteConnected()) return result;

    std::istringstream stream(remoteServer->executeRemoteCommand("ls -1F"));
    std::string line;
    while (std::getline(stream, line)) {
        line = stripCarriageReturn(line);
        if (line.empty()) continue;

        bool isDirectory = false;
        const char tail = line.back();
        if (tail == '/') {
            isDirectory = true;
            line.pop_back();
        } else if (tail == '*' || tail == '@' || tail == '=' || tail == '|') {
            line.pop_back();
        }
        if (!line.empty()) result.emplace_back(line, isDirectory);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

std::size_t Terminal::columns() const {
    const float usable = boxSize.x - 2.f * padX;
    const auto cols = static_cast<std::size_t>(usable / glyphAdvance);
    return cols < 8 ? 8 : cols;
}

std::vector<std::string> Terminal::wrap(const std::string& s, std::size_t cols) const {
    std::vector<std::string> out;
    if (s.empty()) {
        out.emplace_back();
        return out;
    }
    for (std::size_t i = 0; i < s.size(); i += cols) {
        out.push_back(s.substr(i, cols));
    }
    return out;
}

void Terminal::draw(sf::RenderWindow* window) {
    sf::RectangleShape terminalView(boxSize);
    terminalView.setFillColor(sf::Color(18, 18, 18));
    terminalView.setOutlineThickness(2.f);
    terminalView.setOutlineColor(sf::Color(36, 36, 36));
    terminalView.setPosition(boxPos);
    window->draw(terminalView);

    const float perLine = static_cast<float>(lineSize) + lineSpacing;
    const float usableHeight = boxSize.y - 2.f * padY - perLine - 8.f; // reserve the prompt row
    const int maxLines = static_cast<int>(usableHeight / perLine);
    if (maxLines < 1) return;

    const std::size_t cols = columns();

    // Wrap backwards from the newest line until the viewport is full. Only the
    // visible tail is ever laid out, so scrollback depth costs nothing to draw.
    std::vector<std::string> view;
    view.reserve(static_cast<std::size_t>(maxLines));
    int toSkip = scrollOffset;
    for (auto it = scrollback.rbegin(); it != scrollback.rend(); ++it) {
        std::vector<std::string> wrapped = wrap(*it, cols);
        for (auto w = wrapped.rbegin(); w != wrapped.rend(); ++w) {
            if (toSkip > 0) {
                --toSkip;
                continue;
            }
            view.push_back(*w);
            if (view.size() >= static_cast<std::size_t>(maxLines)) break;
        }
        if (view.size() >= static_cast<std::size_t>(maxLines)) break;
    }

    sf::Text lineTxt(monoFont, "", lineSize);
    lineTxt.setFillColor(sf::Color(210, 210, 210));
    float y = boxPos.y + padY;
    for (auto it = view.rbegin(); it != view.rend(); ++it) {
        lineTxt.setString(*it);
        lineTxt.setPosition({boxPos.x + padX, std::round(y)});
        window->draw(lineTxt);
        y += perLine;
    }

    // --- prompt row -------------------------------------------------------
    const std::string prompt = isBusy() ? "APOLLO ⋯ " : "APOLLO % ";
    const float promptY = boxPos.y + boxSize.y - padY - perLine;

    // Horizontal scroll keeps the caret on screen on very long lines.
    const std::size_t inputCols = cols > prompt.size() ? cols - prompt.size() : 1;
    std::size_t viewStart = 0;
    if (cursor >= inputCols) viewStart = cursor - inputCols + 1;
    const std::string visible = command.substr(viewStart, inputCols);

    sf::Text promptTxt(monoFont, prompt + visible, lineSize);
    promptTxt.setFillColor(sf::Color(235, 235, 180));
    promptTxt.setPosition({boxPos.x + padX, std::round(promptY)});
    window->draw(promptTxt);

    if (!isBusy() && cursorBlink.getElapsedTime().asSeconds() < 0.5f) {
        sf::RectangleShape caret({2.f, static_cast<float>(lineSize)});
        caret.setFillColor(sf::Color(235, 235, 180));
        const float caretX =
            boxPos.x + padX + (static_cast<float>(prompt.size() + (cursor - viewStart)) * glyphAdvance);
        caret.setPosition({std::round(caretX), std::round(promptY + 3.f)});
        window->draw(caret);
    } else if (cursorBlink.getElapsedTime().asSeconds() > 1.f) {
        cursorBlink.restart();
    }

    // Scroll indicator so it is obvious you are not looking at the live tail.
    if (scrollOffset > 0) {
        sf::Text marker(monoFont, "-- scrolled " + std::to_string(scrollOffset) + " lines --", 12);
        marker.setFillColor(sf::Color(120, 120, 120));
        marker.setPosition({boxPos.x + padX, std::round(promptY - perLine)});
        window->draw(marker);
    }
}
