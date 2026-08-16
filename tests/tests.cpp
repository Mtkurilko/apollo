// Apollo's tests. One binary, no framework to install: `ctest` or just run it.
//
// The parts worth testing are the ones with no screen attached — the config
// language, key decoding, the terminal grid and the escape parser — and those
// are exactly the parts that would be miserable to check by hand.

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "core/Commands.h"
#include "core/Config.h"
#include "core/ConfigFile.h"
#include "core/Keys.h"
#include "core/Layout.h"
#include "core/Paths.h"
#include "core/Theme.h"
#include "term/Screen.h"
#include "term/VtParser.h"

namespace {

int checks = 0;
int failures = 0;
std::string group;

void section(const std::string& name) {
    group = name;
    std::cout << "\n" << name << "\n";
}

void check(bool ok, const std::string& what) {
    ++checks;
    if (ok) {
        std::cout << "  ok   " << what << "\n";
        return;
    }
    ++failures;
    std::cout << "  FAIL " << what << "\n";
}

template <typename A, typename B>
void expect(const A& got, const B& want, const std::string& what) {
    ++checks;
    if (got == want) {
        std::cout << "  ok   " << what << "\n";
        return;
    }
    ++failures;
    std::cout << "  FAIL " << what << "\n"
              << "         got:  " << got << "\n"
              << "         want: " << want << "\n";
}

using namespace apollo;
using namespace apollo::term;

// --- config language -------------------------------------------------------

void testConfigFile() {
    section("config language");

    const std::string source = R"(# Apollo
$accent = #7aa2f7

general {
    workspace = ~/work     # where it opens
    follow_cwd = true
}

colors {
    accent = $accent
}

connection lab {
    host = 10.0.0.5
    user = alice
}

connection pi {
    host = raspberry.local
    user = pi
}

bind = CTRL, K, command_palette
bind = LEADER, B, toggle_browser
)";

    ConfigFile file;
    check(file.parse(source), "parses without diagnostics");
    expect(file.get("general.workspace", "?"), std::string("~/work"), "reads a nested value");
    expect(file.get("colors.accent", "?"), std::string("#7aa2f7"), "expands a variable");
    expect(file.get("connection.lab.host", "?"), std::string("10.0.0.5"),
          "reads through a labelled section");
    expect(file.labelled("connection").size(), std::size_t(2), "finds every labelled section");
    expect(file.getAll("bind").size(), std::size_t(2), "collects repeated keys");
    check(!file.get("general.nothing").has_value(), "missing keys report missing");

    // A hash inside a value is a colour, not a comment; a hash after a space is
    // a comment. Both have to survive a rewrite.
    file.set("general.workspace", "~/elsewhere");
    check(file.text().find("# where it opens") != std::string::npos,
          "rewriting a value keeps its trailing comment");
    check(file.text().find("# Apollo") != std::string::npos, "rewriting keeps the header");
    expect(file.get("general.workspace", "?"), std::string("~/elsewhere"), "rewrite took effect");

    file.set("general.editor", "nvim");
    expect(file.get("general.editor", "?"), std::string("nvim"), "inserts a new key in a section");
    // Alignment is checked structurally: the new line's '=' has to land in the
    // same column as the ones already in the section.
    {
        const auto columnOfAssign = [&](const std::string& key) {
            std::istringstream reader(file.text());
            std::string line;
            while (std::getline(reader, line)) {
                const std::string trimmed = ConfigFile::trim(line);
                if (trimmed.rfind(key, 0) != 0) continue;
                if (trimmed.size() > key.size() && trimmed[key.size()] != ' ' &&
                    trimmed[key.size()] != '=') {
                    continue;
                }
                return static_cast<int>(line.find('='));
            }
            return -1;
        };
        expect(columnOfAssign("editor"), columnOfAssign("workspace"),
               "a new key is aligned with its neighbours");
    }

    file.set("terminal.scrollback", "500");
    expect(file.get("terminal.scrollback", "?"), std::string("500"), "creates a missing section");

