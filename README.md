# dlna-server

dlna-server shares your videos, music, and photos with TVs, game consoles, and other DLNA-capable devices on your home network. Point it at a folder or a playlist, and any DLNA player on the same network can find it and start playing — no extra setup needed on the player's side.

On Windows, it runs as a normal app with a tray icon. On Linux and macOS, it runs from the command line, with an optional desktop app too.

## Features

- Works with any standard DLNA player — smart TVs, game consoles, media boxes
- Shows up on your network automatically, so players can find it without you typing in an address
- Tells connected apps right away when you add or remove media, so your library stays up to date
- Lets you skip to any point in a video instead of only playing from the start
- Plays HLS streams (a format often used for live TV and internet radio) as one smooth item, not broken into pieces
- Reads M3U, M3U8, and PLS playlists, including playlists that point to other playlists, and picks up subtitle files listed inside them
- Can stream from FTP servers and web addresses, not just files on your own computer
- Keeps track of the same file across rescans, so players don't lose their place in your library
- Lets you limit access to specific devices or address ranges on your network
- Automatically finds matching subtitles and cover art sitting next to your media files
- On Windows, can set up its own firewall access for you, limited to devices on your own network

## Prerequisites

- CMake 3.20 or newer
- A C++17 compiler — MSVC on Windows, GCC or Clang on Linux/macOS
- libcurl — via vcpkg on Windows, via your system package manager or dev package on POSIX
- FLTK, only if you're building the native Linux/macOS GUI. If it's not already installed, CMake fetches and builds it for you.

## Building

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

On Linux, `cmake --install` also registers desktop and AppStream metadata; `CPack` produces a `.deb` package (`libcurl4` is listed as a runtime dependency).

## Usage

**Windows GUI** — launch `DLNA Server.exe`, add a folder, playlist, or network URL as a source, and click Start. A tray icon keeps it running in the background.

**Windows headless:**

```
DLNA Server.exe --headless
```

**POSIX CLI:**

```
dlna-server --port 8200 --source /path/to/media --source ftp://user:pass@host/media
```

Available flags: `--port`, `--name`, `--uuid`, `--debug`, `--source` (repeatable), `--help`. Sources can be folders, playlist files (`.m3u`, `.m3u8`, `.pls`), or `ftp://`/`ftps://` URLs.

## Configuration

Settings persist to `config.ini` under a `[Settings]` section — server name, port, media sources, IP whitelist, and folder-display options. See [`docs/CONFIGURATION.md`](docs/CONFIGURATION.md) for the full field reference.

## Testing

A pytest-based test suite lives under `tests/`. Run it from the repository root:

```
pytest
```

## Developer documentation

Deeper references live in [`docs/`](docs/):

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — component layout and Windows/POSIX symmetry
- [`docs/BUILD.md`](docs/BUILD.md) — full build matrix and CMake options
- [`docs/HTTP-SERVER.md`](docs/HTTP-SERVER.md) — HTTP endpoints and streaming behavior
- [`docs/SSDP-DISCOVERY.md`](docs/SSDP-DISCOVERY.md) — SSDP/multicast implementation notes
- [`docs/MEDIA-SCANNING.md`](docs/MEDIA-SCANNING.md) — source scanning, playlist parsing, HLS handling
- [`docs/THREADING-AND-CONCURRENCY.md`](docs/THREADING-AND-CONCURRENCY.md) — locking model and thread-safety guarantees
- [`docs/CONFIGURATION.md`](docs/CONFIGURATION.md) — `config.ini` field reference
- [`docs/KNOWN-ISSUES.md`](docs/KNOWN-ISSUES.md) — open defects and scoped-out findings
