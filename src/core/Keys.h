// Key chords, as written in the config:
//
//     bind = CTRL, K, command_palette
//     bind = LEADER, B, toggle_browser
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
    std::string key; // normalised: "a", "f1", "left", "enter", "space", ...

    static std::optional<KeyChord> parse(const std::string& modifiers,
                                         const std::string& key);
    static std::optional<KeyChord> parse(const std::string& spec);

    std::string describe() const;
    bool empty() const { return key.empty(); }

    bool operator==(const KeyChord& o) const { return mods == o.mods && key == o.key; }
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
