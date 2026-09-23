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
 local  ~/Apollo/apollo_project              Space Space commands · F1 keys · Space , config
```

The terminal is a pty with a full escape sequence parser, so `vim`, `htop`,
`less` and `git add -p` work normally. Apollo adds panes, tabs, a command
palette, scrollback search, SSH destinations and file transfer around it.
Every key it takes is behind a leader key, Space by default, so Ctrl-A, Ctrl-C,
Ctrl-K and Ctrl-R still go to your shell.

## Install

```bash
./install.sh
```

Needs a C++17 compiler, CMake and git. FTXUI is fetched and linked into the
binary, so there is no library to install. To install without sudo:

```bash
PREFIX=~/.local ./install.sh
```

Then run `apollo` from anywhere. The first run asks five short questions —
where to open, which theme, which leader key, an optional SSH destination, and
whether to install shell integration — and writes a commented
`~/.apollo/apollo.conf` as you go. Enter moves on, Shift+Tab goes back, Esc
keeps what you've answered and skips the rest. The folder field Tab-completes,
and the SSH step takes `user@host:port` in one line, names the destination for
you and finds your key. `apollo setup` runs it again, and re-running `./install.sh` over an existing
setup asks whether to (`[y/N]`: Enter keeps your settings). A first run that
was interrupted is offered again next time rather than skipped for good.

## Commands

| | |
| --- | --- |
| `apollo` | Open here |
| `apollo ~/src/thing` | Open there |
| `apollo connect [name]` | Open connected over SSH |
| `apollo push <file>... [name:dir]` | Copy files to a destination |
| `apollo pull [name:]<path>... [dir]` | Copy files from one |
| `apollo config` | Settings, in an editor |
| `apollo config <subcommand>` | Settings, from the shell |
| `apollo setup` | Run the wizard again |
| `apollo doctor` | Check the installation |
| `apollo commands` | List the commands you have added |
| `apollo <command>` | Run one of them |
| `apollo completions zsh` | Shell completion |

## Keys

Apollo's keys are behind a leader: press it, let go, then press the key.
Anything not listed goes to the program in the terminal.

The leader is **Space**, and it only acts as the leader where a space would do
nothing:

- in the file browser, unless you're in the middle of typing a name;
- at an empty shell prompt — nothing typed, no program running, no full-screen
  app up. Locally Apollo can see what has the terminal; with the shell
  integration below it also sees each prompt and command, which makes this
  exact everywhere, over SSH included.

Everywhere else Space types a space, and `Ctrl+Space` or `Ctrl+\` is the
leader — inside vim, halfway through a command, anywhere. There are two
because some systems keep Ctrl+Space for switching input languages; where one does,
`Ctrl+\` is the one that gets through. Pressing either one twice sends it to the
program, so `Ctrl+\` `Ctrl+\` is still SIGQUIT. If Space turns out not to be
for Apollo (a key that isn't bound follows it) you get the space and the key,
so nothing you type is lost. Hold the leader for half a second and a list of
what can follow it pops up.

Change it with one line, any spelling you like:

```conf
general {
    leader          = space        # or ctrl+space, ctrl+a, C-b, alt+space, `
    leader_anywhere = ctrl+space, ctrl+backslash   # any keys; empty for none
}
```

`apollo config set leader ctrl+a` does the same from the shell, and in
`apollo config` the Leader key row takes a keypress: select it, press Enter,
press the key you want. A leader that doesn't type anything (Ctrl+A, say) is
the leader everywhere and nothing more.

| Key | |
| --- | --- |
| `Leader Space` | Command palette, fuzzy searched (Space Space, by default) |
| `Leader ,` | Settings editor |
| `Leader B` / `E` / `T` | Toggle the browser, focus it, focus the terminal |
| `Leader C` / `X` / `N` / `O` | New tab, close tab, next, previous |
| `Leader /` | Search the scrollback |
| `Leader ↑` / `↓` | Jump to the previous or next command's output |
| `Leader S` | Side-by-side or stacked |
| `Leader ←` / `→` | Resize the panes |
| `Leader U` / `[` / `]` | Up a directory, back, forward |
| `Leader Y` / `V` | Copy the current path, go to the one on the clipboard |
| `Leader D` | Disconnect: put this tab back on the local machine |
| `Leader A` | Send the selected file to the other machine |
| `Shift+PgUp` / `PgDn` | Scroll back |
| `F1` | Full key reference |
| `F10` | Quit |

Drag to select and the selection is copied. The wheel scrolls, double click
opens, click a tab to switch. The palette, the settings editor and the wizard
all take the mouse too; click a row to pick it, a section tab to move, a
setting twice to change it. Every key is a line in the config, and `unbind`
removes any of them.

Keys that rearrange the panes (show the browser, stack it, resize it, show
dotfiles) write to the config as they go, so the layout comes back next time.

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
then dates. Files are colored and marked by type, and the line underneath
shows permissions, size and mtime for whatever is selected.

`browser.sort` cycles name, size, modified and type; `browser.sort_reverse`
flips it. Both are on the toolbar, in the palette and in the config.

The terminal comes along. Back, forward, up, opening a directory and
`Leader V` all `cd` the shell as well as moving the pane, so the two never
drift apart. The other direction — the pane following a `cd` you type — needs
the shell integration below on this machine, and needs nothing at all on the
far end of a connection.

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
| `apollo connect [remote-name]` | Another tab, connected |
| `apollo disconnect` | Put this tab back on the local machine |

Everything else (`apollo doctor`, `apollo config list`, or your own commands)
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
    workspace    = ~/src
    leader       = space       # the key in front of Apollo's own
    follow_cwd   = true        # the browser follows your shell
    confirm_quit = true        # ask before leaving mid-command
}

decoration {
    theme   = nord             # or apollo, midnight, gruvbox, catppuccin, solarized, paper
    border  = rounded
    gaps    = 1
    animate = true
    boot    = true             # splash on startup
}

colors {
    accent = $accent           # override any theme color
}

browser {
    position    = left         # left or right
    layout      = split        # split, or stacked above the terminal
    show_hidden = false
    git_status  = true
}

bind   = LEADER, G, exec, git status
unbind = LEADER, Q

source = ~/.apollo/themes/shared.conf
```

