// What a transfer copies and where it lands, as two paths you can change
// before anything moves. Both are checked first: this machine's side at once,
// the other side over the connection.
#pragma once

#include <string>

#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>

#include "core/Config.h"
#include "net/Transfer.h"
#include "ui/Widgets.h"

namespace apollo::ui {

class TransferForm {
public:
    enum class Result { None, Submit, Cancel };

    // Filled in from the job: its one source and its destination.
    void open(const transfer::Job& job, bool directory);
    void close();
    bool isOpen() const { return open_; }

    Result onKey(const KeyChord& chord, const std::string& raw);
    Result onMouse(const ftxui::Mouse& mouse);
    ftxui::Element render(const Theme& theme, const DecorationSettings& decoration,
                          int width, int height);

    // The job as the fields now read.
    transfer::Job job() const;

    // Waiting on the other machine. `id` is the check to wait for.
    void checking(int id);
    int checkId() const { return checking_ ? checkId_ : 0; }
    // A check failed. Stay open with the reason, so it can be fixed.
    void fail(const std::string& why);

private:
    enum Field { Local, Remote };
    LineEdit& field(Field which) { return which == Local ? local_ : remote_; }
    bool upload() const { return job_.direction == transfer::Direction::Upload; }
    // The source is on top, then where it goes.
    Field first() const { return upload() ? Local : Remote; }

    bool open_ = false;
    transfer::Job job_;
    bool directory_ = false;
    LineEdit local_;
    LineEdit remote_;
    Field focus_ = Local;
    bool checking_ = false;
    int checkId_ = 0;
    std::string error_;
    Hotspots spots_;
};

} // namespace apollo::ui
