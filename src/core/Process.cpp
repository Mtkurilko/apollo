#include "core/Process.h"

#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>

extern char** environ;

namespace fs = std::filesystem;

namespace apollo::process {
namespace {

// A pipe that closes itself, so no early return can leak a descriptor.
struct Pipe {
    int fd[2] = {-1, -1};

    bool open() { return ::pipe(fd) == 0; }
    void closeRead()  { if (fd[0] >= 0) { ::close(fd[0]); fd[0] = -1; } }
    void closeWrite() { if (fd[1] >= 0) { ::close(fd[1]); fd[1] = -1; } }
    ~Pipe() { closeRead(); closeWrite(); }
};

// environ plus the caller's overlay, with overlay entries replacing any
// inherited variable of the same name.
std::vector<std::string> mergedEnv(const std::vector<std::string>& extra) {
    std::vector<std::string> merged;
    for (char** e = environ; e && *e; ++e) {
        const std::string entry = *e;
        const std::string name = entry.substr(0, entry.find('='));
        const bool overridden = std::any_of(
            extra.begin(), extra.end(),
            [&](const std::string& o) { return o.rfind(name + "=", 0) == 0; });
        if (!overridden) merged.push_back(entry);
    }
    merged.insert(merged.end(), extra.begin(), extra.end());
    return merged;
}

Result spawnAndCollect(const std::vector<std::string>& argv,
                       std::chrono::milliseconds timeout,
                       const std::string& workingDir,
                       const std::vector<std::string>& extraEnv) {
    Result result;
    if (argv.empty()) {
        result.err = "no command given";
        return result;
    }

    Pipe outPipe, errPipe;
    if (!outPipe.open() || !errPipe.open()) {
        result.err = "cannot create pipes: " + std::string(std::strerror(errno));
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, outPipe.fd[0]);
    posix_spawn_file_actions_addclose(&actions, errPipe.fd[0]);
    posix_spawn_file_actions_adddup2(&actions, outPipe.fd[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, errPipe.fd[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, outPipe.fd[1]);
    posix_spawn_file_actions_addclose(&actions, errPipe.fd[1]);
    if (!workingDir.empty()) {
        posix_spawn_file_actions_addchdir_np(&actions, workingDir.c_str());
    }

    std::vector<char*> raw;
    raw.reserve(argv.size() + 1);
    for (const auto& arg : argv) raw.push_back(const_cast<char*>(arg.c_str()));
    raw.push_back(nullptr);

    std::vector<std::string> envStorage;
    std::vector<char*> envp;
    if (!extraEnv.empty()) {
        envStorage = mergedEnv(extraEnv);
        envp.reserve(envStorage.size() + 1);
        for (auto& entry : envStorage) envp.push_back(entry.data());
        envp.push_back(nullptr);
    }

    pid_t pid = -1;
    const int status = posix_spawnp(&pid, argv[0].c_str(), &actions, nullptr,
                                    raw.data(), envp.empty() ? environ : envp.data());
    posix_spawn_file_actions_destroy(&actions);

    if (status != 0) {
        result.err = argv[0] + ": " + std::strerror(status);
        return result;
    }

    outPipe.closeWrite();
    errPipe.closeWrite();

    // Read both streams together; draining them one at a time deadlocks as
    // soon as the child fills the other pipe's buffer.
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    pollfd fds[2] = {{outPipe.fd[0], POLLIN, 0}, {errPipe.fd[0], POLLIN, 0}};
    std::string* sink[2] = {&result.out, &result.err};
    int open = 2;

    while (open > 0) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            result.timedOut = true;
            ::kill(pid, SIGKILL);
            break;
        }

        const int ready = ::poll(fds, 2, static_cast<int>(left.count()));
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready == 0) continue;

        for (int i = 0; i < 2; ++i) {
            if (fds[i].fd < 0 || !(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;

            char buffer[4096];
            const ssize_t got = ::read(fds[i].fd, buffer, sizeof(buffer));
            if (got > 0) {
                sink[i]->append(buffer, static_cast<std::size_t>(got));
            } else if (got == 0 || (got < 0 && errno != EINTR && errno != EAGAIN)) {
                fds[i].fd = -1;
                --open;
            }
        }
    }

    int wstatus = 0;
    while (::waitpid(pid, &wstatus, 0) < 0 && errno == EINTR) {}
    if (WIFEXITED(wstatus)) result.exitCode = WEXITSTATUS(wstatus);
    else if (WIFSIGNALED(wstatus)) result.exitCode = 128 + WTERMSIG(wstatus);

    return result;
}

} // namespace

Result run(const std::vector<std::string>& argv,
           std::chrono::milliseconds timeout,
           const std::string& workingDir,
           const Env& extraEnv) {
    return spawnAndCollect(argv, timeout, workingDir, extraEnv);
}

Result shell(const std::string& command,
             std::chrono::milliseconds timeout,
             const std::string& workingDir) {
    return spawnAndCollect({"/bin/sh", "-c", command}, timeout, workingDir, {});
}

Result feed(const std::vector<std::string>& argv, const std::string& input,
            std::chrono::milliseconds timeout) {
    Result result;
    if (argv.empty()) { result.err = "no command given"; return result; }

    Pipe in;
    if (!in.open()) {
        result.err = "cannot create a pipe: " + std::string(std::strerror(errno));
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in.fd[0], STDIN_FILENO);
    posix_spawn_file_actions_addclose(&actions, in.fd[0]);
    posix_spawn_file_actions_addclose(&actions, in.fd[1]);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);

