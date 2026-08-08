![Banner](./assets/banner.webp)

# DLNA Server

[![Version](https://img.shields.io/badge/Version-1.7.0-blue.svg)](https://github.com/14ag/dlna-server/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)]()

dlna-server shares your videos, music, and photos with TVs, game consoles, and other DLNA-capable devices on your home network. Point it at a folder or a playlist, and any DLNA player on the same network can find it and start playing - no extra setup needed on the player's side.

## Features

- Works with any standard DLNA player - smart TVs, game consoles, media boxes
- Shows up on your network automatically, so players can find it without you typing in an address
- Tells connected apps right away when you add or remove media, so your library stays up to date
- Plays HLS streams
- Reads M3U, M3U8, and PLS playlists, including playlists that point to other playlists that point to other playlists that point to other playlists, and picks up subtitle files listed inside them
- Can stream from FTP servers and web addresses
- Keeps track of the same file across rescans, so players don't lose their place in your library
- Lets you limit access to specific devices or address ranges on your network
- Automatically finds matching subtitles and cover art sitting next to your media files


## Install

See [`CHANGELOG.md`](CHANGELOG.md) for the full release history. Prebuilt binaries are available from the [Releases](https://github.com/14ag/dlna-server/releases) page:

- **Windows** - download  the `.exe`  and run it
- **Linux** - run `sudo ./build-assets.sh --install` from repo root
- **Linux CLI-only** - run `sudo ./build-assets.sh --cli --install`

`--install` builds GUI assets by default, writes the `.deb` into `output/linux/`, and installs that package with `dpkg -i` so it appears in the package database. If you want to confirm the install, run `dpkg -L dlna-server | grep desktop`.

The desktop GUI starts by default on every platform. Use the Settings dialog to configure sources, port, and server name. The command-line flags described below override settings for a single session.

## Usage

**Windows** - launch `DLNA Server.exe`, add a folder, playlist, or network URL as a source, and click Start. A tray icon keeps it running in the background. Pass `--headless` to start with tray icon only and no window.

**Linux / macOS desktop** - launch `dlna-server-gui-bin` (AppImage starts it automatically). Use the Settings dialog to configure sources and server options.

**Headless / CLI (all platforms):**

```
DLNA Server.exe --port 8200 --source C:\media
dlna-server --port 8200 --source /path/to/media --source ftp://user:pass@host/media
```

Startup Flags: `--port`, `--name`, `--uuid`, `--debug`, `--source` (repeatable), `--help`, `--kill-server`/`-k` (Windows), `--headless`, `--configure-firewall` (Windows), `--print-scan-concurrency <n>`. Sources can be folders, playlist files (`.m3u`, `.m3u8`, `.pls`), or `ftp://`/`ftps://` URLs. On every platform, any unrecognized argument is treated as a media source path. Providing a source via `--source` forces headless mode regardless of `--headless`.

`--print-scan-concurrency <n>` evaluates the playlist scan concurrency formula for a given number of items and exits - useful for tuning the internal thread budget without running a full scan. `--kill-server` (`-k`) stops a running Windows instance via the named pipe. `--configure-firewall --port <n>` opens the Windows firewall rule and exits.

## Configuration

Settings persist to `config.ini` under a `[Settings]` section - server name, port, media sources, IP whitelist, and folder-display options. See [`docs/CONFIGURATION.md`](docs/CONFIGURATION.md) for the full field reference.

## Building

### Prerequisites

- CMake 3.20 or newer
- A C++17 compiler - MSVC on Windows, GCC or Clang on Linux/macOS
- libcurl - via vcpkg on Windows, via your system package manager or dev package on POSIX
- FLTK, only if you're building the native Linux/macOS GUI. If it's not already installed, CMake fetches and builds it for you.

**Windows** (requires a vcpkg install with the CURL port available):

```
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
cmake --install build
```

**Linux / macOS:**

```
cmake -B build -S .
cmake --build build
sudo cmake --install build
```

The native FLTK GUI (`dlna-server-gui-native`) builds by default on POSIX. Turn it off with `-DDLNA_ENABLE_FLTK_GUI=OFF` if you only want the headless binary.

On Linux, `cmake --install` also registers desktop and AppStream metadata; `CPack` produces a `.deb` package (`libcurl4` is listed as a runtime dependency). For the repo launcher, prefer `./build-assets.sh --install` so install lands in `dpkg` in one step.

## Testing

A pytest-based test suite lives under `tests/`. Run it from the repository root:

```
pytest
```

## Developer documentation

Deeper references live in [`docs/`](docs/):

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) - component layout and Windows/POSIX symmetry
- [`docs/BUILD.md`](docs/BUILD.md) - full build matrix and CMake options
- [`docs/HTTP-SERVER.md`](docs/HTTP-SERVER.md) - HTTP endpoints and streaming behavior
- [`docs/SSDP-DISCOVERY.md`](docs/SSDP-DISCOVERY.md) - SSDP/multicast implementation notes
- [`docs/MEDIA-SCANNING.md`](docs/MEDIA-SCANNING.md) - source scanning, playlist parsing, HLS handling
- [`docs/THREADING-AND-CONCURRENCY.md`](docs/THREADING-AND-CONCURRENCY.md) - locking model and thread-safety guarantees
- [`docs/CONFIGURATION.md`](docs/CONFIGURATION.md) - `config.ini` field reference
- [`docs/KNOWN-ISSUES.md`](docs/KNOWN-ISSUES.md) - open defects and scoped-out findings
