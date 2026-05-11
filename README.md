# WinDLNAServer

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)]()

WinDLNAServer streams local video, audio, and image files to DLNA and UPnP clients on the local network.

The Windows build provides a native Win32 desktop app for managing folders and server settings. The POSIX build provides a headless `dlna-server` target plus a small desktop GUI launcher for Linux and macOS.

## Features

- Streams MP4, MKV, AVI, MOV, MP3, FLAC, JPEG, and PNG files over HTTP.
- Supports byte-range requests so DLNA clients can seek within media files.
- Advertises the server with SSDP multicast `NOTIFY` messages.
- Handles UPnP `ContentDirectory:1` Browse SOAP requests.
- Serves device, ContentDirectory, and ConnectionManager XML descriptions.
- Provides a native Windows UI with tray behavior.
- Provides Linux/macOS desktop launchers with a GUI for media folders, server name, port, start, stop, and logs.
- Supports optional IP whitelisting on Windows and POSIX builds.
- Stores settings in `config.ini` beside the executable.
- Includes smoke tests for Windows, Android VLC reachability, and POSIX-over-SSH detection.

## Prerequisites

### Windows

- Windows 10 version 1903 or newer
- Visual Studio 2022 with the MSVC C++ desktop toolchain
- CMake 3.20 or newer

### Linux, macOS, or Termux

- CMake 3.20 or newer
- A C++17 compiler such as `clang++` or `g++`
- `make` or another CMake-supported build tool
- Python 3 with Tkinter for the optional desktop GUI

On Termux, the test setup uses:

```sh
pkg install clang cmake make python
```

## Build

### Windows

```cmd
cmake -B build
cmake --build build --config Release
```

The Windows executable is written to `build/Release/WinDLNAServer.exe`.

To build and copy the release binary into `output/`, run:

```powershell
.\build-output.ps1
```

### Linux

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The headless executable is written to `build/dlna-server`.

On macOS, CMake also assembles `build/DLNA Server.app`. Copy that app bundle to `/Applications` or `~/Applications` to launch it from Launchpad or Finder.

On Linux, install the app launcher and desktop entry with:

```sh
cmake --install build --prefix "$HOME/.local"
```

After install, `DLNA Server` appears in desktop app launchers that read `~/.local/share/applications`.

## Usage

### Windows

Run `WinDLNAServer.exe`. Add one or more media folders with the `+` button, then start the server with the play button.

When the main window closes, the app stays in the tray. Use the tray menu to show the window, stop the server, or exit.

### Linux/macOS GUI

Launch `DLNA Server` from the desktop app list on Linux, or open `DLNA Server.app` on macOS. Add one or more media folders, set the server name and port, then press Start.

The GUI uses the same POSIX server binary and writes `config.ini` beside it, so command-line and desktop launches share settings.

### Headless Linux/macOS

```sh
./build/dlna-server --port 8200 --name "DLNA Server" --source /path/to/media
```

Useful options:

- `--port <port>` sets the HTTP port.
- `--name <name>` sets the friendly DLNA device name.
- `--uuid <uuid>` sets a persistent device UUID.
- `--debug` enables extra discovery logging.
- `--source <path>` adds a media folder. You can pass it more than once.

## Configuration

Settings are stored in `config.ini` beside the executable:

- Windows release output: `output/config.ini`
- Windows CMake build output: `build/<Config>/config.ini`
- POSIX build output: `build/config.ini`

If `config.ini` is missing, the app creates it on startup with default settings and a generated UUID. On later starts, the app reads the same file to restore previous settings.

Example:

```ini
[Settings]
ServerName=WinDLNA Server
Port=8200
FileServerPort=8201
DebugLog=0
RunOnBoot=0
IPWhiteList=
DeviceUUID=11111111-2222-3333-4444-555555555555
MediaSources=C:\Media|D:\Videos
```

## Testing

Run the Windows build and protocol smoke test:

```powershell
.\build-output.ps1
.\verify-smoke.ps1
```

Run Android reachability checks through ADB:

```powershell
.\verify-android-smoke.ps1
```

Run the POSIX headless build in Termux over SSH and detect it from the Windows computer:

```powershell
.\verify-posix-ssh.ps1
```

`verify-posix-ssh.ps1` reads SSH credentials from `.env`:

```ini
username=your-username
password=your-password
```

The script builds `dlna-server` on the remote device, starts it headless, verifies SSDP `ssdp:alive` multicast from this computer, fetches `description.xml`, checks a unicast SSDP response, and stops the remote server.

## Project Structure

- `src/main.cpp`, `src/mainwindow.cpp`, `src/settingsdlg.cpp` - Windows UI entry points.
- `src/server.cpp`, `src/httpserver.cpp`, `src/ssdp.cpp` - Windows server and discovery implementation.
- `src/posix_main.cpp`, `src/posix_httpserver.cpp`, `src/posix_ssdp.cpp` - headless POSIX implementation.
- `src/posix_gui.py`, `packaging/linux`, `packaging/macos` - Linux/macOS GUI launcher and desktop packaging.
- `src/contentdirectory.cpp` - shared UPnP XML and Browse response generation.
- `src/media_sources.cpp`, `src/posix_media_sources.cpp` - media indexing.
- `verify-smoke.ps1`, `verify-android-smoke.ps1`, `verify-posix-ssh.ps1` - protocol and device smoke tests.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for local development, testing, and pull request guidance.
