#include "net/RemoteFs.h"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <sstream>

#include "core/Process.h"

namespace apollo::remote {
namespace {

// All one command, so a listing is one round trip.
// Line 1 is the directory, line 2 says which `ls` dialect answered, then the
// listing, then a short second listing just to see through symlinks to dirs.
std::string script(const std::string& path, int shellPid) {
    const std::string quoted = ssh::quoteRemotePath(path.empty() ? "." : path);
    std::string out =
        "cd -- " + quoted + " 2>/dev/null || { echo APOLLO_NODIR; exit 0; }; pwd; "
        "if ls -lA --time-style=+%s . >/dev/null 2>&1; then echo EPOCH; "
        "LC_ALL=C ls -lA --time-style=+%s . 2>/dev/null; else echo TEXT; "
        "LC_ALL=C ls -lA . 2>/dev/null; fi; echo APOLLO_DIRS; "
        "LC_ALL=C ls -ALp . 2>/dev/null";

    // Where the shell has got to. /proc on Linux, lsof on BSD/mac.
    // A machine with neither just doesn't answer and nothing follows.
    if (shellPid > 0) {
        const std::string pid = std::to_string(shellPid);
        out += "; echo APOLLO_SHELL; readlink /proc/" + pid + "/cwd 2>/dev/null || "
               "lsof -a -p " + pid + " -d cwd -Fn 2>/dev/null | sed -n 's/^n//p' | head -1";
    }
    return out;
}

// Offset of the nth whitespace-separated field. npos if there isn't one.
std::size_t fieldAt(const std::string& line, int n) {
    std::size_t at = 0;
    for (int i = 0; i < n; ++i) {
        while (at < line.size() && line[at] != ' ' && line[at] != '\t') ++at;
        while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) ++at;
        if (at >= line.size()) return std::string::npos;
    }
    return at;
}

std::vector<std::string> fields(const std::string& line, int count) {
    std::vector<std::string> out;
    std::istringstream stream(line);
    std::string word;
    while (static_cast<int>(out.size()) < count && stream >> word) out.push_back(word);
    return out;
}

// "-rw-r--r--", "drwxr-xr-x+", "lrwxrwxrwx".
// Some systems tack a + on the end for ACLs. Not part of the mode.
bool looksLikeMode(const std::string& text) {
    if (text.size() < 10) return false;
    const char type = text[0];
    if (type != '-' && type != 'd' && type != 'l' && type != 'b' && type != 'c' &&
        type != 'p' && type != 's') {
        return false;
    }
    for (std::size_t i = 1; i < 10; ++i) {
        if (text[i] != '-' && (text[i] < 'a' || text[i] > 'z') &&
            (text[i] < 'A' || text[i] > 'Z')) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t end = text.find('\n', at);
        if (end == std::string::npos) {
            if (at < text.size()) lines.push_back(text.substr(at));
            break;
        }
        lines.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    return lines;
}

} // namespace

Listing parse(const std::string& output) {
    Listing out;

    const std::vector<std::string> lines = splitLines(output);
    if (lines.empty()) {
        out.error = "no answer";
        return out;
    }
    if (lines[0] == "APOLLO_NODIR") {
        out.error = "no such directory";
        return out;
    }
    out.path = lines[0];

    const bool epoch = lines.size() > 1 && lines[1] == "EPOCH";
    // A text date is 3 fields, an epoch is 1. Changes where the name starts.
    const int nameField = epoch ? 6 : 8;

    // Long listing runs up to the marker, the symlink one from there to the end.
    std::size_t marker = lines.size();
    for (std::size_t i = 2; i < lines.size(); ++i) {
        if (lines[i] == "APOLLO_DIRS") { marker = i; break; }
    }

    // If we asked for the shell's directory it's the last thing printed.
    std::size_t shellMarker = lines.size();
    for (std::size_t i = marker + 1; i < lines.size(); ++i) {
        if (lines[i] == "APOLLO_SHELL") { shellMarker = i; break; }
    }
    if (shellMarker + 1 < lines.size()) out.shellCwd = lines[shellMarker + 1];

    std::set<std::string> directories;
    for (std::size_t i = marker + 1; i < shellMarker; ++i) {
        const std::string& name = lines[i];
        if (name.size() > 1 && name.back() == '/') {
            directories.insert(name.substr(0, name.size() - 1));
        }
    }

    for (std::size_t at = 2; at < marker; ++at) {
        const std::string& line = lines[at];
        if (line.empty()) continue;

        const std::vector<std::string> head = fields(line, 6);
        if (head.empty() || !looksLikeMode(head[0])) continue; // "total 24", and oddities

        const std::size_t nameAt = fieldAt(line, nameField);
        if (nameAt == std::string::npos) continue;

        Entry entry;
        entry.permissions = head[0].substr(1, 9);
        entry.symlink = head[0][0] == 'l';
        entry.directory = head[0][0] == 'd';
        entry.executable = entry.permissions[2] == 'x';
        entry.name = line.substr(nameAt);

        if (const auto arrow = entry.name.find(" -> "); arrow != std::string::npos) {
            entry.linkTarget = entry.name.substr(arrow + 4);
            entry.name.resize(arrow);
        }
        if (entry.name.empty() || entry.name == "." || entry.name == "..") continue;
        if (entry.symlink && directories.count(entry.name)) entry.directory = true;

        if (head.size() > 4) entry.size = std::strtoull(head[4].c_str(), nullptr, 10);
        if (epoch && head.size() > 5) {
            entry.modified = static_cast<std::time_t>(std::strtoll(head[5].c_str(), nullptr, 10));
        } else if (!epoch) {
            const std::size_t from = fieldAt(line, 5);
            if (from != std::string::npos && from < nameAt) {
                entry.modifiedText = line.substr(from, nameAt - from);
                while (!entry.modifiedText.empty() && entry.modifiedText.back() == ' ') {
                    entry.modifiedText.pop_back();
                }
            }
        }
        out.entries.push_back(std::move(entry));
    }

    out.ok = true;
    return out;
}

Listing list(const Connection& conn, const std::string& path, int shellPid) {
    Listing out;
    out.connection = conn.name;
    out.path = path;

    if (!conn.valid()) {
        out.error = "connection is missing a host or user";
        return out;
    }

    const ssh::Invocation call = ssh::command(conn, script(path, shellPid));
    const auto result = process::run(call.argv, std::chrono::seconds(12), "", call.env);
    if (result.timedOut) {
        out.error = "timed out listing " + path;
        return out;
    }
    if (!result.ok() && result.out.empty()) {
        std::string message = result.err.empty() ? "ssh exited " + std::to_string(result.exitCode)
                                                 : result.err;
        if (const auto newline = message.find('\n'); newline != std::string::npos) {
            message.resize(newline);
        }
        out.error = message;
        return out;
    }

    Listing parsed = parse(result.out);
    parsed.connection = conn.name;
    if (!parsed.ok) {
        // Name the machine -- "no such directory" alone isn't much help.
        parsed.error += parsed.error == "no such directory" ? " on " + conn.label() + ": " + path
                                                            : " from " + conn.label();
        parsed.path = path;
    }
    return parsed;
}

// --- the worker ------------------------------------------------------------

Lister::Lister(std::function<void()> wake) : state_(std::make_shared<Shared>()) {
    state_->wake = std::move(wake);
}

Lister::~Lister() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->stopping = true;
    state_->wake = nullptr; // whatever it pointed at is going away
    state_->ready.notify_all();
}

void Lister::request(const Connection& conn, const std::string& path, int shellPid) {
    bool startWorker = false;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->queued = Job{conn, path, shellPid};
        startWorker = !state_->busy && !state_->started;
        if (startWorker) state_->started = true;
    }
    if (startWorker) {
        std::thread([state = state_] {
            for (;;) {
                Job job;
                {
                    std::unique_lock<std::mutex> lock(state->mutex);
                    state->ready.wait(lock, [&] { return state->stopping || state->queued; });
                    if (state->stopping) return;
                    job = *state->queued;
                    state->queued.reset();
                    state->busy = true;
                }

                Listing answer = list(job.conn, job.path, job.shellPid);

                std::function<void()> wake;
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->busy = false;
                    if (state->stopping) return;
                    // Newer request already waiting. Nobody wants this answer.
                    if (state->queued) continue;
                    state->done = std::move(answer);
                    wake = state->wake;
                }
                if (wake) wake();
            }
        }).detach();
    }
    state_->ready.notify_one();
}

std::optional<Listing> Lister::take() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    std::optional<Listing> out;
    out.swap(state_->done);
    return out;
}

} // namespace apollo::remote
