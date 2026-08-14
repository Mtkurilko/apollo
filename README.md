# Apollo

A terminal workspace: a file browser and a real terminal, side by side, in one
window that is entirely yours to configure.

```
 1 zsh  2 lab
╭─ ~/src/apollo ─────────────────╮ ╭─ zsh ──────────────────────────── vim ─╮
│ ▸ build/                       │ │# Apollo                                │
│ ▸ scripts/                   M │ │                                        │
│ ▸ src/                       M │ │A terminal workspace: a file browser and│
│ ▸ tests/                       │ │ a real terminal, side by side.         │
│ · CMakeLists.txt          3.6K │ │~                                       │
│ · README.md               8.1K │ │"README.md" 210L, 8104B                 │
╰────────────────────────────────╯ ╰────────────────────────────────────────╯
 local  ~/src/apollo                        4 dirs, 2 files  Ctrl+Space Space
```

The terminal is a real terminal — a pty and a full escape sequence parser — so
`vim`, `htop`, `less`, `git add -p` and anything else behave exactly as they do
in the terminal you are reading this in. Apollo adds panes, tabs, a command
palette, a searchable scrollback and SSH destinations around it, and then gets
out of the way: **every key it takes for itself is behind a leader key**, so
Ctrl-A, Ctrl-C, Ctrl-K and Ctrl-R still belong to your shell.

## Install

```bash
./install.sh
```

That needs a C++17 compiler, CMake and git, and nothing else — FTXUI is fetched
and linked into the binary, so there is no library to install and no package
version to drift out from under you. To install somewhere that needs no
password:

```bash
PREFIX=~/.local ./install.sh
```

Then, from anywhere:

```bash
apollo
```

The first run walks through a short wizard — where to open, which theme, an
optional SSH destination, and whether to install shell integration. It writes a
commented `~/.apollo/apollo.conf` and everything it asks can be changed later.

## Using it

| | |
| --- | --- |
| `apollo` | Open here |
| `apollo ~/src/thing` | Open there |
| `apollo connect [name]` | Open connected over SSH |
| `apollo config` | Settings, in an editor |
| `apollo config <subcommand>` | Settings, from the shell |
| `apollo setup` | Run the wizard again |
| `apollo doctor` | Check the installation |
| `apollo commands` | List the commands you have added |
| `apollo <command>` | Run one of them |

### Keys

Apollo's own keys live behind a **leader**, `Ctrl+Space` by default: press it,
let go, then press the key. Everything not listed here goes straight to the
program running in the terminal.

| Key | |
| --- | --- |
| `Leader Space` | The command palette — everything Apollo can do, fuzzy searched |
| `Leader ,` | The settings editor |
| `Leader B` / `E` / `T` | Toggle the browser, focus it, focus the terminal |
| `Leader C` / `X` / `N` / `O` | New tab, close tab, next, previous |
| `Leader /` | Search the scrollback |
| `Leader ↑` / `↓` | Jump to the previous or next command's output |
| `Leader S` | Side-by-side or stacked |
| `Leader ←` / `→` | Resize the panes |
| `Shift+PgUp` / `PgDn` | Scroll back |
| `F1` | The full key reference |

Drag to select, and the selection is copied. The wheel scrolls. Double click
opens. Every one of these is a line in the config, and `unbind` removes any of
them.

## Configuring it

Everything lives in `~/.apollo/apollo.conf`, in a small sectioned language with
variables, comments and live reload. Save the file and the running Apollo
restyles itself; `apollo config` edits the same file **in place**, leaving your
comments, blank lines and alignment exactly where you put them.

```conf
$accent = #7aa2f7

general {
    workspace  = ~/src
    follow_cwd = true          # the browser follows your shell
}

decoration {
    theme  = nord              # or apollo, midnight, gruvbox, catppuccin, solarized, paper
    border = rounded
    gaps   = 1
}

colors {
    accent = $accent           # override any theme colour
}

browser {
    position    = left
    show_hidden = false
    git_status  = true
}

leader = CTRL, SPACE
bind   = LEADER, G, exec, git status
unbind = LEADER, Q

source = ~/.apollo/themes/shared.conf
```

### Themes

Seven are built in: `apollo`, `midnight`, `nord`, `gruvbox`, `catppuccin`,
`solarized` and `paper`. Your own is a file in `~/.apollo/themes` — a base to
start from and whatever you want to change:

```conf
# ~/.apollo/themes/sunset.conf
base = midnight

colors {
    accent     = #ff8a5b
    accent_alt = #ffd166
    selection  = #3a2233
}
```

```bash
apollo config set decoration.theme sunset
```

It appears in the command palette and in the settings editor alongside the
built-in ones. Colours are `#rrggbb`, `#rgb`, `rgb(r, g, b)` or a name, and the
sixteen ANSI colours the terminal hands to programs are derived from the same
palette, so `ls` and `git diff` belong to the same picture as the chrome.

`apollo config` on its own opens an editor with every setting, what it does,
what it accepts and what the default was:

