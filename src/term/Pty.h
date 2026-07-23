// A pseudo-terminal running a child process.
//
// This is what makes Apollo's terminal a terminal rather than a command runner:
// the child gets a real tty, so it line-edits, paints full screen, reports its
// window size and handles signals exactly as it would in any other terminal.
#pragma once

#include <sys/types.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace apollo {

class Pty {
public:
    struct Launch {
        std::vector<std::string> argv;
        std::vector<std::string> env; // "NAME=value", laid over the inherited set
        std::string cwd;
    };

    Pty() = default;
    ~Pty();
    Pty(const Pty&) = delete;
    Pty& operator=(const Pty&) = delete;

    bool start(const Launch& launch, int rows, int cols, std::string* error = nullptr);
    void resize(int rows, int cols);

    // Reads whatever is available without blocking. Returns 0 when there is
    // nothing to read yet, and -1 once the far end has closed.
    std::ptrdiff_t read(char* buffer, std::size_t size);
    bool write(std::string_view bytes);

    int fd() const { return fd_; }
    pid_t pid() const { return pid_; }
    bool running() const { return pid_ > 0 && !exited_; }
    // Reaps the child if it has finished. Cheap enough to call every frame.
    bool poll();
    int exitCode() const { return exitCode_; }

    // SIGHUP, then SIGKILL if it is still there.
    void terminate();
    void signal(int number);

private:
    void close();

    int fd_ = -1;
    pid_t pid_ = -1;
    bool exited_ = false;
    int exitCode_ = 0;
    int rows_ = 24, cols_ = 80;
};

} // namespace apollo