    file.set("connection.new.host", "example.com");
    expect(file.get("connection.new.host", "?"), std::string("example.com"),
          "creates a labelled section");
    check(file.text().find("connection new {") != std::string::npos,
          "the new labelled section is written in labelled form");

    check(file.unset("general.follow_cwd"), "unset removes a key");
    check(!file.get("general.follow_cwd").has_value(), "the key is gone");

    check(file.removeSection("connection.pi"), "removes a whole section");
    expect(file.labelled("connection").size(), std::size_t(2), "the other sections survive");

    ConfigFile broken;
    broken.parse("general {\n  workspace = ~\n");
    check(!broken.ok(), "an unclosed section is reported");

    ConfigFile stray;
    stray.parse("just some words\n");
    check(!stray.ok(), "a line that is not a setting is reported");

    expect(ConfigFile::split("a, b, \"c, d\"").size(), std::size_t(3), "splitting honours quotes");
    expect(ConfigFile::split("a, b, \"c, d\"")[2], std::string("c, d"), "quotes are stripped");
    check(ConfigFile::asBool("yes", false), "yes is true");
    check(!ConfigFile::asBool("off", true), "off is false");
    expect(ConfigFile::asInt("42", 0), 42, "reads a number");
}

// --- colours ---------------------------------------------------------------

void testTheme() {
    section("colours");

    check(Rgb::parse("#7aa2f7") == Rgb{122, 162, 247}, "parses six digit hex");
    check(Rgb::parse("#abc") == Rgb{170, 187, 204}, "parses three digit hex");
    check(Rgb::parse("rgb(1, 2, 3)") == Rgb{1, 2, 3}, "parses rgb()");
    check(Rgb::parse("teal").has_value(), "parses a colour name");
    check(!Rgb::parse("#xyz").has_value(), "rejects nonsense");
    expect(Rgb{122, 162, 247}.hex(), std::string("#7aa2f7"), "round trips to hex");

    for (const auto& name : Theme::builtinNames()) {
        check(Theme::builtin(name).has_value(), "theme '" + name + "' exists");
    }
    check(!Theme::builtin("nope").has_value(), "an unknown theme is not invented");

    const Theme t = *Theme::builtin("nord");
    check(t.ansi[1] != Rgb{}, "the ansi ramp is filled in");
}

// --- keys ------------------------------------------------------------------

void testKeys() {
    section("keys");

    const auto ctrlK = KeyChord::parse("CTRL", "K");
    check(ctrlK.has_value() && ctrlK->mods == ModCtrl && ctrlK->key == "k", "parses CTRL, K");
    expect(ctrlK->describe(), std::string("Ctrl+K"), "describes itself");

    const auto leader = KeyChord::parse("LEADER, SPACE");
    check(leader.has_value() && (leader->mods & ModLeader), "parses a leader chord");

    check(KeyChord::parse("CTRL SHIFT", "F5").has_value(), "parses two modifiers and a function key");
    check(KeyChord::parse("", "COMMA").has_value(), "punctuation can be spelled out");
    expect(KeyChord::parse("", "COMMA")->key, std::string(","), "comma normalises to the character");
    check(!KeyChord::parse("HYPER", "K").has_value(), "an unknown modifier is rejected");
    check(!KeyChord::parse("CTRL", "wobble").has_value(), "an unknown key is rejected");

    expect(decodeKey("\x01").describe(), std::string("Ctrl+A"), "decodes Ctrl-A");
    expect(decodeKey(std::string(1, '\0')).describe(), std::string("Ctrl+Space"),
          "decodes Ctrl-Space, the default leader");
    expect(decodeKey("\x1B[A").key, std::string("up"), "decodes an arrow");
    expect(decodeKey("\x1BOA").key, std::string("up"), "decodes an application mode arrow");
    expect(decodeKey("\x1B[5~").key, std::string("pageup"), "decodes page up");

    const KeyChord shiftPageUp = decodeKey("\x1B[5;2~");
    check(shiftPageUp.key == "pageup" && (shiftPageUp.mods & ModShift), "decodes Shift+PageUp");

    const KeyChord ctrlRight = decodeKey("\x1B[1;5C");
    check(ctrlRight.key == "right" && (ctrlRight.mods & ModCtrl), "decodes Ctrl+Right");

    const KeyChord altB = decodeKey("\x1B" "b");
    check(altB.key == "b" && (altB.mods & ModAlt), "decodes Alt+B");
    expect(decodeKey("\x7F").key, std::string("backspace"), "decodes backspace");
    expect(decodeKey("\x1B[Z").describe(), std::string("Shift+Tab"), "decodes shift tab");

    check(findAction("command_palette") != nullptr, "known actions are findable");
    check(findAction("nonsense") == nullptr, "unknown actions are not");
}