    std::vector<char*> raw;
    raw.reserve(argv.size() + 1);
    for (const auto& arg : argv) raw.push_back(const_cast<char*>(arg.c_str()));
    raw.push_back(nullptr);

    pid_t pid = -1;
    const int status = posix_spawnp(&pid, argv[0].c_str(), &actions, nullptr, raw.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (status != 0) {
        result.err = argv[0] + ": " + std::strerror(status);
        return result;
    }

    in.closeRead();
    std::size_t sent = 0;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (sent < input.size() && std::chrono::steady_clock::now() < deadline) {
        const ssize_t wrote = ::write(in.fd[1], input.data() + sent, input.size() - sent);
        if (wrote > 0) { sent += static_cast<std::size_t>(wrote); continue; }
        if (wrote < 0 && errno == EINTR) continue;
        break;
    }
    in.closeWrite();

    int wstatus = 0;
    while (::waitpid(pid, &wstatus, 0) < 0 && errno == EINTR) {}
    if (WIFEXITED(wstatus)) result.exitCode = WEXITSTATUS(wstatus);
    return result;
}

std::optional<std::string> which(const std::string& name) {
    if (name.empty()) return std::nullopt;
    if (name.find('/') != std::string::npos) {
        return ::access(name.c_str(), X_OK) == 0 ? std::optional(name) : std::nullopt;
    }

    const char* path = std::getenv("PATH");
    if (!path) path = "/usr/bin:/bin:/usr/sbin:/sbin";

    std::string current;
    const std::string haystack = std::string(path) + ":";
    for (const char c : haystack) {
        if (c != ':') { current.push_back(c); continue; }
        if (!current.empty()) {
            const std::string candidate = current + "/" + name;
            if (::access(candidate.c_str(), X_OK) == 0) return candidate;
        }
        current.clear();
    }
    return std::nullopt;
}

bool detach(const std::vector<std::string>& argv, const std::string& workingDir) {
    if (argv.empty()) return false;

    std::vector<char*> raw;
    raw.reserve(argv.size() + 1);
    for (const auto& arg : argv) raw.push_back(const_cast<char*>(arg.c_str()));
    raw.push_back(nullptr);

    // Double-fork: the middle process exits immediately and the grandchild is
    // reparented to init, so it never becomes a zombie. Doing this rather than
    // ignoring SIGCHLD matters, because the PTY sessions and process::run()
    // both rely on waitpid() still working.
    const pid_t middle = ::fork();
    if (middle < 0) return false;

    if (middle == 0) {
        ::setsid();
        if (const pid_t grandchild = ::fork(); grandchild != 0) ::_exit(grandchild < 0 ? 1 : 0);

        if (!workingDir.empty() && ::chdir(workingDir.c_str()) != 0) ::_exit(127);

        // Nothing detached should scribble on Apollo's own terminal.
        const int null = ::open("/dev/null", O_RDWR);
        if (null >= 0) {
            ::dup2(null, STDIN_FILENO);
            ::dup2(null, STDOUT_FILENO);
            ::dup2(null, STDERR_FILENO);
            if (null > STDERR_FILENO) ::close(null);
        }
        ::execvp(raw[0], raw.data());
        ::_exit(127);
    }

    int status = 0;
    while (::waitpid(middle, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

std::string userShell() {
    if (const char* shell = std::getenv("SHELL"); shell && *shell) return shell;
    if (const passwd* pw = getpwuid(getuid()); pw && pw->pw_shell && *pw->pw_shell) {
        return pw->pw_shell;
    }
    return "/bin/sh";
}

} // namespace apollo::process
