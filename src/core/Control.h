// Talking to a running Apollo.
//
// Running `apollo` inside Apollo used to start a second one nested in the
// first, which is never what anybody wanted. Instead, each instance listens on
// a private socket and exports its path to the shells it starts, so a command
// typed in Apollo's own terminal reaches the Apollo around it: `apollo` goes
// home, `apollo quit` quits, `apollo config` opens the settings.
//
// The socket lives in /tmp, is named for the user and the process, and is
// created with owner-only permissions. Anything arriving on it was typed by
// the user into their own terminal, which is the same trust as the keyboard.
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

    // Starts listening. `wake` is called from the listening thread whenever a
    // message arrives; it must be cheap and thread safe.
    bool start(std::function<void()> wake, std::string* error = nullptr);
    void stop();

    const std::string& path() const { return path_; }
    bool running() const { return listening_; }

    // Everything received since the last call, in order.
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

// The socket this process was told about, or empty when Apollo is not the
// terminal we are running in.
std::string socketFromEnvironment();

// Sends one message and returns whether it was delivered. A socket that is
// there but dead — a crashed instance, a stale file — reports false, so the
// caller can carry on as if nothing was listening.
bool send(const std::string& socketPath, const std::string& message);

} // namespace control
} // namespace apollo
