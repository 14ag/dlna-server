![Banner](./assets/banner.webp)

# DLNA Server

[![Version](https://img.shields.io/badge/Version-1.7.0-blue.svg)](https://github.com/14ag/dlna-server/releases)
![GitHub top language](https://img.shields.io/github/languages/top/14ag/dlna-server)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey.svg)]()

# dlna-server

Share your videos, music and photos with TVs and media players on your home network.

The app finds your files, then plays them on any device that supports DLNA or UPnP. You do not need to copy files to the device.

## Features

- Works on Windows 10 or later (x64 and x86) and on Linux.
- Has a window with a tray icon. Also runs without a window (`--headless` on Windows, the `dlna-server` command on Linux).
- Shares local folders, single files and playlists. Also shares FTP and FTPS folders, and HTTP or HTTPS playlists.
- Plays `.m3u`, `.m3u8` and `.pls` playlists, and HLS streams.
- Supports subtitle files and album art next to your media.
- Can watch your folders and scan again when files change (optional).
- Lets you limit access to chosen IP addresses or ranges.
- Keeps the same ID for each file between scans and restarts.

## What you need

- Windows 10 or later, or a Linux system.
- A network where your player and your computer can reach each other. They must be on the same local subnet.
- To build from source: CMake 3.20 or newer, a C++17 compiler, and libcurl. See [Development Guide](docs/Development-Guide.md).

## Install

Download a file from the [releases page](https://github.com/14ag/dlna-server/releases).

| System | File | How to install |
|---|---|---|
| Windows | `.zip` | Unzip it. Run `dlna-server.exe`. |
| Debian or Ubuntu | `.deb` | `sudo apt install ./<file>.deb` |
| Any Linux (x86_64) | `.AppImage` | `chmod +x <file>.AppImage`, then run it. |
| Any Linux with Flatpak | `.flatpak` | `flatpak install --user <file>.flatpak` |

## Quick start

With the window:

1. Start the app.
2. Click **Add**. Choose a folder or a file.
3. Click **Start**.
4. On your TV or player, look for a media server with the name of your computer.

On Windows, the first start asks to allow the firewall. Choose **Yes**, then accept the Windows permission prompt.

From the command line:

```bash
# Linux: share two folders, then return to the shell
dlna-server ~/Videos ~/Music

# Stop the running server
dlna-server --kill-server
```

```bat
:: Windows: share two folders without a window
dlna-server.exe --headless D:\Media E:\Video

:: Stop the running server
dlna-server.exe --kill-server
```

Paths you give on the command line are used for this run only. They are not saved.

## Settings

Settings are saved in `config.ini`. You can change most of them in the **Settings** window. The default HTTP port is `8200`. The discovery port is UDP `1900`.

See [Configuration](docs/Configuration.md) for every setting and command-line option.

## Problems?

See [Troubleshooting](docs/Troubleshooting.md). Turn on **Debug log** in Settings and open **Logs** to see what the server is doing.

## Tests

Tests use Python and pytest (see `pytest.ini`). They call the built app, so build it first, then run:

```bash
python -m pytest
```

## Project layout

```
src/         C++ source for the server and both user interfaces
resources/   Icons, fonts, Windows resources, GTK style sheets
packaging/   Linux, Flatpak and macOS packaging files
scripts/     Build, packaging and release scripts
docs/        Documentation
tests/       Python tests
```

## Documentation

Start at [docs/Home.md](docs/Home.md).

## Contributing

Read the [Development Guide](docs/Development-Guide.md). Report bugs and ideas on the [issue tracker](https://github.com/14ag/dlna-server/issues).

## License

MIT
