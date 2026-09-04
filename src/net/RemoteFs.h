// Lists directories on the far side of a connection. Reuses the ssh master
// `apollo connect` already has open, so it is one round trip, not a login.
#pragma once

#include <condition_variable>
#include <ctime>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "net/Ssh.h"

namespace apollo::remote {

struct Entry {
    std::string name;
    std::string linkTarget; // for symlinks
    bool directory = false;
    bool executable = false;
    bool symlink = false;
    std::uintmax_t size = 0;
    std::time_t modified = 0;
    std::string modifiedText; // when the remote ls only gave a date to read
    std::string permissions; // "rwxr-xr-x", as the remote ls reported it
};

struct Listing {
    bool ok = false;
    std::string connection;
    std::string path;  // absolute, with ~ and symlinks already resolved remotely
    std::string error; // set when ok is false
    std::vector<Entry> entries;
    std::string shellCwd; // where the remote shell is. Empty if we can't tell
};

// This blocks, so the UI thread should use the Lister below instead.
// Pass shellPid to get that shell's directory back in the same trip. 0 skips it.
Listing list(const Connection& conn, const std::string& path, int shellPid = 0);

// Turns what the remote printed into entries. Split out from list() so the
// `ls` parsing can be tested without a machine to talk to.
Listing parse(const std::string& output);

// Runs listings on a worker thread and wakes the caller when one lands.
// Newest request wins -- a reply for a directory you already left is dropped.
class Lister {
public:
    explicit Lister(std::function<void()> wake);
    ~Lister();
    Lister(const Lister&) = delete;
    Lister& operator=(const Lister&) = delete;

    void request(const Connection& conn, const std::string& path, int shellPid = 0);
    // Whatever arrived since the last call, if anything.
    std::optional<Listing> take();

private:
    struct Job {
        Connection conn;
        std::string path;
        int shellPid = 0;
    };

    // You can't interrupt an ssh call, so the worker can outlive the Lister.
    // Both hold this and whoever finishes last frees it.
    struct Shared {
        std::mutex mutex;
        std::condition_variable ready;
        bool stopping = false;
        bool started = false;
        bool busy = false;
        std::function<void()> wake;
        std::optional<Job> queued;
        std::optional<Listing> done;
    };
    std::shared_ptr<Shared> state_;
};

} // namespace apollo::remote
