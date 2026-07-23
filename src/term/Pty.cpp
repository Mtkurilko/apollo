#include "term/Pty.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

#include <algorithm>
#include <cstring>

extern char** environ;

namespace apollo {
namespace {

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

} // namespace

Pty::~Pty() {
    terminate();
    close();
}

bool Pty::start(const Launch& launch, int rows, int cols, std::string* error) {
    if (launch.argv.empty()) {
        if (error) *error = "nothing to run";
        return false;
    }

    rows_ = std::max(1, rows);
    cols_ = std::max(1, cols);

    winsize size{};
    size.ws_row = static_cast<unsigned short>(rows_);
    size.ws_col = static_cast<unsigned short>(cols_);

    // Sensible line discipline: the child is free to change any of it, and
    // most shells immediately do.
    termios settings{};
    settings.c_iflag = ICRNL | IXON | IUTF8 | BRKINT;
    settings.c_oflag = OPOST | ONLCR;
    settings.c_cflag = CS8 | CREAD | HUPCL;
    settings.c_lflag = ISIG | ICANON | IEXTEN | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE;
    settings.c_cc[VEOF] = 4;     settings.c_cc[VEOL] = 0xFF;
    settings.c_cc[VERASE] = 0x7F; settings.c_cc[VINTR] = 3;
    settings.c_cc[VKILL] = 21;   settings.c_cc[VMIN] = 1;
    settings.c_cc[VQUIT] = 28;   settings.c_cc[VSTART] = 17;
    settings.c_cc[VSTOP] = 19;   settings.c_cc[VSUSP] = 26;
    settings.c_cc[VTIME] = 0;    settings.c_cc[VWERASE] = 23;
    cfsetispeed(&settings, B38400);
    cfsetospeed(&settings, B38400);

    int master = -1;
    const pid_t child = ::forkpty(&master, nullptr, &settings, &size);
    if (child < 0) {
        if (error) *error = std::string("cannot open a pty: ") + std::strerror(errno);
        return false;
    }

    if (child == 0) {
        // Child. Only async-signal-safe work between here and exec.
        if (!launch.cwd.empty()) {
            if (::chdir(launch.cwd.c_str()) != 0) { /* start in $HOME instead */ }
        }
        for (int sig : {SIGINT, SIGQUIT, SIGTERM, SIGHUP, SIGPIPE, SIGCHLD, SIGTSTP}) {
            ::signal(sig, SIG_DFL);
        }

        const std::vector<std::string> env = mergedEnv(launch.env);
        std::vector<char*> envp;
        envp.reserve(env.size() + 1);
        for (const auto& entry : env) envp.push_back(const_cast<char*>(entry.c_str()));
        envp.push_back(nullptr);

        std::vector<char*> argv;
        argv.reserve(launch.argv.size() + 1);
        for (const auto& arg : launch.argv) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);

        // macOS has no execvpe; replacing environ before execvp is the
        // portable equivalent and is safe in a just-forked child.
        environ = envp.data();
        ::execvp(argv[0], argv.data());
        ::_exit(127);
    }

    pid_ = child;
    fd_ = master;
    exited_ = false;
    exitCode_ = 0;

    // Non-blocking: the UI thread drains whatever has arrived each frame and
    // never waits on the child.
    const int flags = ::fcntl(fd_, F_GETFL, 0);
    ::fcntl(fd_, F_SETFL, (flags < 0 ? 0 : flags) | O_NONBLOCK);
    ::fcntl(fd_, F_SETFD, FD_CLOEXEC);
    return true;
}

void Pty::resize(int rows, int cols) {
    rows_ = std::max(1, rows);
    cols_ = std::max(1, cols);
    if (fd_ < 0) return;

    winsize size{};
    size.ws_row = static_cast<unsigned short>(rows_);
    size.ws_col = static_cast<unsigned short>(cols_);
    ::ioctl(fd_, TIOCSWINSZ, &size);
    // SIGWINCH goes to the foreground process group on its own; full screen
    // programs redraw themselves from here.
}

std::ptrdiff_t Pty::read(char* buffer, std::size_t size) {
    if (fd_ < 0) return -1;

    const ssize_t got = ::read(fd_, buffer, size);
    if (got > 0) return got;
    if (got == 0) return -1; // the child closed its side
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    return -1;
}

bool Pty::write(std::string_view bytes) {
    if (fd_ < 0 || bytes.empty()) return false;

    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const ssize_t wrote = ::write(fd_, bytes.data() + sent, bytes.size() - sent);
        if (wrote > 0) { sent += static_cast<std::size_t>(wrote); continue; }
        if (wrote < 0 && (errno == EINTR)) continue;
        if (wrote < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // The child is not reading. Wait briefly rather than spinning; a
            // paste larger than the pty buffer lands here.
            ::usleep(500);
            continue;
        }
        return false;
    }
    return true;
}

bool Pty::poll() {
    if (pid_ <= 0 || exited_) return exited_;

    int status = 0;
    const pid_t done = ::waitpid(pid_, &status, WNOHANG);
    if (done != pid_) return false;

    exited_ = true;
    if (WIFEXITED(status)) exitCode_ = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) exitCode_ = 128 + WTERMSIG(status);
    return true;
}

void Pty::signal(int number) {
    if (pid_ > 0 && !exited_) ::kill(-pid_, number);
}

void Pty::terminate() {
    if (pid_ <= 0 || exited_) return;

    ::kill(-pid_, SIGHUP);
    for (int attempt = 0; attempt < 40; ++attempt) { // up to ~200ms
        if (poll()) return;
        ::usleep(5000);
    }
    ::kill(-pid_, SIGKILL);
    poll();
}

void Pty::close() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

} // namespace apollo