A sourced file supplies defaults; your own file overrides them wherever the
`source` line sits. `apollo config set` always writes your file.

### Themes

Seven built in (widely selected from inspiration): `apollo`, `midnight`, 
`nord`, `gruvbox`, `catppuccin`, `solarized`, `paper`. Your own is a file 
in `~/.apollo/themes` — a base plus whatever you change:

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
Colors are `#rrggbb`, `#rgb`, `rgb(r, g, b)` or a name. The sixteen ANSI
colors handed to programs are derived from the same palette, so `ls` and
`git diff` match the rest of the window.

`apollo config` (inspired by Claude Code's /config as I think it's effective) 
with no arguments opens an editor with every setting, what it
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
│   Color: Accent        ██ #7aa2f7                                       • │
├────────────────────────────────────────────────────────────────────────────┤
│ Color scheme, built in or a file in ~/.apollo/themes  —  apollo · nord ·…  │
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

## Opening files

Enter or a double click on a file opens it. The first time Apollo sees a file
type it asks what to open it with, offering the editors you actually have
installed and, locally, whatever the desktop uses. The answer is remembered:

```conf
open = md,  vim
open = png, desktop
open = log, less
```

`ask` puts the question back, and so does deleting the line — from the file, or
with `d` on the Open with page of `apollo config`. `Leader Space` → *Choose
what opens the selected file* asks again for one file without changing
anything, and that action can be bound to a key like any other.

Over a connection the file is opened by something running on that machine, so
the choices are the terminal editors; the desktop is not offered, because this
machine cannot open a file on another one.

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
normal one — full screen programs, signals, colors. Connections share a
control socket, so the second one is a round trip rather than a handshake.

The browser follows. While a tab is connected the pane lists the machine that
tab is on, not this one: the same navigation, filtering, sorting and dotfiles,
over the connection that is already open. The title carries a `⇅` and the path
reads `user@host:/path`, which is also what `Leader Y` copies. Switch tabs and
the pane switches machines with you.

`Leader D`, or `apollo disconnect`, ends a connection. A tab opened to hold one
goes with it; set `general.disconnect_closes_tab = false` to leave a local
shell in the tab instead. The last tab is never closed out from under you — it
becomes a local shell either way. The shared ssh master is dropped once the
last tab using it has gone.

The pane follows a `cd` you type over there too, with nothing installed on the
remote: the shell reports its pid on the way in, and the listing round trip
reads that process's directory. Linux answers through `/proc`, other systems
through `lsof`; one that offers neither simply does not follow.

