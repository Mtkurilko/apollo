#include "term/Session.h"

#include <poll.h>
#include <signal.h>

#include <algorithm>
#include <cstring>

namespace apollo::term {
namespace {

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return text;
}

} // namespace

void Selection::normalized(int& fromLine, int& fromCol, int& toLine, int& toCol) const {
    const bool forward = anchorLine < headLine ||
                         (anchorLine == headLine && anchorCol <= headCol);
    fromLine = forward ? anchorLine : headLine;
    fromCol = forward ? anchorCol : headCol;
    toLine = forward ? headLine : anchorLine;
    toCol = forward ? headCol : anchorCol;
}

Session::Session(int rows, int cols, int scrollback)
    : screen_(rows, cols, scrollback), parser_(screen_) {
    parser_.onClipboard = [this](const std::string& payload) {
        if (onClipboard) onClipboard(payload);
    };
}

Session::~Session() {
    stopReader();
    pty_.terminate();
}

bool Session::start(const Options& options, std::string* error) {
    options_ = options;
    screen_.setScrollbackLimit(options.scrollback);

    Pty::Launch launch;
    launch.argv = options.argv;
    launch.cwd = options.cwd;
    launch.env = options.env;

    launch.env.push_back("TERM=xterm-256color");
    launch.env.push_back("COLORTERM=truecolor");
    launch.env.push_back("TERM_PROGRAM=Apollo");
    launch.env.push_back("APOLLO=1");

    if (!pty_.start(launch, screen_.rows(), screen_.cols(), error)) return false;

    started_ = true;
    startReader();
    return true;
}

void Session::close() {
    stopReader();
    pty_.terminate();
    started_ = false;
}

void Session::startReader() {
    readerStop_ = false;
    announced_ = false;
    // The thread never touches the screen. It only says bytes are ready.
    reader_ = std::thread([this] {
        while (!readerStop_) {
            // Wait for pump() to take the last batch. poll() below reports the
            // bytes as ready for as long as they sit unread, so announcing
            // again before they are drained would just spin the thread.
            {
                std::unique_lock<std::mutex> lock(readerLock_);
                readerWake_.wait(lock, [this] { return !announced_ || readerStop_; });
            }
            if (readerStop_) break;

            pollfd waiting{pty_.fd(), POLLIN, 0};
            const int ready = ::poll(&waiting, 1, 100);
            if (readerStop_) break;
            if (ready <= 0) continue;

            {
                std::lock_guard<std::mutex> lock(readerLock_);
                announced_ = true;
            }
            if (wake_) wake_();
        }
    });
}

void Session::stopReader() {
    {
        std::lock_guard<std::mutex> lock(readerLock_);
        readerStop_ = true;
    }
    readerWake_.notify_all();
    if (reader_.joinable()) reader_.join();
}

void Session::setWakeup(std::function<void()> wake) { wake_ = std::move(wake); }

void Session::resize(int rows, int cols) {
    if (rows == screen_.rows() && cols == screen_.cols()) return;
    screen_.resize(rows, cols);
    pty_.resize(rows, cols);
}

bool Session::pump() {
    if (!started_) return false;

    // Cleared before the read, so bytes that land while we are draining get
    // announced again rather than sitting unnoticed until the next beat.
    {
        std::lock_guard<std::mutex> lock(readerLock_);
        announced_ = false;
    }
    readerWake_.notify_one();

    bool changed = false;
    char buffer[65536];
    const int historyBefore = screen_.historyLines();

    // Bounded so `yes` or a huge `cat` can't starve the rest of the frame.
    for (int round = 0; round < 32; ++round) {
        const std::ptrdiff_t got = pty_.read(buffer, sizeof(buffer));
        if (got > 0) {
            parser_.feed(std::string_view(buffer, static_cast<std::size_t>(got)));
            changed = true;
            continue;
        }
        if (got < 0) { pty_.poll(); break; }
        break;
    }

    if (const std::string replies = parser_.takeReplies(); !replies.empty()) {
        pty_.write(replies);
    }
    if (changed && scrollOffset_ > 0) {
        const int pushed = screen_.historyLines() - historyBefore;
        scrollOffset_ = std::clamp(scrollOffset_ + pushed, 0, screen_.historyLines());
    }

    if (screen_.commandRunning != wasRunning_) {
        wasRunning_ = screen_.commandRunning;
        if (wasRunning_) commandStarted_ = std::chrono::steady_clock::now();
    }
    if (pty_.poll()) changed = true;
    return changed;
}

