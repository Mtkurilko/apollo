// Running other programs, without going through a shell unless asked.
//
// Everything here takes an argv vector rather than a command string, so a
// directory called `; rm -rf ~` is just an awkward directory name.
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

// Extra "NAME=value" entries laid over the inherited environment. Passing a
// secret this way keeps it out of argv, where `ps` would show it to everyone.
using Env = std::vector<std::string>;

// Runs argv[0] found on PATH, captures both streams, and waits.
Result run(const std::vector<std::string>& argv,
           std::chrono::milliseconds timeout = std::chrono::seconds(20),
           const std::string& workingDir = "",
           const Env& extraEnv = {});

// Runs a command line through /bin/sh. Only for text the user typed themselves
// or wrote in their own config.
Result shell(const std::string& command,
             std::chrono::milliseconds timeout = std::chrono::seconds(20),
             const std::string& workingDir = "");

// Runs a program and writes `input` to its standard input. Used for pbcopy and
// anything else that takes its payload on a stream rather than in argv.
Result feed(const std::vector<std::string>& argv,
            const std::string& input,
            std::chrono::milliseconds timeout = std::chrono::seconds(5));

// Full path of an executable on PATH, or nullopt.
std::optional<std::string> which(const std::string& name);

// Starts a program and does not wait for it. Used for `open`, editors, and
// anything else that takes over from here.
bool detach(const std::vector<std::string>& argv, const std::string& workingDir = "");

// The user's login shell, from $SHELL then the passwd entry, then /bin/sh.
std::string userShell();

// The desktop's clipboard, whichever one is installed. Returns false when
// there is nothing on the machine that can do it.
bool clipboardWrite(const std::string& text);
std::optional<std::string> clipboardRead();

// Hands a file to the desktop to open however it sees fit.
bool openWithDesktop(const std::string& path);

} // namespace apollo::process