```
 apollo config  ~/.apollo/apollo.conf                                  v0.3.0
 General  Appearance  Terminal  Browser  Keys  Commands  Connections  About
 ▸ Theme                 nord                                                •
   Border                rounded
   Gaps                  1
   Animate               ● on
 ─────────────────────────────────────────────────────────────────────────────
 Colour scheme  —  apollo · midnight · nord · gruvbox · catppuccin · solarized
   decoration.theme   default: apollo
```

Or from the shell:

```bash
apollo config list
apollo config set decoration.theme gruvbox
apollo config unset browser.width
apollo config binds
apollo config check          # exits non-zero if anything is wrong; good in CI
```

An unknown key, a value outside its range, a bind naming an action that does
not exist — all of them are reported rather than ignored, in `apollo doctor`,
in `apollo config check`, and on a Problems tab that appears in the editor only
when there is something to say.

## Adding commands

Two ways, both of which show up immediately in `apollo <name>`, in the command
palette, and as something a key can be bound to.

**In the config:**

```conf
command = deploy, ./scripts/deploy.sh, "Ship the current branch"
bind    = LEADER, D, run, deploy
```

**As a file.** Anything executable in `~/.apollo/commands` is a command, with
no configuration at all. Its first comment line becomes its description:

```bash
cat > ~/.apollo/commands/tidy <<'EOF'
#!/bin/sh
# Remove build output and stray editor files
rm -rf build && find . -name '*~' -delete
EOF
chmod +x ~/.apollo/commands/tidy
```

```
$ apollo commands
  tidy            Remove build output and stray editor files   ~/.apollo/commands/tidy
```

## SSH destinations

`apollo connect` spawns a real `ssh` inside the terminal's pty, so a remote
session is exactly as complete as one you would start yourself — full screen
programs, signals, colours and all. Connections are multiplexed over a shared
control socket, so the second one costs a round trip rather than a handshake.

```bash
apollo config add lab alice@10.0.0.5
apollo config set connection.lab.key ~/.ssh/id_ed25519
apollo config connections
```

```conf
connection lab {
    host       = 10.0.0.5
    user       = alice
    key        = ~/.ssh/id_ed25519
    remote_dir = ~/work
    jump       = bastion.example.com
}
```

How `apollo connect` picks one:

- **One destination configured** — it is used, no name needed.
- **Several** — name one, `apollo connect lab`, unless `general.default_connection`
  is set, in which case that one is used.
- **A name you give always wins.**

Prefer a key. A password has to be handed to `sshpass`; Apollo passes it in the
environment rather than in `argv` so it does not show up in `ps`, but a key
avoids the question entirely.

## Shell integration

The wizard offers to add one line to your shell's startup file. It sources a
snippet that emits OSC 7 and OSC 133 — standard sequences that report the
working directory and mark where prompts begin. With it:

- the file browser follows every `cd` you type;
- `Leader ↑` and `Leader ↓` jump between the output of previous commands.

It is plain shell, harmless in any other terminal, and removed by deleting the
line. Nothing else in Apollo depends on it.

## How it is put together

| | |
| --- | --- |
| `src/core/ConfigFile` | The config language: parser, variables, includes, and edits that keep your comments |
| `src/core/Config` | The typed view, and the schema that drives validation, the editor and the CLI |
| `src/core/Keys` | Key chords, the leader, and decoding what the terminal actually sends |
| `src/core/Commands` | Built-ins, config commands, dropped-in scripts, and fuzzy matching |
| `src/core/Process` | `posix_spawn` with argv arrays, so a filename is never a command |
| `src/term/Pty` | `forkpty`, non-blocking reads, window size |
| `src/term/Screen` | The cell grid: scrollback, alternate screen, scroll regions, reflow on resize |
| `src/term/VtParser` | The DEC/ECMA-48 escape sequence state machine |
| `src/term/Session` | One terminal, and the view state of looking at it |
| `src/ui/*` | Panes, palette, settings editor, wizard |
| `src/net/Ssh` | Destinations, multiplexing, and keeping secrets out of `argv` |

Reading is split so the screen has exactly one writer and needs no locking: a
small thread waits on the pty and does nothing but signal that bytes are ready,
and the bytes are read and parsed on the UI thread.

## Developing

```bash
cmake -S . -B build && cmake --build build -j8
./build/apollo_tests
```

The tests cover the parts with no screen attached — the config language, key
decoding, the terminal grid, the escape parser, reflow, connection resolution
and migration — which are exactly the parts that would be miserable to check by
hand.

For the parts that do have a screen, `scripts/drive.py` runs Apollo under a pty
of a given size, sends it keystrokes, and prints what it painted:

```bash
python3 scripts/drive.py --cols 96 --rows 24 '<leader>' '<space>' 'theme' -- .
```

## Upgrading from 0.2

The first run converts `~/.apollo/config.properties` into the new format and
leaves the old file alone. Connections, the workspace and the default
destination all carry across. The 0.1 `apollo.properties`, which held a single
host, becomes the connection named `default`.
