#include "core/Control.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <signal.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace apollo {
namespace {

constexpr std::size_t kMaxMessage = 4096;

// AF_UNIX paths cap near 104 bytes on macOS and fail silently, so keep it short.
std::string socketPathFor(pid_t pid) {
    return "/tmp/apollo." + std::to_string(::getuid()) + "." + std::to_string(pid) + ".sock";
}

bool fillAddress(sockaddr_un& address, const std::string& path, std::string* error) {
    if (path.size() >= sizeof(address.sun_path)) {
        if (error) *error = "socket path is too long: " + path;
        return false;
    }
    std::memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    return true;
}

void removeAbandonedSockets() {
    const std::string prefix = "apollo." + std::to_string(::getuid()) + ".";

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/tmp", ec)) {
        if (ec) return;
        const std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) != 0 || entry.path().extension() != ".sock") continue;

        const std::string middle = name.substr(prefix.size());
        const int pid = std::atoi(middle.c_str());
        // Signal 0 asks whether the process exists without disturbing it.
        if (pid > 0 && ::kill(static_cast<pid_t>(pid), 0) == 0) continue;
        if (pid > 0 && errno == EPERM) continue; // alive, just not ours
        fs::remove(entry.path(), ec);
    }
}

} // namespace

ControlServer::~ControlServer() { stop(); }

bool ControlServer::start(std::function<void()> wake, std::string* error) {
    wake_ = std::move(wake);
    path_ = socketPathFor(::getpid());

    removeAbandonedSockets();

    // A socket left behind by a crash would refuse the bind.
    std::error_code ec;
    fs::remove(path_, ec);

    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) {
        if (error) *error = std::string("cannot create a control socket: ") + std::strerror(errno);
        return false;
    }

    sockaddr_un address{};
    if (!fillAddress(address, path_, error)) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    // Owner-only, decided before the socket exists rather than after.
    const mode_t previous = ::umask(0077);
    const int bound = ::bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    ::umask(previous);

    if (bound != 0 || ::listen(fd_, 8) != 0) {
        if (error) *error = std::string("cannot listen on ") + path_ + ": " + std::strerror(errno);
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    listening_ = true;
    thread_ = std::thread([this] { accept(); });
    return true;
}

void ControlServer::accept() {
    while (listening_) {
        pollfd waiting{fd_, POLLIN, 0};
        const int ready = ::poll(&waiting, 1, 200);
        if (!listening_) break;
        if (ready <= 0) continue;

        const int client = ::accept(fd_, nullptr, nullptr);
        if (client < 0) continue;

        std::string message;
        char buffer[512];
        while (message.size() < kMaxMessage) {
            const ssize_t got = ::read(client, buffer, sizeof(buffer));
            if (got <= 0) break;
            message.append(buffer, static_cast<std::size_t>(got));
            if (message.find('\n') != std::string::npos) break;
        }
        ::close(client);

        if (const auto newline = message.find('\n'); newline != std::string::npos) {
            message.resize(newline);
        }
        if (message.empty()) continue;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.push_back(std::move(message));
        }
        if (wake_) wake_();
    }
}

void ControlServer::stop() {
    if (!listening_ && fd_ < 0) return;

    listening_ = false;
    if (thread_.joinable()) thread_.join();
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }

    std::error_code ec;
    if (!path_.empty()) fs::remove(path_, ec);
    path_.clear();
}

std::vector<std::string> ControlServer::take() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    out.swap(pending_);
    return out;
}

namespace control {

std::string socketFromEnvironment() {
    const char* value = std::getenv("APOLLO_SOCKET");
    return (value && *value) ? value : "";
}

bool send(const std::string& socketPath, const std::string& message) {
    if (socketPath.empty()) return false;

    sockaddr_un address{};
    if (!fillAddress(address, socketPath, nullptr)) return false;

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        // Nothing listening: a stale file from an instance that is gone.
        ::close(fd);
        return false;
    }

    const std::string payload = message + "\n";
    std::size_t sent = 0;
    while (sent < payload.size()) {
        const ssize_t wrote = ::write(fd, payload.data() + sent, payload.size() - sent);
        if (wrote > 0) { sent += static_cast<std::size_t>(wrote); continue; }
        if (wrote < 0 && errno == EINTR) continue;
        ::close(fd);
        return false;
    }
    ::close(fd);
    return true;
}

} // namespace control
} // namespace apollo
