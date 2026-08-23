# Apollo

A file browser and a real terminal side by side, in one configurable window.

```
╭────────────────────────────────────────────╮ ╭───────────────────────────────────────────╮
│ ~/Apollo/apollo_project                    │ │ zsh                                   vim │
├────────────────────────────────────────────┤ ├───────────────────────────────────────────┤
│ ←  →  ↑ …/apollo_project         name ↓  / │ │# Apollo                                   │
│ ▸ build/                                   │ │                                           │
│ ▸ scripts/                        M        │ │A file browser and a real terminal side by │
│ ▸ src/                            ?        │ │side, in one configurable window.          │
│ ▸ tests/                          M        │ │                                           │
│ ◇ apollo.properties                   167B │ │~                                          │
│ ≡ CMakeLists.txt                  M   3.9K │ │"README.md" 322L, 11838B                   │
├────────────────────────────────────────────┤ │                                           │
│ ▸ build                                    │ │                                           │
│ drwxr-xr-x  11:37                          │ │                                           │
╰────────────────────────────────────────────╯ ╰───────────────────────────────────────────╯
 local  ~/Apollo/apollo_project              Leader Space commands · F1 keys · Leader , config
```

The terminal is a pty with a full escape sequence parser, so `vim`, `htop`,
`less` and `git add -p` work normally. Apollo adds panes, tabs, a command
palette, scrollback search and SSH destinations around it. Every key it takes
is behind a leader key, so Ctrl-A, Ctrl-C, Ctrl-K and Ctrl-R still go to your
shell.

## Install

```bash
./install.sh
```

Needs a C++17 compiler, CMake and git. FTXUI is fetched and linked into the
binary, so there is no library to install. To install without sudo:

```bash
PREFIX=~/.local ./install.sh
```

Then run `apollo` from anywhere. The first run asks where to open, which theme,
an optional SSH destination, and whether to install shell integration, then
writes a commented `~/.apollo/apollo.conf`. All of it can be changed later.

## Commands

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
| `apollo completions zsh` | Shell completion |

## Keys

Apollo's keys are behind a leader, `Ctrl+Space` by default: press it, let go,
then press the key. Anything not listed goes to the program in the terminal.

| Key | |
| --- | --- |
| `Leader Space` | Command palette, fuzzy searched |
| `Leader ,` | Settings editor |
| `Leader B` / `E` / `T` | Toggle the browser, focus it, focus the terminal |
| `Leader C` / `X` / `N` / `O` | New tab, close tab, next, previous |
| `Leader /` | Search the scrollback |
| `Leader ↑` / `↓` | Jump to the previous or next command's output |
| `Leader S` | Side-by-side or stacked |
| `Leader ←` / `→` | Resize the panes |
| `Leader U` / `[` / `]` | Up a directory, back, forward |
| `Leader Y` / `V` | Copy the current path, go to the one on the clipboard |
| `Shift+PgUp` / `PgDn` | Scroll back |
| `F1` | Full key reference |
| `F10` | Quit |

Drag to select and the selection is copied. The wheel scrolls, double click
opens, click a tab to switch. Every key is a line in the config, and `unbind`
removes any of them.

Keys that rearrange the panes — show the browser, stack it, resize it, show
dotfiles — write to the config as they go, so the layout comes back next time.

## The browser

Drag the divider to resize the pane; where it lands is where it stays. The
toolbar has back, forward and up, the path (click to copy), the sort and the
filter.

Two ways to find something:

- **Type a name.** Letters move the selection to the first match and keep
  moving it as you type. A pause starts a new search.
- **Press `/`.** Narrows the list, fuzzily. The status bar says how many
  entries are left.

The columns depend on the width: names when narrow, then sizes and git status,
then dates. Files are coloured and marked by type, and the line underneath
shows permissions, size and mtime for whatever is selected.

`browser.sort` cycles name, size, modified and type; `browser.sort_reverse`
flips it. Both are on the toolbar, in the palette and in the config.

## Apollo inside Apollo

Running `apollo` in Apollo's own terminal talks to the instance that is already
there, over a socket each instance creates for the shells it starts:

| | |
| --- | --- |
| `apollo` | Back to the workspace, both panes |
| `apollo <directory>` | Take both panes there |
| `apollo quit` | Quit |
| `apollo config` | Open the settings |
| `apollo new-tab` | Another terminal tab |
| `apollo connect lab` | Another tab, connected |

Everything else — `apollo doctor`, `apollo config list`, your own commands —
prints in the terminal where you typed it.

The socket lives in `/tmp`, is named for your user and pid, and is owner-only.
Sockets left behind by a killed instance are swept on the next start.

## Config

Everything is in `~/.apollo/apollo.conf`: a sectioned language with variables,
comments and live reload. Save the file and the running Apollo restyles itself.
`apollo config` edits the same file in place and leaves comments, blank lines
and alignment where they are.

```conf
$accent = #7aa2f7

general {
    workspace  = ~/src
    follow_cwd = true          # the browser follows your shell
}

decoration {
    theme   = nord             # or apollo, midnight, gruvbox, catppuccin, solarized, paper
    border  = rounded
    gaps    = 1
    animate = true
    boot    = true             # splash on startup
}

colors {
    accent = $accent           # override any theme colour
}

browser {
    position    = left         # left or right
    layout      = split        # split, or stacked above the terminal
    show_hidden = false
    git_status  = true
}

leader = CTRL, SPACE
bind   = LEADER, G, exec, git status
unbind = LEADER, Q

source = ~/.apollo/themes/shared.conf
```

