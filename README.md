# Apollo

A desktop shell for your workspace, written in C++17 with SFML 3.

Apollo puts a **file browser** and a **terminal** side by side in one window. You
navigate by double-clicking folders in the GUI or by typing commands — both drive
the same working directory, so the two views never disagree. The same session can
be pointed at a remote machine over SSH, at which point the browser lists the
remote directory and commands execute there.

## Install

Requires macOS and SFML 3.

```bash
brew install sfml && ./install.sh
```

The installer checks your toolchain, builds, and puts `apollo` on your `PATH`.
To install somewhere that needs no `sudo`:

```bash
PREFIX=~/.local ./install.sh
```

Then, from anywhere:

```bash
apollo
```

The first launch runs a short setup wizard — it asks for a password, which
directory to open, and optionally one SSH connection. Everything it asks can be
changed later with `apollo config`.

```bash
apollo doctor
```

checks that the assets, config, connections and `ssh` are all in order.

## Command line

| Command | Effect |
| --- | --- |
| `apollo` | Launch the app |
| `apollo setup` | Re-run the setup wizard |
| `apollo config ...` | View and edit configuration (see below) |
| `apollo doctor` | Verify the installation |
| `apollo --version` / `--help` | Version, usage |

## Configuration

Settings live in `~/.apollo/config.properties`, created with `0600` permissions
because it can hold SSH passwords. Edit it with `apollo config` from your shell
or from inside Apollo — the two share one implementation.

```bash
apollo config list                      # every setting, passwords masked
apollo config get <key>
apollo config set <key> <value>
apollo config unset <key>
apollo config path                      # where the file lives
apollo config edit                      # open it in $EDITOR
```

### Connections

Apollo can hold any number of SSH destinations, each under a short name.

```bash
apollo config add lab alice@10.0.0.5
apollo config add pi pi@raspberry.local 2222
apollo config set connection.lab.key ~/.ssh/id_ed25519
apollo config connections
apollo config default lab
apollo config remove pi
```

`apollo connect` picks a destination like this:

- **One connection configured** — it is used, no name needed.
- **Several configured** — you must name one (`apollo connect lab`), unless
  `apollo.defaultConnection` is set, in which case that one is used.
- **A name you give always wins** over the default.

Prefer `connection.<name>.key` over `connection.<name>.password`: `sshpass`
puts a password in the process table, where any user on the machine can read it
with `ps`.

### Keys

| Key | Meaning |
| --- | --- |
| `apollo.password` | Password for the Apollo boot screen |
| `apollo.root` | Directory Apollo opens in |
| `apollo.defaultConnection` | Used by a bare `apollo connect` |
| `connection.<name>.host` | Hostname or IP |
| `connection.<name>.user` | Remote username |
| `connection.<name>.key` | Private key path (preferred) |
| `connection.<name>.password` | Password, if no key is set |
| `connection.<name>.port` | SSH port (default 22) |
| `connection.<name>.remoteDir` | Directory to open remotely (default `~/APOLLO`) |

A pre-0.2 `apollo.properties` in the source tree is migrated automatically the
first time Apollo runs; its single host becomes the connection named `default`.

## Commands inside Apollo

Anything not listed below is passed to the shell — locally, or on the remote host
when connected.

- `apollo` — return to the Apollo root
- `apollo help` — the in-app command list
- `apollo connect [name]` / `apollo disconnect`
- `apollo connections` — list configured destinations
- `apollo config ...` — same subcommands as the CLI
- `apollo setup` — re-run the wizard
- `apollo term` / `apollo termk` — open Terminal.app here (`termk` also quits)
- `apollo save` — upload files edited from the remote cache
- `apollo clear`, `apollo p++` / `apollo p--`, `apollo exit`
- `open <file>` — open a file, fetching it over SCP first when remote
- `cd <dir>` — change directory (mirrored in the GUI)

## Terminal keys

| Key | Action |
| --- | --- |
| `←` / `→` | Move the cursor |
| `Opt+←` / `Opt+→` | Move by word |
| `Home` / `End`, `Ctrl+A` / `Ctrl+E` | Start / end of line |
| `↑` / `↓` | Previous / next command in history |
| `Ctrl+K` / `Ctrl+U` | Delete to end / start of line |
| `Ctrl+W` | Delete the previous word |
| `Ctrl+C` | Cancel the running command, or clear the line |
| `Ctrl+L` | Clear the scrollback |
| `Ctrl+D` | Quit (on an empty line) |
| Scroll wheel, `PgUp` / `PgDn` | Scroll the scrollback (10k lines) |
| `Tab` | Complete a path |

Long-running commands stream their output as it arrives and never block the
window — the prompt shows `APOLLO .` while one is in flight.

## Layout

| File | Role |
| --- | --- |
| `main.cpp` | CLI entry point, window creation, event loop |
| `System.{h,cpp}` | Boot sequence, setup wizard, directory model, GUI drawing |
| `Terminal.{h,cpp}` | Command parsing, async execution, scrollback, terminal pane |
| `Config.{h,cpp}` | Settings and named SSH connections |
| `ConfigCommand.{h,cpp}` | `apollo config` subcommands, shared by CLI and app |
| `RemoteServer.{h,cpp}` | One SSH session, multiplexed over a ControlMaster socket |
| `PropertiesParser.{h,cpp}` | `key=value` reader/writer with atomic saves |
| `AppPaths.{h,cpp}` | Asset and config path resolution |

## Development

```bash
make -j8        # build to ./build/apollo
make run        # build and launch in place
make doctor     # build, then run the installation check
make install    # install to $PREFIX (default /usr/local)
make uninstall  # remove it (leaves ~/.apollo alone)
make clean
```
