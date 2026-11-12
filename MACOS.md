# MacShell: Quickshell on macOS

MacShell is Quickshell for macOS. Put a Quickshell config where you would on Linux and it runs, using native macOS
services underneath. Nothing else needs installing.

## Running a config

| Linux | MacShell |
|---|---|
| `~/.config/quickshell/shell.qml` | same path; loaded when MacShell.app starts |
| `~/.config/quickshell/<name>/shell.qml` | `qs -c <name>` |
| `qs -p <folder>` | same |
| `qs ipc call ...` | same |

The `qs` and `quickshell` commands live inside the app at `MacShell.app/Contents/MacOS/`. Link them into your PATH, for
example `ln -s /Applications/MacShell.app/Contents/MacOS/qs ~/.local/bin/qs`.

QML plugins that are built for macOS go in `~/Library/Application Support/MacShell/qml`.

## Installing what a rice asks for

Paste the Linux install command from a rice's README after `qs --install`:

```sh
qs --install sudo pacman -S --needed fish btop ttf-rubik-vf
qs --install yay -S caelestia-shell
qs --install "git clone https://github.com/someone/dots && cd dots && ./install.sh"
```

`pacman`, `yay`, `paru`, `apt`, `dnf`, `zypper`, `git clone`, `curl ... | bash` installers hosted on GitHub and plain
GitHub links are understood. Each package is matched to:

| Linux package | On macOS |
|---|---|
| Quickshell, Qt, Hyprland, PipeWire, NetworkManager, wl-clipboard, ... | already built into MacShell |
| `ttf-*`, `otf-*`, `*-nerd` fonts | the matching Homebrew font |
| tools with a macOS build in Homebrew | `brew install` |
| AUR packages and git repositories containing a `shell.qml` | copied to `~/.config/quickshell/<name>` |
| anything else | reported as having no macOS equivalent |

When a repository holds several `shell.qml` files, the one with the most QML files is installed. Sibling configs it
imports by name (for example `import stage.services`) are installed next to it, and the configs folder is on the QML
import path. QML modules it imports from elsewhere in the repository are copied to
`~/Library/Application Support/MacShell/qml`, and modules declared in a CMake project are built against Homebrew Qt
(`cmake` and `ninja` required) and relinked to MacShell's own Qt.

Background programs in the repository that the config calls by name are built for macOS too: Go projects with `go`
and Rust projects with `cargo`. They are installed to `~/.local/bin`. When the repository's systemd unit or
`exec-once` line says how to start one, that command is saved to `.macshell-companions` in the config folder; MacShell
starts it with the config, logs it to `~/Library/Logs/MacShell/<name>.log`, and stops it when the shell quits or dies.
A program that fails to build because it relies on Linux-only system features (inotify, eventfd, netlink, ...) is
reported by name, since it needs a macOS port.

The plan is shown before anything installs. `--noconfirm` or `-y` skips the prompt. Linux-only steps such as
`./install.sh` are listed and skipped, and an existing config folder is never overwritten. Wrap the command in quotes
when it contains `&&` or `;`.

## Your Hyprland config

MacShell takes the place of the compositor and reads `~/.config/hypr/hyprland.conf`, including every file it `source`s:

- **`bind` lines** become global shortcuts. All bind flags are understood. Mouse binds and XF86 media keys are skipped,
  because macOS already handles media keys.
- **`exec-once` lines** run when MacShell starts. Lines that would launch Quickshell itself are skipped.
- **`env` lines** set environment variables for everything MacShell starts.

Super maps to ⌥ Option by default, because ⌘ Command shortcuts belong to every Mac app. To change it, set `super_key` in
`settings.conf` (see below).

Extra shortcuts that only make sense on macOS can go in `~/.config/macshell/shortcuts.conf`, using the same `bind` syntax.

### Supported dispatchers

| Works | On macOS |
|---|---|
| `exec`, `workspace` (numbers, `e+1`, `r-1`), `global` | as on Linux |
| `killactive`, `closewindow` | closes the window, not the app |
| `fullscreen` | `1` maximizes; anything else toggles native full screen |
| `movefocus`, `focuswindow`, `cyclenext` | moves focus between windows on the current Space |
| `movewindow`, `swapwindow` | snaps the window to that half of the screen |
| `resizeactive`, `centerwindow` | as on Linux |
| `exit` | quits MacShell |

