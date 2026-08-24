// Listing directories on the other end of a connection, over the ssh master
// `apollo connect` already keeps open.
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
    // Where the connected shell is, when it could be found. Empty on a remote
    // that offers neither /proc nor lsof.
    std::string shellCwd;
};

// One round trip. Blocks, so callers on the UI thread want the Lister below.
// `shellPid` is the connected shell, so its directory can come back with the
// listing rather than costing a second round trip. Zero to skip that.
Listing list(const Connection& conn, const std::string& path, int shellPid = 0);

// What the remote printed, turned into entries. Split out from list() because
// `ls` output is the part worth testing without a machine to talk to.
Listing parse(const std::string& output);

// Runs listings on a worker thread and wakes the caller when one lands. Only
// the newest request matters: a stale reply for a directory nobody is looking
// at any more is thrown away.
class Lister {
public:
    explicit Lister(std::function<void()> wake);
    ~Lister();
    Lister(const Lister&) = delete;
    Lister& operator=(const Lister&) = delete;

    void request(const Connection& conn, const std::string& path, int shellPid = 0);
    // The listing that arrived since the last call, if there was one.
    std::optional<Listing> take();
    bool busy() const;

private:
    struct Job {
        Connection conn;
        std::string path;
        int shellPid = 0;
    };

    // The worker outlives the Lister when a listing is still in flight — an
    // ssh call cannot be interrupted — so what they share is held by both and
    // freed by whichever finishes last.
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
