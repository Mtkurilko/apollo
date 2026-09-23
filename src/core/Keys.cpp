#include "core/Keys.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace apollo {
namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return text;
}

std::string trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t");
    if (begin == std::string::npos) return "";
    return text.substr(begin, text.find_last_not_of(" \t") - begin + 1);
}

const std::map<std::string, std::string>& keyAliases() {
    static const std::map<std::string, std::string> table = {
        {"return", "enter"},   {"cr", "enter"},        {"ret", "enter"},
        {"esc", "escape"},     {"bs", "backspace"},    {"del", "delete"},
        {"pgup", "pageup"},    {"pgdn", "pagedown"},   {"pgdown", "pagedown"},
        {"prior", "pageup"},   {"next", "pagedown"},   {"spc", "space"},
        {"ins", "insert"},     {"arrowleft", "left"},  {"arrowright", "right"},
        {"arrowup", "up"},     {"arrowdown", "down"},  {"tab", "tab"},
        // Bind lines are comma-separated, so `bind = LEADER, ,, x` can't work.
        {"comma", ","},        {"period", "."},        {"dot", "."},
        {"slash", "/"},        {"backslash", "\\"},    {"minus", "-"},
        {"dash", "-"},         {"equal", "="},         {"plus", "+"},
        {"semicolon", ";"},    {"colon", ":"},         {"apostrophe", "'"},
        {"quote", "\""},       {"grave", "`"},         {"tilde", "~"},
        {"bracketleft", "["},  {"bracketright", "]"},
    };
    return table;
}

const std::map<std::string, std::string>& keyLabels() {
    static const std::map<std::string, std::string> table = {
        {"enter", "Enter"},   {"escape", "Esc"},      {"backspace", "Backspace"},
        {"delete", "Del"},    {"pageup", "PgUp"},     {"pagedown", "PgDn"},
        {"space", "Space"},   {"tab", "Tab"},         {"left", "Left"},
        {"right", "Right"},   {"up", "Up"},           {"down", "Down"},
        {"home", "Home"},     {"end", "End"},         {"insert", "Ins"},
    };
    return table;
}

} // namespace

std::optional<KeyChord> KeyChord::parse(const std::string& modifiers,
                                        const std::string& key) {
    KeyChord chord;

    std::istringstream mods(modifiers);
    std::string token;
    while (mods >> token) {
        const std::string name = lower(token);
        if (name == "ctrl" || name == "control") chord.mods |= ModCtrl;
        else if (name == "alt" || name == "opt" || name == "option" || name == "meta") chord.mods |= ModAlt;
        else if (name == "shift") chord.mods |= ModShift;
        else if (name == "super" || name == "cmd" || name == "command" || name == "win") chord.mods |= ModSuper;
        else if (name == "leader" || name == "mod" || name == "prefix") chord.mods |= ModLeader;
        else if (name == "none" || name == "-") continue;
        else return std::nullopt;
    }

    std::string name = lower(trim(key));
    if (name.empty()) return std::nullopt;

    if (const auto it = keyAliases().find(name); it != keyAliases().end()) {
        name = it->second;
    }

    const bool isFunction = name.size() >= 2 && name[0] == 'f' &&
                            std::all_of(name.begin() + 1, name.end(),
                                        [](unsigned char c) { return std::isdigit(c); });
    const bool isNamed = keyLabels().count(name) > 0;
    if (name.size() != 1 && !isFunction && !isNamed) return std::nullopt;

    chord.key = name;
    return chord;
}

std::optional<KeyChord> KeyChord::parse(const std::string& spec) {
    const auto comma = spec.find(',');
    if (comma != std::string::npos) return parse(spec.substr(0, comma), spec.substr(comma + 1));

    // "ctrl+space", "Ctrl-Shift-K", "C-a". Peel modifiers off the front while
    // what's left still has a key in it, so "ctrl++" and "-" stay keys.
    static const std::map<std::string, std::string> shortMods = {
        {"c", "ctrl"}, {"m", "alt"}, {"s", "shift"},
    };
    std::string mods;
    std::string rest = trim(spec);
    for (;;) {
        const auto sep = rest.find_first_of("+-");
        if (sep == std::string::npos || sep == 0 || sep + 1 >= rest.size()) break;
        std::string name = lower(rest.substr(0, sep));
        if (const auto it = shortMods.find(name); it != shortMods.end()) name = it->second;
        if (!parse(name, "x")) break; // not a modifier, so the separator is part of the key
        mods += name + " ";
        rest = rest.substr(sep + 1);
    }
    return parse(mods, rest);
}

