// Running other programs. argv arrays, never shell strings.
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace apollo::process {

struct Result {
    int exitCode = -1;
    std::string out;
    std::string err;
    bool timedOut = false;

    bool ok() const { return exitCode == 0 && !timedOut; }
};

// Extra "NAME=value" entries on top of the inherited environment.
using Env = std::vector<std::string>;

// Runs argv[0] off PATH, captures both streams, waits.
Result run(const std::vector<std::string>& argv,
           std::chrono::milliseconds timeout = std::chrono::seconds(20),
           const std::string& workingDir = "",
           const Env& extraEnv = {});

// Runs argv on this terminal -- its prompts and progress meters and all --
// and waits. Returns the exit code, 127 if it couldn't start.
int runAttached(const std::vector<std::string>& argv, const Env& extraEnv = {});

Result shell(const std::string& command,
             std::chrono::milliseconds timeout = std::chrono::seconds(20),
             const std::string& workingDir = "");

Result feed(const std::vector<std::string>& argv,
            const std::string& input,
            std::chrono::milliseconds timeout = std::chrono::seconds(5));

std::optional<std::string> which(const std::string& name);

bool detach(const std::vector<std::string>& argv, const std::string& workingDir = "");

std::string userShell();

// The desktop clipboard, whichever one is installed.
bool clipboardWrite(const std::string& text);
std::optional<std::string> clipboardRead();

bool openWithDesktop(const std::string& path);

std::string shellQuote(const std::string& text);

} // namespace apollo::process
