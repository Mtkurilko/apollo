// A pty running a child process.
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

    // Reads whatever is there. Never blocks.
    std::ptrdiff_t read(char* buffer, std::size_t size);
    bool write(std::string_view bytes);

    int fd() const { return fd_; }
    pid_t pid() const { return pid_; }
    bool running() const { return pid_ > 0 && !exited_; }
    // True when something other than the shell holds the terminal. That's a
    // running command, whether or not the shell bothered to tell us.
    bool foregroundBusy() const;
    bool poll();
    int exitCode() const { return exitCode_; }

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
