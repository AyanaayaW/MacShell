# MacShell

**Quickshell for macOS.** Run Linux Quickshell configs (bars, launchers, dashboards, lock screens and full rices) on a
Mac, unchanged. Underneath, MacShell uses native macOS services.

MacShell is a fork of [Quickshell](https://quickshell.outfoxxed.me). Configs written for Quickshell on Linux run as they
are: the `Quickshell.*` modules, the Hyprland IPC, and the Linux commands that configs call (`nmcli`, `wpctl`,
`hyprctl`, `notify-send`, ...) are provided by macOS.

```sh
qs --install yay -S caelestia-shell
qs -c caelestia
```

## Highlights

- **Configs work as on Linux.** MacShell loads `~/.config/quickshell/<name>/shell.qml`, and `qs -c`, `qs -p`,
  `qs ipc call` and `qs kill` work as they do on Linux.
- **Linux install commands work.** Paste a rice's install line after `qs --install`. `pacman`, `yay`, `paru`, `apt`,
  `dnf`, `git clone` and `curl ... | bash` installers are understood:
  - Linux packages become Homebrew formulae and fonts.
  - Quickshell configs and their QML plugins are installed.
  - Background programs written in Go or Rust are built for macOS.
- **Hyprland support.** `~/.config/hypr/hyprland.conf` binds become global shortcuts, and `exec-once` and `env` lines
  run. Workspaces map to macOS Spaces, and the Hyprland sockets and `hyprctl` are served by MacShell.
- **Replaces the macOS UI if you want.** MacShell can hide the Dock and menu bar, reserve space for your bars so
  windows stay out of them, and mirror other apps' notifications into your config.
- **Native services.** Audio comes from CoreAudio, Wi-Fi from CoreWLAN, plus Bluetooth, battery, brightness,
  now-playing media, the clipboard, screen capture, the tray and lock-screen authentication.

Tested with Caelestia, end-4's illogical-impulse, Noctalia and others. Shells that depend on their own Linux-only
background daemon only partly work until that daemon is ported.

## Requirements

- A Mac with Apple silicon, running macOS 13 or later.
- [Homebrew](https://brew.sh), used by `qs --install` for tools and fonts.

## Installing

Build MacShell (see [Building](#building)), then:

1. Move `MacShell.app` to `/Applications`. macOS only grants permissions such as Location (needed for Wi-Fi network
   names) to apps it can verify, and it cannot verify apps inside protected folders like `~/Documents`.
2. Put `qs` on your `PATH`:

   ```sh
   ln -s /Applications/MacShell.app/Contents/MacOS/qs ~/.local/bin/qs
   ```

3. Start a config: `qs -c <name>`. From a terminal it runs in the background, so the terminal can be closed. Use
   `qs log -f` to follow its output and `qs kill` to stop it.

macOS asks for permissions such as Accessibility, Screen Recording, Location and Bluetooth the first time a config uses
them. See [MACOS.md](MACOS.md#permissions).

## Building

```sh
brew install qt cmake ninja pkg-config cli11
cmake -GNinja -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DWAYLAND=OFF -DX11=OFF -DI3=OFF -DCRASH_HANDLER=OFF -DUSE_JEMALLOC=OFF -DNETWORK=OFF -DBLUETOOTH=OFF \
  -DSERVICE_PIPEWIRE=OFF -DSERVICE_STATUS_NOTIFIER=OFF -DSERVICE_MPRIS=OFF -DSERVICE_POLKIT=OFF \
  -DSERVICE_UPOWER=OFF -DSERVICE_GREETD=OFF
cmake --build build
./packaging/macos/bundle.sh
```

This produces `dist/MacShell.app` and a disk image. See [DEVELOPMENT.md](DEVELOPMENT.md) for details.

## Documentation

- [MACOS.md](MACOS.md): MacShell's own documentation:
  - running configs and installing rices;
  - Hyprland support;
  - built-in Linux commands;
  - settings and permissions;
  - differences from Linux.
- [DEVELOPMENT.md](DEVELOPMENT.md): building and developing.
- [Quickshell documentation](https://quickshell.outfoxxed.me/docs): the QML API that configs use.

## Differences from Linux

macOS limits a few things that Linux compositors allow:

- Windows always float.
- Windows can't be moved between Spaces.
- Each Space draws its own wallpaper.
- Compiled QML plugins need a macOS build.

Polkit, greetd and some Wayland-only protocols are not available. The full list is in
[MACOS.md](MACOS.md#differences-from-linux).

## Credits

MacShell is built on [Quickshell](https://quickshell.outfoxxed.me) by outfoxxed and its contributors. It is an
independent fork and is not affiliated with the Quickshell project. Upstream Quickshell is hosted at
[git.outfoxxed.me/quickshell/quickshell](https://git.outfoxxed.me/quickshell/quickshell).

## License

Licensed under the GNU LGPL 3, like Quickshell. See [LICENSE](LICENSE).

Unless you explicitly state otherwise, any contribution submitted for inclusion is licensed as above, without any
additional terms or conditions.