// --- fuzzy matching --------------------------------------------------------

void testFuzzy() {
    section("fuzzy matching");

    check(fuzzy::score("command_palette", "cp").has_value(), "matches a subsequence");
    check(!fuzzy::score("command_palette", "zz").has_value(), "rejects a non-match");
    check(*fuzzy::score("run", "run") > *fuzzy::score("run-everything", "run"),
          "prefers the shorter name");
    check(*fuzzy::score("toggle_browser", "tb") > *fuzzy::score("subtabber", "tb"),
          "prefers word starts");

    std::vector<int> where;
    fuzzy::score("deploy", "dpy", &where);
    expect(where.size(), std::size_t(3), "reports where it matched");

    CommandRegistry registry;
    registry.addBuiltin("connect", "Connect to a host");
    registry.addBuiltin("config", "Edit the configuration");
    check(registry.addDeclared("deploy", "./deploy.sh", "Ship it", "test"), "declares a command");
    check(!registry.addDeclared("config", "x", "", "test"), "will not shadow a built-in");
    expect(registry.search("").size(), std::size_t(3), "an empty query lists everything");
    expect(registry.search("dep").front().command->name, std::string("deploy"), "finds by name");
    check(registry.find("deploy") != nullptr, "looks up by exact name");
}

// --- the terminal grid -----------------------------------------------------

