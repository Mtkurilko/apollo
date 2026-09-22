#include "net/Transfer.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <thread>

#include "core/Paths.h"
#include "core/Process.h"

namespace fs = std::filesystem;

namespace apollo::transfer {

std::string remoteSpec(const Connection& conn, const std::string& path) {
    // scp reads a colon in the host as the end of it. Brackets keep IPv6 whole.
    const std::string host =
        conn.host.find(':') != std::string::npos ? "[" + conn.host + "]" : conn.host;

    std::string where = path;
    if (where == "~" || where == "~/") where.clear();
    else if (where.rfind("~/", 0) == 0) where.erase(0, 2);
    return conn.user + "@" + host + ":" + where;
}

ssh::Invocation command(const Job& job, bool quiet, bool batch) {
    // -r does no harm on a file and saves looking. -p keeps the dates, so the
    // copy sorts where the original did.
    std::vector<std::string> args = {"-r", "-p"};
    if (quiet) args.push_back("-q");

    if (job.direction == Direction::Upload) {
        for (const auto& source : job.sources) args.push_back(source);
        args.push_back(remoteSpec(job.conn, job.destination));
    } else {
        for (const auto& source : job.sources) args.push_back(remoteSpec(job.conn, source));
        args.push_back(job.destination.empty() ? "." : job.destination);
    }
    return ssh::copy(job.conn, args, batch);
}

std::string describe(const Job& job) {
    if (job.sources.size() == 1) {
        std::string name = fs::path(job.sources.front()).filename().string();
        if (name.empty()) name = fs::path(job.sources.front()).parent_path().filename().string();
        return name.empty() ? job.sources.front() : name;
    }
    return std::to_string(job.sources.size()) + " files";
}

std::string describeDestination(const Job& job) {
    if (job.direction == Direction::Download) return job.destination;
    return job.conn.name + ":" + (job.destination.empty() ? "~" : job.destination);
}

// --- checking -------------------------------------------------------------

std::string checkLocal(const Job& job) {
    std::error_code ec;
    if (job.direction == Direction::Upload) {
        if (job.sources.empty() || job.sources.front().empty()) return "say what to send";
        for (const auto& source : job.sources) {
            if (!fs::exists(paths::expandUser(source), ec)) return "nothing here called " + source;
        }
        return "";
    }

    // A download lands in a directory, or becomes a new name inside one.
    if (job.destination.empty()) return "say where on this machine it should go";
    const fs::path where = paths::expandUser(job.destination);
    if (fs::is_directory(where, ec)) return "";
    if (fs::exists(where, ec)) {
        return job.sources.size() == 1 ? "" : job.destination + " is a file, not a directory";
    }
    const fs::path parent = where.parent_path().empty() ? fs::path(".") : where.parent_path();
    if (!fs::is_directory(parent, ec)) return "no directory here called " + parent.string();
    return "";
}

std::string remoteCheckScript(const Job& job) {
    // One line of answer per path: DIR, FILE, NEW (missing, but its directory
    // is there), or MISSING.
    const auto probe = [](const std::string& path) {
        const std::string quoted = ssh::quoteRemotePath(path.empty() ? "~" : path);
        return "p=" + quoted + "; if [ -d \"$p\" ]; then echo DIR; elif [ -e \"$p\" ]; then echo FILE; "
               "elif [ -d \"$(dirname -- \"$p\")\" ]; then echo NEW; else echo MISSING; fi";
    };
    if (job.direction == Direction::Upload) return probe(job.destination);

    std::string script;
    for (const auto& source : job.sources) script += (script.empty() ? "" : "; ") + probe(source);
    return script;
}

std::string readRemoteCheck(const Job& job, const std::string& answer) {
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at < answer.size()) {
        const std::size_t end = answer.find('\n', at);
        lines.push_back(answer.substr(at, end == std::string::npos ? std::string::npos : end - at));
        if (end == std::string::npos) break;
        at = end + 1;
    }
    const std::string where = job.conn.name;

