// Talking to an Apollo that's already running. Each instance listens on a
// socket and tells the shells it starts where to find it. That's what makes
// `apollo` inside Apollo act on the window you're in instead of nesting.
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace apollo {

class ControlServer {
public:
    ControlServer() = default;
    ~ControlServer();
    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    bool start(std::function<void()> wake, std::string* error = nullptr);
    void stop();

    const std::string& path() const { return path_; }
    bool running() const { return listening_; }

    std::vector<std::string> take();

private:
    void accept();

    int fd_ = -1;
    std::string path_;
    std::thread thread_;
    std::atomic<bool> listening_{false};
    std::function<void()> wake_;

    std::mutex mutex_;
    std::vector<std::string> pending_;
};

namespace control {

std::string socketFromEnvironment();

bool send(const std::string& socketPath, const std::string& message);

} // namespace control
} // namespace apollo
