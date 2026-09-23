// Copying files to and from a connection. Rides the same ssh master that
// `apollo connect` and the browser already use, so a transfer to a machine
// you're connected to starts without a handshake.
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "net/Ssh.h"

namespace apollo::transfer {

enum class Direction { Upload, Download };

struct Job {
    Connection conn;
    Direction direction = Direction::Upload;
    std::vector<std::string> sources; // local paths to upload, remote ones to download
    std::string destination;          // a directory on the other side, or a new name there
};

// The result of Runner::verify.
struct Verdict {
    int id = 0;
    Job job;
    std::string error; // empty: good to go
};

struct Outcome {
    Job job;
    bool ok = false;
    std::string error; // what scp said, first line, when it failed
    std::chrono::steady_clock::duration took{};
};

// `user@host:path`, the way scp wants it. Home-relative paths lose the ~,
// since a bare relative path already means home to scp and a quoted ~ doesn't.
std::string remoteSpec(const Connection& conn, const std::string& path);

// The scp call for a job. `quiet` drops the progress meter for callers that
// have no terminal to draw it on; `batch` refuses to prompt for anything.
ssh::Invocation command(const Job& job, bool quiet, bool batch);

// "app.log", "3 files" -- what a status line calls the sources.
std::string describe(const Job& job);
// "lab:~/work" or "~/Downloads".
std::string describeDestination(const Job& job);

// --- checking a job before it runs ------------------------------------------
// Both return empty when the job can go ahead, otherwise what's wrong with it.

// This machine's side: what's sent exists, or where it lands does. Instant.
std::string checkLocal(const Job& job);
// The other side, over ssh: one round trip, so it blocks. The Runner has an
// asynchronous version for the UI thread.
std::string checkRemote(const Job& job);
// What the remote side said, decoded. Split out so it can be tested without
// a machine to ask.
std::string readRemoteCheck(const Job& job, const std::string& answer);
// The script checkRemote runs over there.
std::string remoteCheckScript(const Job& job);

// `name:path` names a configured connection; anything else is local.
struct Target {
    std::string connection; // empty for a local path
    std::string path;
};
Target parseTarget(const std::string& text,
                   const std::function<bool(const std::string&)>& isConnection);

// Runs jobs off the UI thread, each on its own, and wakes the caller when one
// finishes. A transfer can't be interrupted, so a worker can outlive this;
// both hold the shared state and whoever finishes last frees it.
class Runner {
public:
    explicit Runner(std::function<void()> wake);
    ~Runner();
    Runner(const Runner&) = delete;
    Runner& operator=(const Runner&) = delete;

    void start(Job job);
    std::vector<Outcome> take();

    // Checks the remote side of a job off this thread. Returns an id to match
    // the verdict against, since an older check can land after a newer one.
    int verify(Job job);
    std::vector<Verdict> takeVerdicts();

    int running() const;
    // The oldest transfer still going, for the status bar.
    std::optional<Job> current() const;
    std::chrono::steady_clock::duration currentElapsed() const;

private:
    struct Active {
        int id = 0;
        Job job;
        std::chrono::steady_clock::time_point started;
    };
    struct Shared {
        mutable std::mutex mutex;
        bool stopping = false;
        int nextId = 0;
        std::function<void()> wake;
        std::vector<Active> active;
        std::vector<Outcome> done;
        std::vector<Verdict> verdicts;
    };
    std::shared_ptr<Shared> state_;
};

} // namespace apollo::transfer
