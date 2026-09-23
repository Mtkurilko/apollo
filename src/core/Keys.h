// Key chords, as written in the config:
//
//     bind   = CTRL, K, command_palette
//     bind   = LEADER, B, toggle_browser
//     leader = space            # or ctrl+space, C-a, "CTRL, B", ...
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace apollo {

enum Mod : std::uint8_t {
    ModNone  = 0,
    ModCtrl  = 1 << 0,
    ModAlt   = 1 << 1,
    ModShift = 1 << 2,
    ModSuper = 1 << 3,
    ModLeader = 1 << 4,
};

struct KeyChord {
    std::uint8_t mods = ModNone;
    std::string key; // normalized: "a", "f1", "left", "enter", "space", ...

    static std::optional<KeyChord> parse(const std::string& modifiers,
                                         const std::string& key);
    // One string: "CTRL, SPACE", "ctrl+space", "C-a", "space", "`".
    static std::optional<KeyChord> parse(const std::string& spec);
    // Several: "ctrl+space, ctrl+backslash" or "ctrl+space ctrl+]". A single
    // chord in any spelling parse() takes works too. Empty gives none.
    static std::optional<std::vector<KeyChord>> parseList(const std::string& spec);
    // "Ctrl+Space or Ctrl+\".
    static std::string describeList(const std::vector<KeyChord>& chords);

    std::string describe() const;
    // Written back the way the config spells it: "SPACE", "CTRL+A".
    std::string spec() const;
    bool empty() const { return key.empty(); }
    // A key that types something on its own, like Space. As the leader it can
    // only take over where typing it would mean nothing.
    bool typesText() const { return mods == ModNone && (key == "space" || key.size() == 1); }

    bool operator==(const KeyChord& o) const { return mods == o.mods && key == o.key; }
    bool operator!=(const KeyChord& o) const { return !(*this == o); }
    bool operator<(const KeyChord& o) const {
        return mods != o.mods ? mods < o.mods : key < o.key;
    }
};

struct Bind {
    KeyChord chord;
    std::string action;
    std::vector<std::string> args;
    int line = -1; // where it came from, for error messages

    std::string describeAction() const;
};

struct ActionInfo {
    std::string name;
    std::string summary;
    bool takesArgument = false;
};
const std::vector<ActionInfo>& knownActions();
const ActionInfo* findAction(const std::string& name);

KeyChord decodeKey(const std::string& bytes);

} // namespace apollo