std::string rowText(const Screen& screen, int y) {
    std::string out;
    const Row& row = screen.row(y);
    for (const auto& cell : row.cells) {
        if (cell.width == 0) continue;
        out.push_back(cell.cp < 128 ? static_cast<char>(cell.cp) : '?');
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

void write(VtParser& parser, const std::string& bytes) { parser.feed(bytes); }

void testScreen() {
    section("terminal grid");

    Screen screen(4, 10, 100);
    VtParser parser(screen);

    write(parser, "hello");
    expect(rowText(screen, 0), std::string("hello"), "writes text");
    expect(screen.cursorX(), 5, "advances the cursor");

    write(parser, "\r\nworld");
    expect(rowText(screen, 1), std::string("world"), "carriage return and line feed");

    // Ten columns exactly, then one more: the line has to wrap and be marked
    // as wrapped so a resize can put it back together.
    Screen wrapping(3, 10, 100);
    VtParser wrapParser(wrapping);
    write(wrapParser, "0123456789X");
    expect(rowText(wrapping, 0), std::string("0123456789"), "fills the line to the edge");
    expect(rowText(wrapping, 1), std::string("X"), "wraps to the next line");
    check(wrapping.row(0).wrapped, "marks the line as continued");

    // Scrolling off the top goes to the scrollback.
    Screen scrolling(2, 10, 100);
    VtParser scrollParser(scrolling);
    write(scrollParser, "one\r\ntwo\r\nthree");
    expect(scrolling.historyLines(), 1, "the top line moves to history");
    expect(scrolling.totalLines(), 3, "history and screen address as one run");

    // Erase should keep the current background, which is how programs paint
    // coloured panels.
    Screen erasing(3, 10, 10);
    VtParser eraseParser(erasing);
    write(eraseParser, "\x1B[41m\x1B[2J");
    check(erasing.row(0).at(0).attr.bg == indexedColor(1), "erasing keeps the background colour");

    // SGR.
    Screen styled(3, 20, 10);
    VtParser styleParser(styled);
    write(styleParser, "\x1B[1;31mred\x1B[0m plain");
    check(styled.row(0).at(0).attr.flags & FlagBold, "bold is applied");
    check(styled.row(0).at(0).attr.fg == indexedColor(1), "colour is applied");
    check(styled.row(0).at(4).attr.plain(), "reset clears attributes");

    write(styleParser, "\x1B[38;2;10;20;30mtrue");
    check(styled.attrs().fg == rgbColor(10, 20, 30), "24 bit colour is applied");

    write(styleParser, "\x1B[38;5;200m");
    check(isRgbColor(styled.attrs().fg), "an indexed colour past 15 becomes rgb");

    // Cursor addressing, with an omitted parameter.
    Screen addressed(5, 20, 10);
    VtParser addressParser(addressed);
    write(addressParser, "\x1B[3;7H");
    check(addressed.cursorY() == 2 && addressed.cursorX() == 6, "CUP addresses row and column");
    write(addressParser, "\x1B[;5H");
    check(addressed.cursorY() == 0 && addressed.cursorX() == 4, "an omitted parameter defaults");

    // Alternate screen.
    Screen alternate(3, 10, 100);
    VtParser altParser(alternate);
    write(altParser, "primary\x1B[?1049h");
    check(alternate.alternate(), "enters the alternate screen");
    expect(rowText(alternate, 0), std::string(""), "the alternate screen starts blank");
    write(altParser, "\x1B[?1049l");
    check(!alternate.alternate(), "leaves the alternate screen");
    expect(rowText(alternate, 0), std::string("primary"), "the primary screen is restored");

    // UTF-8, including a split sequence.
    Screen unicode(2, 10, 10);
    VtParser unicodeParser(unicode);
    write(unicodeParser, "\xE2\x9C");
    write(unicodeParser, "\x93");
    expect(unicode.cursorX(), 1, "a code point split across reads still lands as one");

    // Modes and reports.
    Screen modes(4, 10, 10);
    VtParser modeParser(modes);
    write(modeParser, "\x1B[?2004h");
    check(modes.bracketedPaste, "bracketed paste mode is tracked");
    write(modeParser, "\x1B[?1h");
    check(modes.applicationCursor, "application cursor mode is tracked");
    write(modeParser, "\x1B[?25l");
    check(!modes.cursorVisible(), "the cursor can be hidden");

    write(modeParser, "\x1B[6n");
    check(!modeParser.takeReplies().empty(), "a cursor position request is answered");

    // OSC.
    Screen osc(3, 20, 10);
    VtParser oscParser(osc);
    write(oscParser, "\x1B]0;my title\x07");
    expect(osc.title, std::string("my title"), "OSC 0 sets the title");
    write(oscParser, "\x1B]7;file://host/Users/me/a%20dir\x1B\\");
    expect(osc.cwd, std::string("/Users/me/a dir"), "OSC 7 reports the directory, percent decoded");
    write(oscParser, "\x1B]133;A\x07");
    check(!osc.promptLines().empty(), "OSC 133 marks a prompt");

    // Scroll regions.
    Screen region(5, 10, 100);
    VtParser regionParser(region);
    write(regionParser, "\x1B[2;4r");
    write(regionParser, "\x1B[1;1Htop");
    write(regionParser, "\x1B[4;1Hlast\n\nmore");
    expect(rowText(region, 0), std::string("top"), "text outside the region does not scroll");

    // Insert and delete.
    Screen editing(2, 10, 10);
    VtParser editParser(editing);
    write(editParser, "abcdef\x1B[1;1H\x1B[2P");
    expect(rowText(editing, 0), std::string("cdef"), "delete character");
    write(editParser, "\x1B[1;1H\x1B[2@");
    expect(rowText(editing, 0), std::string("  cdef"), "insert character");
}

void testReflow() {
    section("resize");

    Screen screen(3, 10, 100);
    VtParser parser(screen);
    // One logical line, longer than the screen is wide.
    write(parser, "aaaaaaaaaabbbbbbbbbbcc");
    check(screen.row(0).wrapped, "the long line is marked wrapped");

    screen.resize(3, 22);
    expect(screen.cols(), 22, "the width changed");
    // It should now fit on one line again.
    std::string joined;
    for (int i = 0; i < screen.totalLines(); ++i) {
        const Row& row = screen.lineAt(i);
        for (const auto& cell : row.cells) {
            if (cell.width && cell.cp != U' ') joined.push_back(static_cast<char>(cell.cp));
        }
    }
    expect(joined, std::string("aaaaaaaaaabbbbbbbbbbcc"), "reflow keeps every character, in order");

    screen.resize(3, 8);
    joined.clear();
    for (int i = 0; i < screen.totalLines(); ++i) {
        const Row& row = screen.lineAt(i);
        for (const auto& cell : row.cells) {
            if (cell.width && cell.cp != U' ') joined.push_back(static_cast<char>(cell.cp));
        }
    }
    expect(joined, std::string("aaaaaaaaaabbbbbbbbbbcc"), "narrowing keeps every character too");

    // Losing rows and changing width at the same time used to walk the new,
    // smaller row count over the old grid and drop whatever was below it.
    Screen shrinking(6, 12, 100);
    VtParser shrinkParser(shrinking);
    write(shrinkParser, "alpha\r\nbravo\r\ncharlie\r\ndelta\r\necho\r\nfoxtrot");
    shrinking.resize(3, 9);
    std::string kept;
    for (int i = 0; i < shrinking.totalLines(); ++i) {
        const Row& row = shrinking.lineAt(i);
        for (const auto& cell : row.cells) {
            if (cell.width && cell.cp != U' ') kept.push_back(static_cast<char>(cell.cp));
        }
    }
    check(kept.find("alpha") != std::string::npos, "shrinking keeps the oldest line");
    check(kept.find("foxtrot") != std::string::npos, "shrinking keeps the newest line");
    check(kept.find("charlie") != std::string::npos, "and everything between");

    // A resize with the same width must not disturb anything.
    Screen stable(4, 12, 100);
    VtParser stableParser(stable);
    write(stableParser, "one\r\ntwo\r\nthree");
    stable.resize(6, 12);
    expect(rowText(stable, 0), std::string("one"), "growing taller keeps the content");
}

// --- the typed config ------------------------------------------------------

void testConfig() {
    section("settings and connections");

    Config config;
    config.loadText(Config::defaultText());
    check(config.issues().empty(), "the shipped default config is clean");
    if (!config.issues().empty()) {
        for (const auto& issue : config.issues()) std::cout << "         " << issue << "\n";
    }

    expect(config.decoration().theme, std::string("apollo"), "reads the theme");
    check(config.general().followCwd, "reads a boolean");
    check(!config.binds().empty(), "the default binds are present");
    check(config.general().leader.mods == ModCtrl && config.general().leader.key == "space",
          "the leader defaults to Ctrl+Space");

    // Unknown settings are reported rather than ignored: a typo in a config
    // file that silently does nothing is the worst kind.
    Config typo;
    typo.loadText("general {\n    workspac = ~\n}\n");
    check(!typo.issues().empty(), "an unknown setting is reported");

    Config badValue;
    badValue.loadText("decoration {\n    gaps = enormous\n}\n");
    check(!badValue.issues().empty(), "a value of the wrong type is reported");

    Config badBind;
    badBind.loadText("bind = CTRL, K, does_not_exist\n");
    check(!badBind.issues().empty(), "a bind naming an unknown action is reported");

    // Binds: a user bind replaces the default on the same chord.
    Config rebound;
    rebound.loadText("bind = LEADER, B, quit\n");
    bool found = false;
    for (const auto& bind : rebound.binds()) {
        if (bind.chord.key == "b" && (bind.chord.mods & ModLeader)) {
            expect(bind.action, std::string("quit"), "a user bind overrides the default");
            found = true;
        }
    }
    check(found, "the overridden bind is still in the table");

    Config unbound;
    unbound.loadText("unbind = LEADER, B\n");
    for (const auto& bind : unbound.binds()) {
        check(!(bind.chord.key == "b" && (bind.chord.mods & ModLeader)), "unbind removes a default");
    }

    // Connection resolution — one host, several hosts, an explicit name.
    std::string error;

    Config none;
    none.loadText("");
    check(!none.resolveConnection("", error).has_value(), "no connections: nothing resolves");
    check(!error.empty(), "and it says how to add one");

    Config single;
    single.loadText("connection lab {\n  host = h\n  user = u\n}\n");
    check(single.resolveConnection("", error).has_value(), "one connection is used implicitly");

    Config several;
    several.loadText(
        "connection lab {\n  host = h1\n  user = u\n}\n"
        "connection pi {\n  host = h2\n  user = u\n}\n");
    error.clear();
    check(!several.resolveConnection("", error).has_value(),
          "several connections: a bare connect will not guess");
    check(error.find("lab") != std::string::npos && error.find("pi") != std::string::npos,
          "the error lists what is configured");
    check(several.resolveConnection("pi", error)->host == "h2", "an explicit name resolves");
    check(!several.resolveConnection("nope", error).has_value(), "an unknown name does not");

    Config withDefault;
    withDefault.loadText(
        "general {\n  default_connection = pi\n}\n"
        "connection lab {\n  host = h1\n  user = u\n}\n"
        "connection pi {\n  host = h2\n  user = u\n}\n");
    check(withDefault.resolveConnection("", error)->name == "pi", "a default is used when set");
    check(withDefault.resolveConnection("lab", error)->name == "lab",
          "an explicit name still beats the default");

    // Editing.
    Config editable;
    editable.loadText(Config::defaultText());
    check(editable.set("decoration.theme", "nord"), "sets a valid value");
    expect(editable.decoration().theme, std::string("nord"), "the change is visible immediately");
    check(!editable.set("decoration.border", "squiggly"), "refuses a value outside the choices");
    // A theme may also be a file the user wrote, so the name is open — but an
    // unknown one still has to be reported rather than silently ignored.
    check(editable.set("decoration.theme", "chartreuse"), "accepts an open-ended choice");
    check(!editable.issues().empty(), "and reports that it does not exist");
    check(editable.set("decoration.theme", "nord"), "back to a real theme");
    check(!editable.set("general.nonsense", "x"), "refuses an unknown setting");
    check(!editable.set("decoration.gaps", "99"), "refuses a number outside its range");

    Connection conn;
    conn.name = "lab";
    conn.host = "10.0.0.5";
    conn.user = "alice";
    check(editable.addConnection(conn), "adds a connection");
    check(editable.connection("lab") != nullptr, "the connection is readable back");
    expect(editable.general().defaultConnection, std::string("lab"),
          "the first connection becomes the default");
    check(!editable.addConnection(conn), "will not add the same name twice");

    Connection bad;
    bad.name = "no.dots";
    bad.host = "h";
    bad.user = "u";
    check(!editable.addConnection(bad), "rejects a name that would collide with a config path");

    check(editable.removeConnection("lab"), "removes a connection");
    check(editable.connection("lab") == nullptr, "and it is gone");
    check(editable.general().defaultConnection.empty(), "the stale default was cleared too");

    check(editable.addCommand("deploy", "./deploy.sh", "Ship it"), "adds a command");
    check(!editable.commands().empty(), "the command is readable back");
    check(!editable.addCommand("deploy", "x", ""), "will not add the same command twice");

    // Every setting in the schema must be reachable and round trip.
    for (const auto& setting : Config::schema()) {
        if (setting.type == Config::Setting::Type::Color) continue;
        if (setting.defaultValue.empty()) continue;
        Config probe;
        probe.loadText("");
        check(probe.set(setting.path, setting.defaultValue),
              "schema default is accepted: " + setting.path);
    }
}

void testMigration() {
    section("migration from 0.2");

    const auto dir = std::filesystem::temp_directory_path() / "apollo-test-migrate";
    std::filesystem::create_directories(dir);
    const auto file = dir / "config.properties";
    {
        std::ofstream out(file);
        out << "apollo.root=/Users/me/work\n"
               "apollo.defaultConnection=lab\n"
               "connection.lab.host=10.0.0.5\n"
               "connection.lab.user=alice\n"
               "connection.lab.port=2222\n";
    }

    const auto migrated = migrateLegacyConfig(file);
    check(migrated.has_value(), "produces a modern config");

    Config config;
    config.loadText(*migrated);
    expect(config.general().workspace, std::string("/Users/me/work"), "the workspace carried over");
    expect(config.general().defaultConnection, std::string("lab"), "the default carried over");
    check(config.connection("lab") != nullptr, "the connection carried over");
    expect(config.connection("lab")->port, 2222, "including its port");
    check(config.issues().empty(), "and the result is a clean config");

    // A 0.1 file, where there was only ever one host.
    const auto old = dir / "apollo.properties";
    {
        std::ofstream out(old);
        out << "ssh.host=example.com\nssh.user=bob\nssh.password=hunter2\n";
    }
    Config oldConfig;
    oldConfig.loadText(*migrateLegacyConfig(old));
    check(oldConfig.connection("default") != nullptr, "a flat 0.1 host becomes 'default'");

    std::filesystem::remove_all(dir);
}

void testThemeFiles() {
    section("theme files");

    const auto themes = std::filesystem::path(std::getenv("APOLLO_CONFIG_DIR")) / "themes";
    std::filesystem::create_directories(themes);
    {
        std::ofstream out(themes / "sunset.conf");
        out << "base = midnight\n\ncolors {\n    accent = #ff8a5b\n}\n";
    }

    Config config;
    config.loadText("decoration {\n    theme = sunset\n}\n");
    expect(config.theme().name, std::string("sunset"), "a theme file is found by name");
    check(config.theme().accent == Rgb{255, 138, 91}, "its colours are applied");
    check(config.theme().fg == Theme::builtin("midnight")->fg, "the base theme shows through");
    check(config.issues().empty(), "and it loads without complaint");

    const auto available = Config::availableThemes();
    check(std::find(available.begin(), available.end(), "sunset") != available.end(),
          "it is offered alongside the built-in themes");

    Config missing;
    missing.loadText("decoration {\n    theme = nowhere\n}\n");
    check(!missing.issues().empty(), "a theme that is neither built in nor a file is reported");
    expect(missing.theme().name, std::string("apollo"), "and the default is used instead");

    std::filesystem::remove_all(themes);
}

// --- pane geometry ---------------------------------------------------------

void testLayout() {
    section("pane geometry");

    // The invariant that matters: what the terminal is told about its own size
    // must be exactly what will be drawn. A grid one column wider than its pane
    // loses the last column of every row, and it reads as a character going
    // missing at each line wrap.
    // Measured against what compute() says it kept, not against what was asked
    // for: shedding a title bar is a valid answer to a window with no room.
    const auto fits = [](const layout::Request& request, const layout::Panes& panes) {
        const int frameH = panes.border ? 2 : 0;
        const int frameV = (panes.border ? 2 : 0) + (panes.titleBar ? 2 : 0);

        int usedWidth = panes.terminalCols + frameH;
        if (panes.browserShown() && !request.stacked) {
            usedWidth += panes.browserWidth + std::max(0, request.gaps);
        }
        if (usedWidth > std::max(1, request.width)) return false;

        int usedHeight = panes.terminalRows + frameV;
        if (panes.statusBar) usedHeight += 1;
        if (panes.tabBar) usedHeight += 1;
        if (panes.browserShown() && request.stacked) {
            usedHeight += panes.browserRows + frameV + std::max(0, request.gaps);
        }
        return usedHeight <= std::max(1, request.height);
    };

    bool everythingFits = true;
    bool everythingPositive = true;
    for (int width = 1; width <= 200; ++width) {
        for (int height = 1; height <= 60; ++height) {
            for (const bool stacked : {false, true}) {
                for (const char* border : {"rounded", "none"}) {
                    for (const int gaps : {0, 1, 3}) {
                        layout::Request request;
                        request.width = width;
                        request.height = height;
                        request.stacked = stacked;
                        request.border = border;
                        request.titleBar = std::string(border) != "none";
                        request.gaps = gaps;
                        request.tabBar = (width % 3) == 0;

                        const layout::Panes panes = layout::compute(request);
                        if (!fits(request, panes)) {
                            everythingFits = false;
                            std::cout << "         overflow at " << width << "x" << height
                                      << (stacked ? " stacked" : " side by side") << " border="
                                      << border << " gaps=" << gaps << "\n";
                        }
                        if (panes.terminalCols < 1 || panes.terminalRows < 1) {
                            everythingPositive = false;
                        }
                    }
                }
            }
        }
    }
    check(everythingFits, "no size makes a pane larger than the window it is drawn in");
    check(everythingPositive, "and no size produces a pane with no room at all");

    layout::Request wide;
    wide.width = 120;
    wide.height = 40;
    const layout::Panes both = layout::compute(wide);
    check(both.browserShown(), "a big window shows both panes");
    expect(both.browserWidth, 34, "the browser gets the width it asked for");

    layout::Request narrow;
    narrow.width = 44;
    narrow.height = 20;
    check(!layout::compute(narrow).browserShown(),
          "a narrow window drops the browser rather than squeezing both");

    layout::Request short_;
    short_.width = 100;
    short_.height = 12;
    short_.stacked = true;
    check(!layout::compute(short_).browserShown(),
          "and a short one does the same when stacked");

    layout::Request hidden;
    hidden.browserVisible = false;
    check(!layout::compute(hidden).browserShown(), "a hidden browser stays hidden");
    expect(layout::compute(hidden).terminalCols, 78, "and the terminal takes the whole width");
}

void testPaths() {
    section("paths");

    expect(paths::expandUser("~"), paths::home().string(), "~ expands to home");
    expect(paths::expandUser("~/x"), (paths::home() / "x").string(), "~/x expands");
    expect(paths::expandUser("/tmp/x"), std::string("/tmp/x"), "an absolute path is left alone");
    expect(paths::expandUser("~other/x"), std::string("~other/x"), "another user's home is left alone");
    expect(paths::contractUser(paths::home() / "src"), std::string("~/src"), "home contracts back");
}

} // namespace

int main() {
    // Never touch the real ~/.apollo: the tests write themes and read the
    // commands directory, and somebody's actual settings are not a fixture.
    const auto sandbox = std::filesystem::temp_directory_path() / "apollo-tests-home";
    std::filesystem::remove_all(sandbox);
    std::filesystem::create_directories(sandbox);
    ::setenv("APOLLO_CONFIG_DIR", sandbox.c_str(), 1);

    std::cout << "apollo tests\n";

    testPaths();
    testLayout();
    testConfigFile();
    testTheme();
    testKeys();
    testFuzzy();
    testScreen();
    testReflow();
    testConfig();
    testMigration();
    testThemeFiles();

    std::filesystem::remove_all(sandbox);

    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";
    if (failures) std::cout << failures << " FAILED\n";
    return failures == 0 ? 0 : 1;
}