// --- input -----------------------------------------------------------------

void Session::noteInput(Line state) {
    line_ = state;
    if (state == Line::Submitted) promptsAtSubmit_ = screen_.promptsSeen;
}

bool Session::atEmptyPrompt() const {
    if (!pty_.running() || screen_.alternate() || screen_.commandRunning) return false;
    // Locally we can see whether anything besides the shell has the terminal.
    // Over ssh the foreground is always ssh, so this only rules things out.
    if (pty_.foregroundBusy()) return false;

    switch (line_) {
        case Line::Empty: return true;
        case Line::Typed: return false;
        case Line::Submitted:
            // With prompt marks, wait for the next prompt: `cat` or a REPL
            // reading a line never draws one. Without them, trust the Enter.
            return screen_.promptsSeen == 0 || screen_.promptsSeen != promptsAtSubmit_;
    }
    return false;
}

void Session::sendText(const std::string& text) {
    if (text.empty()) return;
    scrollToBottom();
    if (text != "\f") {
        noteInput(text.back() == '\r' || text.back() == '\n' ? Line::Submitted : Line::Typed);
    }
    pty_.write(text);
}

void Session::sendKey(const KeyChord& chord, const std::string& raw) {
    scrollToBottom();

    if (chord.key == "enter" && chord.mods == ModNone) {
        noteInput(Line::Submitted);
    } else if (chord.mods == ModCtrl && chord.key == "c") {
        // Throws the line away. With prompt marks, wait to see a prompt, since
        // a REPL comes back from Ctrl-C too.
        noteInput(screen_.promptsSeen > 0 ? Line::Submitted : Line::Empty);
    } else if (chord.mods == ModCtrl && chord.key == "u") {
        if (line_ == Line::Typed) noteInput(Line::Empty); // killed what was typed
    } else if (chord.mods == ModCtrl && chord.key == "l") {
        // Redraws the screen and leaves the line as it was.
    } else {
        noteInput(Line::Typed);
    }

    if (screen_.applicationCursor && chord.mods == ModNone) {
        static const std::pair<const char*, char> map[] = {
            {"up", 'A'}, {"down", 'B'}, {"right", 'C'}, {"left", 'D'},
            {"home", 'H'}, {"end", 'F'},
        };
        for (const auto& [name, final] : map) {
            if (chord.key == name) {
                const std::string encoded = std::string("\x1BO") + final;
                pty_.write(encoded);
                return;
            }
        }
    }
    pty_.write(raw);
}

void Session::paste(const std::string& text) {
    if (text.empty()) return;
    scrollToBottom();
    noteInput(Line::Typed);

    // Bracketed paste keeps editors from auto-indenting every pasted line.
    if (screen_.bracketedPaste) {
        pty_.write("\x1B[200~");
        pty_.write(text);
        pty_.write("\x1B[201~");
        return;
    }
    // Without it every newline submits a line. A real keyboard sends CR.
    std::string safe = text;
    std::replace(safe.begin(), safe.end(), '\n', '\r');
    pty_.write(safe);
}

