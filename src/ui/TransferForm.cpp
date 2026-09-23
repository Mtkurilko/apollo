#include "ui/TransferForm.h"

#include <algorithm>

#include "core/ConfigFile.h"

namespace apollo::ui {

using namespace ftxui;

namespace {
constexpr int kLocalField = 1;
constexpr int kRemoteField = 2;
constexpr int kSubmit = 3;
constexpr int kCancel = 4;
} // namespace

void TransferForm::open(const transfer::Job& job, bool directory) {
    open_ = true;
    job_ = job;
    directory_ = directory;
    checking_ = false;
    error_.clear();

    const std::string source = job.sources.empty() ? "" : job.sources.front();
    const std::string destination = job.destination.empty() ? "~" : job.destination;
    local_.set(upload() ? source : destination);
    remote_.set(upload() ? destination : source);
    focus_ = first();
}

void TransferForm::close() {
    open_ = false;
    checking_ = false;
}

transfer::Job TransferForm::job() const {
    transfer::Job out = job_;
    const std::string local = ConfigFile::trim(local_.text);
    const std::string remote = ConfigFile::trim(remote_.text);
    out.sources = {upload() ? local : remote};
    out.destination = upload() ? remote : local;
    return out;
}

void TransferForm::checking(int id) {
    checking_ = true;
    checkId_ = id;
    error_.clear();
}

void TransferForm::fail(const std::string& why) {
    checking_ = false;
    error_ = why;
}

TransferForm::Result TransferForm::onKey(const KeyChord& chord, const std::string& raw) {
    if (!open_) return Result::None;
    if (chord.key == "escape" || (chord.mods == ModCtrl && chord.key == "c")) {
        close();
        return Result::Cancel;
    }
    // Nothing to edit while the other machine is being asked.
    if (checking_) return Result::None;

    if (chord.key == "enter") return Result::Submit;
    if (chord.key == "tab" || chord.key == "up" || chord.key == "down") {
        focus_ = focus_ == Local ? Remote : Local;
        return Result::None;
    }
    if (field(focus_).onKey(chord, raw)) error_.clear();
    return Result::None;
}

TransferForm::Result TransferForm::onMouse(const Mouse& mouse) {
    if (!open_) return Result::None;
    if (mouse.button != Mouse::Left || mouse.motion != Mouse::Pressed) return Result::None;

    switch (spots_.at(mouse.x, mouse.y)) {
        case kLocalField:  if (!checking_) focus_ = Local; break;
        case kRemoteField: if (!checking_) focus_ = Remote; break;
        case kSubmit:      return checking_ ? Result::None : Result::Submit;
        case kCancel:      close(); return Result::Cancel;
        default: break;
    }
    return Result::None;
}

Element TransferForm::render(const Theme& theme, const DecorationSettings& decoration,
                             int width, int height) {
    spots_.clear();
    const int panelWidth = std::clamp(width - 8, 52, 84);
    const std::string where = job_.conn.name;

    const auto row = [&](Field which) {
        const bool focused = focus_ == which && !checking_;
        const bool isSource = which == first();
        const std::string label = which == Local ? "Local " : "Remote";
        const std::string placeholder =
            which == Local ? (isSource ? "a file or folder here" : "a directory here")
                           : (isSource ? "a file or folder on " + where : "~ on " + where);
        const std::string help =
            isSource ? (upload() ? "what to send" : "what to fetch")
                     : (which == Local ? "a directory here, or a new name for it"
                                       : "a directory on " + where + ", or a new name for it");
        return spots_.track(which == Local ? kLocalField : kRemoteField, vbox({
            hbox({
                text(focused ? " ▸ " : "   ") | color(toFtx(theme.accent)),
                text(label + "  ") | bold | color(toFtx(focused ? theme.fg : theme.muted)),
                field(which).render(theme, placeholder, focused, false, panelWidth - 15),
                filler(),
            }),
            text("           " + help) | color(toFtx(theme.muted)),
        }));
    };
    const Field second = first() == Local ? Remote : Local;

    Element status = text("");
    if (checking_) {
        status = text(" checking " + where + "…") | color(toFtx(theme.accent));
    } else if (!error_.empty()) {
        status = text(" " + elide(error_, panelWidth - 4)) | color(toFtx(theme.error));
    }

    const std::string verb = upload() ? "send" : "fetch";
    Element content = vbox({
        hbox({
            text(upload() ? " Send to " + where : " Fetch from " + where) | bold |
                color(toFtx(theme.accent)),
            filler(),
            text(job_.conn.describe() + " ") | color(toFtx(theme.muted)),
        }),
        separator() | color(toFtx(theme.border)),
        text(""),
        row(first()),
        text(""),
        row(second),
        text(""),
        text(std::string(" ") + (directory_ ? "A directory is copied whole. " : "") +
             "Anything there with the same name is replaced.") |
            color(toFtx(theme.muted)),
        status,
        separator() | color(toFtx(theme.border)),
        hbox({
            text(" "),
            spots_.track(kSubmit, hint("Enter", verb, theme)),
            text("   "),
            hint("Tab", "other path", theme),
            text("   "),
            spots_.track(kCancel, hint("Esc", "cancel", theme)),
            filler(),
        }),
    });
    return modal(std::move(content), theme, decoration, panelWidth, height - 2);
}

} // namespace apollo::ui