Tiling-only dispatchers such as `togglefloating`, `pseudo`, `togglesplit` and `pin` are accepted and do nothing, since
macOS windows always float. `movetoworkspace` is not possible: macOS does not let other apps move windows between Spaces.

## Hyprland IPC

MacShell sets `HYPRLAND_INSTANCE_SIGNATURE` and `XDG_RUNTIME_DIR` for everything it starts and serves Hyprland's two
sockets in `$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/`:

- `.socket.sock` answers requests such as `j/clients` and `[[BATCH]]` lists;
- `.socket2.sock` streams events as `name>>data` lines, so `socat` listeners work unchanged.

Workspaces are your macOS Spaces. Monitors, clients, devices and keyboard layouts come from macOS.

## Built-in Linux commands

These run inside MacShell itself and are on the PATH of everything MacShell starts:

| Command | Backed by |
|---|---|
| `hyprctl` | MacShell's Hyprland IPC |
| `notify-send` | Quickshell's notification server |
| `nmcli` | CoreWLAN |
| `wpctl`, `pactl` | CoreAudio |
| `brightnessctl` | built-in display brightness |
| `loginctl`, `systemctl` | lock, log out, sleep, restart, power off |
| `wl-copy`, `wl-paste` (including `--watch`) | the macOS clipboard |
| `cliphist` | clipboard history in `$XDG_CACHE_HOME/cliphist`, skipping copied passwords |
| `grim` | `screencapture` |
| `hyprpicker` | the macOS color sampler |
| `swww img`, `swww query` | the macOS desktop picture |
| `xdg-open` | `open` |

`~/.local/bin`, `/opt/homebrew/bin` and `/usr/local/bin` are added to the PATH, so tools you install yourself are found.
Tools with no macOS build, such as `matugen`, can usually be installed with `cargo install`.

Configs also read Linux system files. MacShell answers `/proc/stat`, `/proc/meminfo`, `/proc/cpuinfo`, `/proc/uptime`,
`/proc/loadavg`, `/proc/version`, `/etc/os-release`, the kernel hostname and release files, the DMI vendor and model, and
the xkb layout list from macOS.

## Settings

`~/.config/macshell/settings.conf`, one `key=value` per line:

| Key | Default | Meaning |
|---|---|---|
| `hide_system_ui` | `false` | hide the Dock and menu bar while MacShell runs, and restore them when it quits |
| `launch_at_login` | `false` | start MacShell when you log in |
| `super_key` | `option` | the Mac key that Hyprland's Super binds use: `option`, `command` or `control` |
| `hyprland_autostart` | `true` | run `exec-once` and `env` lines from `hyprland.conf` |
| `mirror_notifications` | `true` | show other apps' macOS notifications in your config (needs Full Disk Access) |

## Permissions

macOS asks for these the first time a config uses the matching feature. Grant them to MacShell in System Settings →
Privacy & Security:

| Permission | Needed for |
|---|---|
| Accessibility | window lists and titles, closing and moving windows, switching Spaces, exclusive zones, the tray |
| Screen & System Audio Recording | screencopy previews, `grim`, audio visualizers |
| Location Services | Wi-Fi network names |
| Bluetooth | Bluetooth devices |
| Automation | Spotify and Music controls |
| Full Disk Access | mirroring other apps' notifications |

## Differences from Linux

- **Wallpapers slide with Spaces.** Panels on the top and overlay layers stay still during Space switches, but macOS
  draws each Space's own wallpaper above anything beneath your windows. To make a wallpaper look still, turn on
  Accessibility → Display → Reduce motion and give every Space the same picture.
- **Other apps' notifications are mirrored, not intercepted.** With Full Disk Access granted to MacShell, notifications
  that macOS delivers also reach your config's `NotificationServer`, with the app's name and bundle ID. macOS still shows
  its own banner as well; to avoid seeing both, set the app's alert style to "None" in System Settings → Notifications,
  which still records it for MacShell. Set `mirror_notifications=false` to turn mirroring off.
- **Lock screens** verify passwords with macOS. Configs that use a Linux PAM folder with a plain `pam_unix` password
  check are verified against your macOS login; fingerprint and other Linux-only modules are refused.
- **Compiled QML plugins** that ship with a config are built for Linux and need a macOS build before they load.
- **Not available:** Polkit and greetd, Wayland-only protocols such as foreign toplevel export, and moving windows
  between Spaces.