    if (job.direction == Direction::Upload) {
        const std::string state = lines.empty() ? "" : lines.front();
        const std::string path = job.destination.empty() ? "~" : job.destination;
        if (state == "DIR") return "";
        if (state == "NEW") {
            return job.sources.size() == 1 ? "" : "no directory on " + where + " called " + path;
        }
        if (state == "FILE") {
            // Replacing a file with a file is a copy. Anything else isn't.
            std::error_code ec;
            const bool sendingFile =
                job.sources.size() == 1 && !fs::is_directory(paths::expandUser(job.sources[0]), ec);
            return sendingFile ? "" : path + " on " + where + " is a file, not a directory";
        }
        if (state == "MISSING") return "no directory on " + where + " called " + path;
        return "couldn't check " + where;
    }

    for (std::size_t i = 0; i < job.sources.size(); ++i) {
        const std::string state = i < lines.size() ? lines[i] : "";
        if (state == "DIR" || state == "FILE") continue;
        if (state == "NEW" || state == "MISSING") {
            return "nothing on " + where + " called " + job.sources[i];
        }
        return "couldn't check " + where;
    }
    return "";
}

std::string checkRemote(const Job& job) {
    const ssh::Invocation call = ssh::command(job.conn, remoteCheckScript(job));
    const process::Result result = process::run(call.argv, std::chrono::seconds(15), "", call.env);
    if (!result.ok()) {
        std::string said = result.err.substr(0, result.err.find('\n'));
        if (said.empty()) said = result.timedOut ? "no answer" : "ssh failed";
        return "couldn't reach " + job.conn.name + ": " + said;
    }
    return readRemoteCheck(job, result.out);
}

Target parseTarget(const std::string& text,
                   const std::function<bool(const std::string&)>& isConnection) {
    const auto colon = text.find(':');
    if (colon != std::string::npos && colon > 0) {
        const std::string name = text.substr(0, colon);
        const bool plainName = std::all_of(name.begin(), name.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '-' || c == '_';
        });
        if (plainName && isConnection(name)) return {name, text.substr(colon + 1)};
    }
    return {"", text};
}

// --- running them ----------------------------------------------------------

Runner::Runner(std::function<void()> wake) : state_(std::make_shared<Shared>()) {
    state_->wake = std::move(wake);
}

Runner::~Runner() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->stopping = true;
}

void Runner::start(Job job) {
    int id = 0;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        id = ++state_->nextId;
        state_->active.push_back({id, job, std::chrono::steady_clock::now()});
    }

    std::thread([shared = state_, job = std::move(job), id] {
        const auto began = std::chrono::steady_clock::now();
        const ssh::Invocation call = command(job, true, true);
        // Big files take as long as they take. The ssh side gives up on its own
        // if the link goes away.
        const process::Result result =
            process::run(call.argv, std::chrono::hours(12), "", call.env);

        Outcome outcome;
        outcome.job = job;
        outcome.ok = result.ok();
        outcome.took = std::chrono::steady_clock::now() - began;
        if (!outcome.ok) {
            std::string said = result.err.empty() ? result.out : result.err;
            said = said.substr(0, said.find('\n'));
            if (said.empty()) said = "scp exited " + std::to_string(result.exitCode);
            outcome.error = said;
        }

        std::function<void()> wake;
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            auto& active = shared->active;
            active.erase(std::remove_if(active.begin(), active.end(),
                                        [id](const Active& a) { return a.id == id; }),
                         active.end());
            if (shared->stopping) return;
            shared->done.push_back(std::move(outcome));
            wake = shared->wake;
        }
        if (wake) wake();
    }).detach();
}

int Runner::verify(Job job) {
    int id = 0;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        id = ++state_->nextId;
    }
    std::thread([shared = state_, job = std::move(job), id] {
        Verdict verdict{id, job, checkRemote(job)};
        std::function<void()> wake;
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            if (shared->stopping) return;
            shared->verdicts.push_back(std::move(verdict));
            wake = shared->wake;
        }
        if (wake) wake();
    }).detach();
    return id;
}

std::vector<Verdict> Runner::takeVerdicts() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return std::exchange(state_->verdicts, {});
}

std::vector<Outcome> Runner::take() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return std::exchange(state_->done, {});
}

int Runner::running() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return static_cast<int>(state_->active.size());
}

std::optional<Job> Runner::current() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->active.empty()) return std::nullopt;
    return state_->active.front().job;
}

std::chrono::steady_clock::duration Runner::currentElapsed() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->active.empty()) return {};
    return std::chrono::steady_clock::now() - state_->active.front().started;
}

} // namespace apollo::transfer
