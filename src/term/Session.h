// One terminal: a pty, a parser and a screen, plus the view state that belongs
// to looking at it (where the scrollback is parked, what is selected).
//
// Reading is deliberately split in two. A small thread waits on the pty and
// does nothing but signal that bytes are ready; the bytes themselves are read
// and parsed on the UI thread inside pump(). The screen therefore has exactly
// one writer and needs no locking, and a keystroke still turns into a frame
// without waiting for a timer to come round.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/Keys.h"
#include "term/Pty.h"
#include "term/Screen.h"
#include "term/VtParser.h"

namespace apollo::term {

struct Selection {
    bool active = false;
    int anchorLine = 0, anchorCol = 0; // absolute line numbers
    int headLine = 0, headCol = 0;

    bool empty() const { return !active || (anchorLine == headLine && anchorCol == headCol); }
    void normalised(int& fromLine, int& fromCol, int& toLine, int& toCol) const;
};

class Session {
public:
    struct Options {
        std::vector<std::string> argv;
        std::vector<std::string> env;
        std::string cwd;
        std::string title = "shell";
        std::string connection; // empty when this is the local machine
        int scrollback = 10000;
    };

    Session(int rows, int cols, int scrollback);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    bool start(const Options& options, std::string* error = nullptr);
    void resize(int rows, int cols);
    // Drains the pty and updates the screen. True when anything changed.
    bool pump();
    void close();

    bool running() const { return pty_.running(); }
    int exitCode() const { return pty_.exitCode(); }
    const std::string& title() const { return options_.title; }
    void setTitle(const std::string& title) { options_.title = title; }
    const std::string& connection() const { return options_.connection; }
    std::string windowTitle() const { return screen_.title; }
    std::string cwd() const { return screen_.cwd; }

    Screen& screen() { return screen_; }
    const Screen& screen() const { return screen_; }

    // --- input ------------------------------------------------------------
    // `raw` is exactly what the terminal sent us. Forwarding it unchanged is
    // what makes odd keys and unusual terminals work; only the few sequences
    // that depend on a mode Apollo tracks get rewritten.
    void sendKey(const KeyChord& chord, const std::string& raw);
    void sendText(const std::string& text);
    void paste(const std::string& text);
    void sendMouse(int button, int col, int row, bool pressed, bool motion, std::uint8_t mods);
    void interrupt();

    // --- looking at the scrollback ----------------------------------------
    int scrollOffset() const { return scrollOffset_; }
    bool scrolled() const { return scrollOffset_ > 0; }
    void scrollBy(int lines);
    void scrollToBottom();
    void scrollToTop();
    void scrollToLine(int absolute);
    // direction -1 goes back through the transcript, +1 forward.
    bool jumpPrompt(int direction);

    // --- text -------------------------------------------------------------
    std::string lineText(int absolute) const;
    std::string textInRange(int fromLine, int fromCol, int toLine, int toCol) const;
    std::vector<int> search(const std::string& needle) const;

    Selection selection;
    std::string selectedText() const;

    // Called from the reader thread when bytes arrive. Must be cheap and
    // thread safe: it exists to wake the UI loop up, nothing more.
    void setWakeup(std::function<void()> wake);
    std::function<void(const std::string&)> onClipboard;

private:
    void startReader();
    void stopReader();

    Pty pty_;
    Screen screen_;
    VtParser parser_;
    Options options_;

    std::thread reader_;
    std::atomic<bool> readerStop_{false};
    std::function<void()> wake_;

    int scrollOffset_ = 0;
    bool started_ = false;
};

} // namespace apollo::term