void Session::sendMouse(int button, int col, int row, bool pressed, bool motion,
                        std::uint8_t mods) {
    if (screen_.mouseTracking == 0) return;
    if (motion && screen_.mouseTracking < 1002) return;

    int code = button;
    if (motion) code += 32;
    if (mods & ModShift) code += 4;
    if (mods & ModAlt) code += 8;
    if (mods & ModCtrl) code += 16;

    if (screen_.mouseSgr) {
        pty_.write("\x1B[<" + std::to_string(code) + ";" + std::to_string(col + 1) + ";" +
                   std::to_string(row + 1) + (pressed ? "M" : "m"));
        return;
    }
    // The original encoding can't express coordinates past 223.
    if (col > 222 || row > 222) return;
    std::string out = "\x1B[M";
    out.push_back(static_cast<char>(32 + (pressed ? code : 3)));
    out.push_back(static_cast<char>(32 + col + 1));
    out.push_back(static_cast<char>(32 + row + 1));
    pty_.write(out);
}

// --- scrollback ------------------------------------------------------------

void Session::scrollBy(int lines) {
    scrollOffset_ = std::clamp(scrollOffset_ + lines, 0, screen_.historyLines());
}

void Session::scrollToBottom() { scrollOffset_ = 0; }

void Session::scrollToTop() { scrollOffset_ = screen_.historyLines(); }

void Session::scrollToLine(int absolute) {
    const int bottom = screen_.totalLines();
    const int wanted = std::clamp(absolute, 0, std::max(0, bottom - 1));
    scrollOffset_ = std::clamp(bottom - screen_.rows() - wanted, 0, screen_.historyLines());
}

bool Session::jumpPrompt(int direction) {
    const auto marks = screen_.promptLines();
    if (marks.empty()) return false;

    const int top = screen_.totalLines() - screen_.rows() - scrollOffset_;
    if (direction < 0) {
        for (auto it = marks.rbegin(); it != marks.rend(); ++it) {
            if (*it < top) { scrollToLine(*it); return true; }
        }
        scrollToLine(marks.front());
        return true;
    }
    for (const int mark : marks) {
        if (mark > top) { scrollToLine(mark); return true; }
    }
    scrollToBottom();
    return true;
}

// --- text ------------------------------------------------------------------

std::string Session::lineText(int absolute) const {
    const Row& row = screen_.lineAt(absolute);
    std::string out;
    for (const auto& cell : row.cells) {
        if (cell.width == 0) continue; // the tail of a wide glyph
        appendUtf8(out, cell.cp == 0 ? U' ' : cell.cp);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string Session::textInRange(int fromLine, int fromCol, int toLine, int toCol) const {
    std::string out;
    for (int line = fromLine; line <= toLine; ++line) {
        const Row& row = screen_.lineAt(line);
        const int begin = line == fromLine ? fromCol : 0;
        const int end = line == toLine ? toCol : static_cast<int>(row.cells.size());

        std::string piece;
        for (int x = begin; x < end && x < static_cast<int>(row.cells.size()); ++x) {
            const Cell& cell = row.cells[static_cast<std::size_t>(x)];
            if (cell.width == 0) continue;
            appendUtf8(piece, cell.cp == 0 ? U' ' : cell.cp);
        }
        while (!piece.empty() && piece.back() == ' ') piece.pop_back();
        out += piece;

        if (line < toLine && !row.wrapped) out.push_back('\n');
    }
    return out;
}

std::vector<int> Session::search(const std::string& needle) const {
    std::vector<int> hits;
    if (needle.empty()) return hits;

    const std::string wanted = lowercase(needle);
    for (int line = 0; line < screen_.totalLines(); ++line) {
        if (lowercase(lineText(line)).find(wanted) != std::string::npos) hits.push_back(line);
    }
    return hits;
}

std::chrono::steady_clock::duration Session::commandElapsed() const {
    if (!screen_.commandRunning) return std::chrono::steady_clock::duration::zero();
    return std::chrono::steady_clock::now() - commandStarted_;
}

std::string Session::selectedText() const {
    if (selection.empty()) return "";
    int fromLine = 0, fromCol = 0, toLine = 0, toCol = 0;
    selection.normalized(fromLine, fromCol, toLine, toCol);
    return textInRange(fromLine, fromCol, toLine, toCol);
}

} // namespace apollo::term
