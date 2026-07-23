// Key chords, as written in the config:
//
//     bind = CTRL, K, command_palette
//     bind = CTRL SHIFT, ENTER, split, vertical
//     bind = , F1, help
//
// The first field is the modifier set (possibly empty), the second the key, the
// third an action name, and anything after it the action's arguments.
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
    // Not a real modifier: chords carrying it fire only after the leader key
    // has been pressed. Apollo binds almost everything behind the leader so it
    // does not steal Ctrl-A, Ctrl-K, Ctrl-R and friends from your shell.
    ModLeader = 1 << 4,
};

struct KeyChord {
    std::uint8_t mods = ModNone;
    std::string key; // normalised: "a", "f1", "left", "enter", "space", ...

    static std::optional<KeyChord> parse(const std::string& modifiers,
                                         const std::string& key);
    // Parses a whole "CTRL SHIFT, K" spec.
    static std::optional<KeyChord> parse(const std::string& spec);

    // "Ctrl+K", for help text and the command palette.
    std::string describe() const;
    bool empty() const { return key.empty(); }

    bool operator==(const KeyChord& o) const { return mods == o.mods && key == o.key; }
    bool operator<(const KeyChord& o) const {
        return mods != o.mods ? mods < o.mods : key < o.key;
    }
};

// One line of the config's bind table.
struct Bind {
    KeyChord chord;
    std::string action;
    std::vector<std::string> args;
    int line = -1; // where it came from, for error messages

    std::string describeAction() const;
};

// The actions Apollo knows how to perform. Kept here so the config checker, the
// help screen and the command palette all read from one list.
struct ActionInfo {
    std::string name;
    std::string summary;
    bool takesArgument = false;
};
const std::vector<ActionInfo>& knownActions();
const ActionInfo* findAction(const std::string& name);

// Turns the raw bytes a terminal sends into a chord Apollo can match against
// the bind table. Returns an empty chord for input it does not recognise,
// which the caller should forward to the pty untouched.
KeyChord decodeKey(const std::string& bytes);

} // namespace apollo