std::string KeyChord::describe() const {
    std::string out;
    if (mods & ModLeader) out += "Leader ";
    if (mods & ModCtrl) out += "Ctrl+";
    if (mods & ModAlt) out += "Alt+";
    if (mods & ModShift) out += "Shift+";
    if (mods & ModSuper) out += "Cmd+";

    if (const auto it = keyLabels().find(key); it != keyLabels().end()) return out + it->second;
    if (key.size() == 1) return out + static_cast<char>(std::toupper(static_cast<unsigned char>(key[0])));

    std::string name = key;
    if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    return out + name;
}

std::optional<std::vector<KeyChord>> KeyChord::parseList(const std::string& spec) {
    std::vector<KeyChord> out;
    if (trim(spec).empty()) return out;
    if (const auto one = parse(spec)) return std::vector<KeyChord>{*one};

    std::string token;
    std::istringstream words(spec);
    while (words >> token) {
        // Commas separate as well as spaces do; a lone "," is the comma key.
        std::string item = token;
        while (item.size() > 1 && item.back() == ',') item.pop_back();
        if (item.empty()) continue;
        const auto chord = parse(item);
        if (!chord) return std::nullopt;
        if (std::find(out.begin(), out.end(), *chord) == out.end()) out.push_back(*chord);
    }
    return out;
}

std::string KeyChord::describeList(const std::vector<KeyChord>& chords) {
    std::string out;
    for (std::size_t i = 0; i < chords.size(); ++i) {
        if (i > 0) out += i + 1 == chords.size() ? " or " : ", ";
        out += chords[i].describe();
    }
    return out;
}

std::string KeyChord::spec() const {
    std::string out;
    if (mods & ModLeader) out += "LEADER+";
    if (mods & ModCtrl) out += "CTRL+";
    if (mods & ModAlt) out += "ALT+";
    if (mods & ModShift) out += "SHIFT+";
    if (mods & ModSuper) out += "SUPER+";
    // Punctuation reads back as its name, so the result parses either way.
    for (const auto& [name, symbol] : keyAliases()) {
        if (symbol != key || name.size() < 2 || key.size() != 1) continue;
        std::string upper = name;
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        return out + upper;
    }
    std::string name = key;
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return out + name;
}

std::string Bind::describeAction() const {
    std::string out = action;
    for (const auto& arg : args) out += " " + arg;
    return out;
}

const std::vector<ActionInfo>& knownActions() {
    static const std::vector<ActionInfo> actions = {
        {"command_palette", "Open the fuzzy command palette", false},
        {"focus_terminal",  "Move focus to the terminal", false},
        {"focus_browser",   "Move focus to the file browser", false},
        {"focus_next",      "Cycle focus between panes", false},
        {"toggle_browser",  "Show or hide the file browser", false},
        {"toggle_layout",   "Switch between side-by-side and stacked panes", false},
        {"toggle_hidden",   "Show or hide dotfiles in the browser", false},
        {"browser_back",    "Go back to the last directory", false},
        {"browser_forward", "Go forward again", false},
        {"browser_up",      "Go to the parent directory", false},
        {"browser_filter",  "Filter the listing as you type", false},
        {"browser_sort",    "Cycle the sort order", false},
        {"browser_reverse", "Reverse the sort order", false},
        {"copy_path",       "Copy the current directory to the clipboard", false},
        {"paste_path",      "Go to the path on the clipboard", false},
        {"grow_pane",       "Give the focused pane more room", false},
        {"shrink_pane",     "Give the focused pane less room", false},
        {"new_tab",         "Open another terminal tab", false},
        {"close_tab",       "Close the current terminal tab", false},
        {"next_tab",        "Go to the next terminal tab", false},
        {"prev_tab",        "Go to the previous terminal tab", false},
        {"scroll_up",       "Scroll the terminal back", false},
        {"scroll_down",     "Scroll the terminal forward", false},
        {"scroll_top",      "Jump to the top of the scrollback", false},
        {"scroll_bottom",   "Jump back to the prompt", false},
        {"prev_prompt",     "Jump to the previous command's output", false},
        {"next_prompt",     "Jump to the next command's output", false},
        {"copy",            "Copy the selection", false},
        {"paste",           "Paste the clipboard into the terminal", false},
        {"clear",           "Clear the terminal", false},
        {"search",          "Search the scrollback", false},
        {"open_config",     "Open the configuration editor", false},
        {"edit_config",     "Open apollo.conf in your editor", false},
        {"setup",           "Run the setup wizard again", false},
        {"reload_config",   "Re-read ~/.apollo/apollo.conf", false},
        {"help",            "Show the key and command reference", false},
        {"connect",         "Connect to an SSH destination", true},
        {"disconnect",      "Return to the local machine", false},
        {"open_with",       "Choose what opens the selected file", false},
        {"transfer",        "Send the selected file to the other machine", false},
        {"run",             "Run an Apollo command by name", true},
        {"exec",            "Run a shell command in the terminal", true},
        {"cd",              "Change the working directory", true},
        {"quit",            "Leave Apollo", false},
    };
    return actions;
}