Sizes, dates, permissions and the executable bit come from the remote `ls`.
Git status does not: that would be running git on the wrong machine.

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
    port       = 2222
    remote_dir = ~/work
    jump       = bastion.example.com
}
```

With one destination configured, `apollo connect` and file transfers use it
without being told. With more than one you always say which — `apollo connect
lab`, `apollo push notes.md lab:` — and a bare `apollo connect` lists the names
instead of guessing. There is no default to fall back on: connecting to the
wrong machine, or copying a file onto it, is worse than being asked. (An old
`general.default_connection` line is ignored; `apollo doctor` points it out.)
Inside Apollo, the palette's connect and send actions ask you to pick.

`apollo config add` asks for the port and, when you have no key to point at, a
password. Prefer the key: a password has to go through `sshpass`, and lives in
`~/.apollo/apollo.conf`, which is written `0600`. Apollo passes it through the
environment rather than `argv`, so it stays out of `ps`.

### Moving files

Files go across the same ssh master `apollo connect` already holds open, so a
copy to a machine you're connected to starts without a handshake.

**In the window.** Select a file or directory in the browser and press
`Leader A`, or click the `⇅` button on the browser's toolbar. The direction
follows the pane: from a remote directory it's fetched to this machine, from a
local one it's sent to a destination. With several destinations configured,
you pick which one first, with the one you were last on at the top.

Then two paths, both editable:

```
╭──────────────────────────────────────────────────────────╮
│ Send to lab                            alice@10.0.0.5    │
├──────────────────────────────────────────────────────────┤
│ ▸ Local   ~/src/app/build/app.tar.gz                     │
│           what to send                                   │
│                                                          │
│   Remote  ~/work                                         │
│           a directory on lab, or a new name for it       │
│                                                          │
│ Anything there with the same name is replaced.           │
├──────────────────────────────────────────────────────────┤
│  Enter  send    Tab  other path    Esc  cancel           │
╰──────────────────────────────────────────────────────────╯
```

What you selected fills in what's sent. Where it lands starts as the
destination's `remote_dir` (or home) for an upload, and the local directory
the pane was last in for a download. Change either: send something else, or
land it somewhere else or under a new name.

Enter checks both before anything moves: this machine's side at once, the
other side over the connection. A path that isn't there says so in the form,
which stays open to fix it. Once both check out, the copy runs in the
background with its progress in the status bar, and the pane refreshes when it
lands.

**From the shell.**

```bash
apollo push build/app.tar.gz                   # the only destination: its remote_dir
apollo push notes.md src/ lab:/srv/share       # several, to a named place
apollo pull lab:logs/app.log                   # into the current directory
apollo pull lab:logs/a.log lab:logs/b.log ~/Desktop
```

`name:` picks the destination, and is required when more than one is
configured. Directories are copied whole, dates are kept, and scp's own
progress meter shows the way. Shell completion offers `name:` for each
destination.

## Shell integration

The wizard offers to add one line to your shell startup file. It sources a
snippet that emits OSC 7 and OSC 133, the sequences that report the working
directory and mark where prompts begin and commands start. With it, the browser
follows every `cd` you type, `Leader ↑` / `Leader ↓` jump between previous
commands' output, and a Space leader knows for certain when the prompt is empty
(a REPL waiting for a line never draws a shell prompt, so Space stays a space
there). Each Apollo refreshes the snippet on start, so a newer one's marks reach
shells set up by an older one.

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
| `src/net/RemoteFs` | Listing directories over an open connection, off the UI thread |
| `src/net/Transfer` | Copying files across that connection, for `Leader A` and `push`/`pull` |

Reading is split so the screen has one writer and needs no locking: a thread
waits on the pty and only signals that bytes are ready; the bytes are read and
parsed on the UI thread.

## Developing

```bash
cmake -S . -B build && cmake --build build -j8
./build/apollo_tests
```

The tests cover the parts with no screen attached: the config language, key
decoding, the terminal grid, the escape parser, reflow, connection resolution,
remote listings, transfers, the empty-prompt check behind a Space leader, and
migration.

For the rest, `scripts/drive.py` runs Apollo under a pty of a given size, sends
keystrokes, and prints what it painted:

```bash
python3 scripts/drive.py --cols 96 --rows 24 '<leader>' '<space>' 'theme' -- .
```