A sourced file supplies defaults; your own file overrides them wherever the
`source` line sits. `apollo config set` always writes your file.

### Themes

Seven built in: `apollo`, `midnight`, `nord`, `gruvbox`, `catppuccin`,
`solarized`, `paper`. Your own is a file in `~/.apollo/themes` — a base plus
whatever you change:

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

It shows up in the palette and the settings editor next to the built-in ones.
Colours are `#rrggbb`, `#rgb`, `rgb(r, g, b)` or a name. The sixteen ANSI
colours handed to programs are derived from the same palette, so `ls` and
`git diff` match the rest of the window.

`apollo config` with no arguments opens an editor with every setting, what it
does, what it accepts and its default:

```
╭────────────────────────────────────────────────────────────────────────────╮
│ apollo config  ~/.apollo/apollo.conf                                v0.3.0 │
├────────────────────────────────────────────────────────────────────────────┤
│ General  Appearance  Terminal  Browser  Keys  Commands  Connections  About  │
├────────────────────────────────────────────────────────────────────────────┤
│ ▸ Theme                 nord                                             • │
│   Border                rounded                                            │
│   Gaps                  1                                                  │
│   Animate               ● on                                               │
│   Dim inactive pane     ● on                                               │
│   Colour: Accent        ██ #7aa2f7                                       • │
├────────────────────────────────────────────────────────────────────────────┤
│ Colour scheme, built in or a file in ~/.apollo/themes  —  apollo · nord ·…  │
│   decoration.theme   default: apollo   d resets it                         │
├────────────────────────────────────────────────────────────────────────────┤
│  Tab   section   ↑↓   move   Enter   change   e   editor   Esc   close      │
╰────────────────────────────────────────────────────────────────────────────╯
```

A dot marks anything changed from the default.

Or from the shell:

```bash
apollo config list
apollo config set decoration.theme gruvbox
apollo config unset browser.width
apollo config binds
apollo config check          # exits non-zero if anything is wrong
```

Unknown keys, out-of-range values and binds naming an action that does not
exist are reported in `apollo doctor`, in `apollo config check`, and on a
Problems tab that only appears when there is something wrong.

### Completion

```bash
apollo completions zsh > "${fpath[1]}/_apollo"     # or
apollo completions bash > ~/.local/share/bash-completion/completions/apollo
```

The script asks Apollo what could come next, so completion covers setting
names, their values, your themes, your SSH destinations and your own commands
without going stale.

## Adding commands

Both ways show up in `apollo <name>`, in the palette, and as something a key
can be bound to.

In the config:

```conf
command = deploy, ./scripts/deploy.sh, "Ship the current branch"
bind    = LEADER, D, run, deploy
```

Or as a file — anything executable in `~/.apollo/commands`, no configuration
needed. Its first comment line becomes the description:

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

## SSH

`apollo connect` spawns a real `ssh` inside the pty, so a remote session is a
normal one — full screen programs, signals, colours. Connections share a
control socket, so the second one is a round trip rather than a handshake.

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

- One destination configured: it is used, no name needed.
- Several: name one, `apollo connect lab`, unless `general.default_connection`
  is set.
- A name you give always wins.

Prefer a key. A password has to go through `sshpass`; Apollo passes it in the
environment rather than `argv` so it stays out of `ps`, but a key avoids the
question.

## Shell integration

The wizard offers to add one line to your shell startup file. It sources a
snippet that emits OSC 7 and OSC 133, the sequences that report the working
directory and mark where prompts begin. With it, the browser follows every `cd`
you type, and `Leader ↑` / `Leader ↓` jump between previous commands' output.

It is plain shell, harmless in any other terminal, and removed by deleting the
line. Nothing else depends on it.

## Layout

| | |
| --- | --- |
| `src/core/ConfigFile` | The config language: parser, variables, includes, edits that keep comments |
| `src/core/Config` | The typed view, and the schema behind validation, the editor and the CLI |
| `src/core/Keys` | Key chords, the leader, and decoding what the terminal sends |
| `src/core/Commands` | Built-ins, config commands, dropped-in scripts, fuzzy matching |
| `src/core/Process` | `posix_spawn` with argv arrays, so a filename is never a command |
| `src/term/Pty` | `forkpty`, non-blocking reads, window size |
| `src/term/Screen` | The cell grid: scrollback, alternate screen, scroll regions, reflow |
| `src/term/VtParser` | The DEC/ECMA-48 escape sequence state machine |
| `src/term/Session` | One terminal, and the view state of looking at it |
| `src/ui/*` | Panes, palette, settings editor, wizard |
| `src/net/Ssh` | Destinations, multiplexing, keeping secrets out of `argv` |

Reading is split so the screen has one writer and needs no locking: a thread
waits on the pty and only signals that bytes are ready; the bytes are read and
parsed on the UI thread.

## Developing

```bash
cmake -S . -B build && cmake --build build -j8
./build/apollo_tests
```

The tests cover the parts with no screen attached: the config language, key
decoding, the terminal grid, the escape parser, reflow, connection resolution
and migration.

For the rest, `scripts/drive.py` runs Apollo under a pty of a given size, sends
keystrokes, and prints what it painted:

```bash
python3 scripts/drive.py --cols 96 --rows 24 '<leader>' '<space>' 'theme' -- .
```

## Upgrading from 0.2

The first run converts `~/.apollo/config.properties` to the new format and
leaves the old file alone. Connections, the workspace and the default
destination carry across. The 0.1 `apollo.properties`, which held a single
host, becomes the connection named `default`.
