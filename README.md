# Apollo

A desktop shell for the `~/Apollo` workspace, written in C++17 with SFML 3.

Apollo puts a **file browser** and a **terminal** side by side in one window. You
navigate by double-clicking folders in the GUI or by typing commands — both drive
the same working directory, so the two views never disagree. The same session can
be pointed at a remote machine over SSH, at which point the browser lists the
remote directory and commands execute there.

## Layout

| File | Role |
| --- | --- |
| `main.cpp` | Window creation, event loop, keyboard + double-click dispatch |
| `System.{h,cpp}` | Boot sequence, directory model, GUI drawing (root grid / list view) |
| `Terminal.{h,cpp}` | Command parsing, execution, scrollback, autocomplete, terminal pane |
| `RemoteServer.{h,cpp}` | SSH session state and remote command execution |
| `PropertiesParser.{h,cpp}` | Minimal `key=value` config reader |
| `assets/` | Font and sprites (Apollo head, columns, folder icon) |

## Built-in commands

Anything not listed below is passed through to the shell (locally, or to the
remote host when connected).

- `apollo` — return to the Apollo root
- `apollo connect` / `apollo disconnect` — attach or detach the SSH session
- `apollo term` / `apollo termk` — open a real Terminal.app here (`termk` also quits Apollo)
- `apollo save` — upload files edited from the remote cache back to the host
- `apollo clear` — clear the scrollback
- `apollo p++` / `apollo p--` — page through the file list
- `apollo exit` — quit
- `open <file>` — open a file locally, or fetch it over SCP and open it
- `cd <dir>` — change directory (mirrored in the GUI)

Tab autocompletes path arguments.

## Configuration

```bash
cp apollo.properties.example apollo.properties
```

Then fill in your SSH host and the boot password. `apollo.properties` is
git-ignored — **do not commit real credentials**.

## Build

Requires SFML 3 (`brew install sfml`).

```bash
./compile.sh
```

Installs to `/usr/local/bin/launchApollo`.
