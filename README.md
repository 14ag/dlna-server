# WinDLNAServer

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)]()

WinDLNAServer streams local video, audio, and image files to DLNA and UPnP clients on the local network.

On Windows, you get the native Win32 app. On Linux and macOS, you can run `dlna-server` from a terminal or launch a small GUI from the app list.

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

On Debian or Ubuntu, the desktop GUI needs the `python3-tk` package in addition to the compiler and CMake packages.

On Termux, the test setup uses:

```sh
pkg install clang cmake make python
```

## Build and install

### Windows

Build the desktop app from the repository root:

```cmd
cmake -B build
cmake --build build --config Release
```

The executable lands at `build/Release/WinDLNAServer.exe`.

If you want the release binary copied into `output/`, run:

```powershell
.\build-output.ps1
```

### Linux desktop app

Build the POSIX server first. This also prepares the files that the GUI launcher uses.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Install everything into your user profile:

```sh
cmake --install build --prefix "$HOME/.local"
```

That puts these files in place:

- `~/.local/bin/dlna-server`
- `~/.local/bin/dlna-server-gui`
- `~/.local/share/applications/dlna-server.desktop`
- `~/.local/share/icons/hicolor/scalable/apps/dlna-server.svg`

After install, open `DLNA Server` from your desktop app launcher. If it doesn't show up right away, run `dlna-server-gui` from a terminal or sign out and back in.

### macOS app bundle

Build the POSIX server and app bundle:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

CMake creates both outputs:

- `build/dlna-server`
- `build/DLNA Server.app`

Install the app for your user account:

```sh
mkdir -p "$HOME/Applications"
cp -R "build/DLNA Server.app" "$HOME/Applications/"
```

Open `DLNA Server` from Finder, Spotlight, or Launchpad. It's the same server binary, wrapped in a small app bundle.

### Headless Linux/macOS

Use the same build command:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Then run the server directly:

```sh
./build/dlna-server --port 8200 --name "DLNA Server" --source /path/to/media
```

## Usage

### Windows

Run `WinDLNAServer.exe`. Add one or more media folders with the `+` button, then start the server with the play button.

When the main window closes, the app stays in the tray. Use the tray menu to show the window, stop the server, or exit.

### Linux/macOS GUI

Launch `DLNA Server` from the desktop app list on Linux, or open `DLNA Server.app` on macOS. Add one or more media folders, set the server name and port, then press Start.

The GUI uses the same POSIX server binary and writes `config.ini` beside it, so command-line and desktop launches share settings. If startup fails, run `dlna-server-gui` from a terminal; Python or Tkinter errors can't hide there.

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
- Linux user install: `~/.local/bin/config.ini`

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