const ActionInfo* findAction(const std::string& name) {
    for (const auto& action : knownActions()) {
        if (action.name == name) return &action;
    }
    return nullptr;
}

// --- decoding what the terminal actually sends -----------------------------

namespace {

// xterm reports modifiers as 1 + a bitmask.
std::uint8_t modsFromParam(int param) {
    const int bits = param - 1;
    std::uint8_t mods = ModNone;
    if (bits & 1) mods |= ModShift;
    if (bits & 2) mods |= ModAlt;
    if (bits & 4) mods |= ModCtrl;
    if (bits & 8) mods |= ModSuper;
    return mods;
}

std::string finalKeyName(char final) {
    switch (final) {
        case 'A': return "up";
        case 'B': return "down";
        case 'C': return "right";
        case 'D': return "left";
        case 'H': return "home";
        case 'F': return "end";
        case 'P': return "f1";
        case 'Q': return "f2";
        case 'R': return "f3";
        case 'S': return "f4";
        default:  return "";
    }
}

std::string tildeKeyName(int code) {
    switch (code) {
        case 1: case 7: return "home";
        case 2:  return "insert";
        case 3:  return "delete";
        case 4: case 8: return "end";
        case 5:  return "pageup";
        case 6:  return "pagedown";
        case 15: return "f5";
        case 17: return "f6";
        case 18: return "f7";
        case 19: return "f8";
        case 20: return "f9";
        case 21: return "f10";
        case 23: return "f11";
        case 24: return "f12";
        default: return "";
    }
}

std::vector<int> csiParams(const std::string& body) {
    std::vector<int> params;
    int current = -1;
    for (const char c : body) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            current = (current < 0 ? 0 : current) * 10 + (c - '0');
        } else if (c == ';') {
            params.push_back(current);
            current = -1;
        }
    }
    params.push_back(current);
    return params;
}

} // namespace

KeyChord decodeKey(const std::string& bytes) {
    KeyChord chord;
    if (bytes.empty()) return chord;

    const unsigned char first = static_cast<unsigned char>(bytes[0]);

    if (bytes.size() == 1) {
        if (first == 0)   { chord.mods = ModCtrl; chord.key = "space"; return chord; }
        if (first == 9)   { chord.key = "tab"; return chord; }
        if (first == 13 || first == 10) { chord.key = "enter"; return chord; }
        if (first == 27)  { chord.key = "escape"; return chord; }
        if (first == 127) { chord.key = "backspace"; return chord; }
        if (first == 32)  { chord.key = "space"; return chord; }
        if (first < 27) {  // Ctrl-A .. Ctrl-Z, minus the three handled above
            chord.mods = ModCtrl;
            chord.key = std::string(1, static_cast<char>('a' + first - 1));
            return chord;
        }
        if (first < 32) {  // Ctrl-[ \ ] ^ _
            chord.mods = ModCtrl;
            chord.key = std::string(1, static_cast<char>(first + 64));
            chord.key = lower(chord.key);
            return chord;
        }
        chord.key = lower(bytes);
        return chord;
    }

    if (first != 27) {
        // A multi-byte UTF-8 character.
        return chord;
    }

    if (bytes.size() == 3 && bytes[1] == 'O') {
        chord.key = finalKeyName(bytes[2]);
        return chord;
    }

    if (bytes[1] == '[') {
        const char final = bytes.back();
        const std::string body = bytes.substr(2, bytes.size() - 3);

        if (final == 'Z') { chord.mods = ModShift; chord.key = "tab"; return chord; }

        const std::vector<int> params = csiParams(body);
        if (final == '~') {
            chord.key = tildeKeyName(params.empty() || params[0] < 0 ? 0 : params[0]);
            if (params.size() > 1 && params[1] > 0) chord.mods = modsFromParam(params[1]);
            return chord;
        }
        chord.key = finalKeyName(final);
        if (params.size() > 1 && params[1] > 0) chord.mods = modsFromParam(params[1]);
        return chord;
    }

    if (bytes.size() == 2) {
        const KeyChord inner = decodeKey(bytes.substr(1));
        if (inner.empty()) return chord;
        chord = inner;
        chord.mods |= ModAlt;
        return chord;
    }

    return chord;
}

} // namespace apollo
