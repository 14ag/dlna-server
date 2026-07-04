![Banner](./assets/banner.webp)


# dlna-server

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)]()

`dlna-server` streams local video, audio, and image files to DLNA and UPnP clients on the local network.

On Windows, you get the native Win32 app. On Linux, release builds ship a native FLTK GUI plus the headless `dlna-server` binary. On macOS, you can run the POSIX server or use the app-bundle wrapper.

## Features

- Streams MP4, MKV, AVI, MOV, MP3, FLAC, JPEG, and PNG files over HTTP.
- Reads playlist files: `.m3u`, `.m3u8`, and `.pls`.
- Can maintain a default `.m3u` playlist from the desktop settings dialog.
- Reads media from FTP shares such as `ftp://user:pass@server:21/media`.
- Supports byte-range requests so DLNA clients can seek within media files.
- Blocks HTTP and SSDP startup until the initial background media scan completes, then starts serving requests.
- Supports explicit rescans from the server layer; completed scans replace the old index only after a full scan succeeds.
- Watches local media sources while running and rescans after local file changes without a manual restart. Remote sources (`ftp://`, `http://`, `https://`) are only scanned at server start and do not participate in the automatic file watch loop. Adding or removing a remote source at runtime requires a server restart.
- Keeps a `media-cache.tsv` beside `config.ini` for stable media IDs, cached scan errors, and metadata groundwork.
- Advertises the server with SSDP multicast `NOTIFY` messages.
- Handles UPnP `ContentDirectory:1` Browse and Search SOAP requests with exact action dispatch.
- Serves device, ContentDirectory, and ConnectionManager XML descriptions.
- Sends async GENA `SystemUpdateID` updates to subscribed ContentDirectory event callbacks.
- Provides a native Windows UI with tray behavior.
- Provides a native Linux FLTK GUI for media folders, server name, port, start, stop, settings, and logs.
- Shows start/stop busy status and keeps Windows awake while the server is active.
- Advertises bundled DLNA server icons at 48, 120, and 256 px.
- Supports optional IP whitelisting on Windows and POSIX builds.
- Stores settings in `config.ini` beside the executable.
- Includes smoke tests for Windows, Android VLC reachability, and POSIX-over-SSH detection.

## Prerequisites

### Windows

- Windows 10 version 1903 or newer
- Visual Studio 2022 with the MSVC C++ desktop toolchain
- CMake 3.20 or newer

### Linux, macOS

- CMake 3.20 or newer
- A C++17 compiler such as `clang++` or `g++`
- `make` or another CMake-supported build tool
- `libcurl` for FTP, HTTP, and HTTPS media sources
- X11 development headers for native FLTK GUI and AppImage builds

On Debian or Ubuntu, native GUI and AppImage builds need:

```sh
sudo apt install build-essential cmake libx11-dev libxft-dev libxext-dev libxinerama-dev libxcursor-dev libxrender-dev libxfixes-dev libpng-dev libjpeg-dev zlib1g-dev
```

On Termux, the test setup uses:

```sh
pkg install clang cmake make python
```

## Build and install

Use normal CMake commands for local builds. Windows users can also run `install-wsl.ps1` to build and install the native Linux GUI inside WSL.

### Release assets

From Windows, build the release downloads into `output/`:

```bat
build-assets.bat
```

Platform-specific release files are written under `output/<platform>/`. The `output/` root is reserved for the release source zip and release notes/Markdown files.

By default the wrapper requests every supported platform output: `winx64`, `winx86`, `linux`, `macos-x64`, and `macos-arm64`.

- `output/dlna-server-<version>-source.zip`
- `output/winx64/dlna-server-<version>-windows-x86_64.zip`
- `output/winx86/dlna-server-<version>-windows-x86.zip`
- `output/linux/dlna-server_<version>_amd64.deb`
- `output/linux/DLNA_Server-<version>-x86_64.AppImage`
- `output/linux/dlna-server-<version>-linux-x86_64.flatpak`
- `output/macos-x64/DLNA_Server-<version>-macos-x64.dmg`
- `output/macos-arm64/DLNA_Server-<version>-macos-arm64.dmg`

Pass a version, WSL distribution, platform list, or disable platform-folder cleaning when needed:

```bat
build-assets.bat -Version 1.4.0 -WslDistro Ubuntu
build-assets.bat --platform winx64,linux --no-clean
```

Supported platform names are exactly `winx64`, `winx86`, `linux`, `macos-x64`, and `macos-arm64`. Without `--no-clean`, only the selected `output/<platform>/` folders are deleted and recreated before artifacts are copied into them. No build script cleans the `output/` root. The Windows builds use Visual Studio through CMake. The Linux builds run in WSL and write back to `output/linux`. The script downloads pinned FLTK and AppImage packaging inputs outside `output/`, verifies SHA256 hashes, and then hands work to WSL, so `output/` stays release-only.

### Windows

Build the desktop app from the repository root:

```powershell
cmake -S . -B build-windows
cmake --build build-windows --config Release
cmake --install build-windows --config Release
```

The installed executable lands under the active CMake install prefix. If you only build, run `build-windows/Release/DLNA Server.exe`.

For a debug build:

```powershell
cmake -S . -B build-windows
cmake --build build-windows --config Debug
cmake --install build-windows --config Debug
```

### Linux desktop app

Build and install the POSIX server, GUI launcher, desktop entry, app metadata, and icon:

```sh
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux
cmake --install build-linux
```

By default this installs the headless server and native FLTK GUI through the `dlna-server-gui` launcher:

```sh
cmake -S . -B build-linux-native -DCMAKE_BUILD_TYPE=Release -DDLNA_ENABLE_FLTK_GUI=ON
cmake --build build-linux-native
cmake --install build-linux-native
```

To build only the headless POSIX server, configure with `-DDLNA_ENABLE_FLTK_GUI=OFF`. This skips the GUI launcher, desktop entry, app metadata, and icons.

From Windows, install directly into WSL with:

```powershell
.\install-wsl.ps1
```

That builds the native Linux GUI in WSL, installs to `~/.local`, refreshes desktop/icon caches, and smoke-tests the launcher when WSLg is available. Use `-InstallPackages` to let the script install missing Ubuntu build dependencies with `sudo apt-get`.

To install it into your desktop user profile instead, run CMake install with a user prefix after the build:

```sh
cmake --install build_output --prefix "$HOME/.local"
```

That user install writes:

- `~/.local/bin/dlna-server`
- `~/.local/bin/dlna-server-gui`
- `~/.local/share/applications/dlna-server.desktop`
- `~/.local/share/icons/hicolor/scalable/apps/dlna-server.svg`
- `~/.local/share/metainfo/dlna-server.appdata.xml`

Linux desktop menus show the app as **DLNA Server**. The installed command names stay `dlna-server` and `dlna-server-gui` because shell scripts, desktop metadata, AppImage packaging, and automation expect stable no-space binary names.

After install, open **DLNA Server** from your desktop app launcher. If it doesn't show up right away, run `dlna-server-gui` from a terminal or sign out and back in.

On WSLg, `[WARN:COPY MODE]` in the Windows taskbar title is emitted by WSLg, not by this app. It means WSLg is warning about its RDP window transport. The app installs desktop metadata and uses the `dlna-server` launcher so WSLg can match the window to the right icon and command, but hiding that warning requires WSLg configuration or a WSL update.

### Linux desktop downloads

Release builds provide three Linux GUI package formats, all using the native FLTK GUI:

- `.deb` for Ubuntu and Debian desktops. Open it with your software installer, then launch **DLNA Server** from the app menu.
- `.flatpak` for desktops with Flatpak enabled. Install it with your software app or `flatpak install`.
- `.AppImage` for portable use. Mark it executable, then run it directly. AppImageLauncher can add it to your app menu.

Build only the Linux desktop release assets from WSL or Linux:

```sh
bash scripts/build-linux-desktop-assets.sh
```

This writes the `.deb`, AppImage, Flatpak bundle, AppDir, and installed tree under `output/linux/`. From Windows, prefer `build-assets.bat` when you want Windows and Linux downloads in one run.

### Linux AppImage

Build the native GUI install tree first:

```sh
cmake -S . -B build-linux-native -DCMAKE_BUILD_TYPE=Release -DDLNA_ENABLE_FLTK_GUI=ON
cmake --build build-linux-native
cmake --install build-linux-native --prefix "$PWD/output"
```

For an AppImage release, install into a local staging directory, create an AppDir, and run `linuxdeploy`:

```sh
mkdir -p output/linux/dlna-server.AppDir/usr/bin
cp output/linux/install/bin/dlna-server output/linux/dlna-server.AppDir/usr/bin/
cp output/linux/install/bin/dlna-server-gui output/linux/dlna-server.AppDir/usr/bin/
cp output/linux/install/bin/dlna-server-gui-bin output/linux/dlna-server.AppDir/usr/bin/
cp packaging/linux/AppRun output/linux/dlna-server.AppDir/
cp packaging/linux/dlna-server.appimage.desktop output/linux/dlna-server.AppDir/dlna-server.desktop
cp resources/dlna-server.svg output/linux/dlna-server.AppDir/
linuxdeploy --appdir output/linux/dlna-server.AppDir --output appimage
```

This expects `linuxdeploy` on `PATH`.

### macOS app bundle

Build and install the POSIX server and app bundle:

```sh
cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release
cmake --build build-macos
cmake --install build-macos
```

The CMake install step writes:

- `bin/dlna-server`
- `DLNA Server.app`

Install the app for your user account:

```sh
mkdir -p "$HOME/Applications"
cp -R "DLNA Server.app" "$HOME/Applications/"
```

Open **DLNA Server** from Finder, Spotlight, or Launchpad. It's the same server binary, wrapped in a small app bundle.

For direct macOS downloads, build a DMG on macOS:

```sh
bash scripts/build-macos-dmg.sh
```

Set Apple Developer ID and notary environment variables before running the script to produce a Gatekeeper-friendly signed and notarized DMG.

### Headless Linux/macOS

If you only need a local build tree, build without installing:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run the build-tree binary directly:

```sh
./build/dlna-server --port 8200 --name "DLNA Server" --source /path/to/media
```

## Usage

### Windows

Run `DLNA Server.exe`. Add one or more media sources with **Add**, remove selected source entries with **Delete**, then start the server with **Start**.

Sources can be folders, playlist files, or network shares. Playlist files can be `.m3u`, `.m3u8`, or `.pls`. Network shares use URL form:

```text
ftp://user:pass@server:21/media
```

On first start, Windows may ask for firewall access. The app itself stays unelevated. If access is missing, it launches a short-lived elevated helper that creates two inbound rules for this executable: TCP from `LocalSubnet` on any local port, and UDP `1900` from `LocalSubnet` for SSDP discovery. Both rules apply to Domain, Private, and Public profiles.

Changing the HTTP port while the server is running restarts the server so the old listener closes and the new port opens. The Windows TCP firewall rule does not need to change when the port changes.

While the server is starting or stopping, the status line shows `starting server...` or `stopping server...` and toolbar controls are disabled until the operation completes. Windows is kept awake while the server is starting, running, or stopping.

**Delete** removes the selected source from the app only. It never deletes media files, folders, playlists, or network locations from disk.

Settings can create a default playlist entry from a movie path and optional subtitle path. The app writes `default.m3u` beside `config.ini`, emits `#DLNA-SUBTITLE` and VLC-compatible subtitle metadata, and indexes it when **Default playlist** is enabled.

The DLNA device description advertises bundled PNG icons at 48, 120, and 256 px. Clients choose the best `/icons/server_icon_*.png` image from the UPnP `iconList`.

The server answers ContentDirectory `Browse` and `Search` probes plus ConnectionManager `GetProtocolInfo` requests. Browse, HTTP streaming, and ConnectionManager responses use the same DLNA protocol metadata table. Companion `folder.jpg`, `cover.jpg`, `album.jpg`, `thumb.jpg`, `thumb.jpeg`, and same-stem JPG/JPEG/PNG art is advertised as `upnp:albumArtURI` only while the file exists and is served from `/albumart/{id}`.

Settings such as **Flat folders style**, **Show file names instead titles**, **Sort by title instead of file name**, **Proxy streams**, and **Add Artist/Album folders to audio** apply consistently to local folders, playlists, and network shares. Playlist order is preserved unless title sorting is enabled or a DLNA client sends an explicit sort request. When **Proxy streams** is disabled for remote HTTP/FTP entries, Browse can advertise the remote URL directly; otherwise media is proxied through `/media/{id}`.

The advertised ContentDirectory and ConnectionManager event URLs accept UPnP GENA `SUBSCRIBE` and `UNSUBSCRIBE` requests with stable `SID` and timeout headers. ContentDirectory subscribers receive async `SystemUpdateID` `NOTIFY` callbacks after completed media-index swaps.

When the main window closes, the Windows app stays in the tray. Use the tray menu to show the window, stop the server, or exit. Settings, log, add-source, and default-playlist windows are modal child windows: they stay above their owner, close back to that owner, and do not add separate taskbar buttons.

### Linux/macOS GUI

Launch **DLNA Server** from the desktop app list on Linux, or open `dlna-server.app` on macOS. Add one or more media sources, set the server name and port, then press Start.

The native Linux GUI uses the same config schema as the server and writes `config.ini` beside the installed executable, so command-line and desktop launches share settings. On WSLg, the launcher forces FLTK to use X11 when a Windows display is available; this avoids cases where the window appears in the taskbar but never paints. If the native GUI binary is missing, `dlna-server-gui` exits with a rebuild hint instead of falling back to another UI.

To hide the WSLg copy-mode warning title on systems where WSLg still adds it, create `%USERPROFILE%\.wslgconfig` with:

```ini
[system-distro-env]
WESTON_RDP_COPY_WARNING_TITLE=false
```

Then run `wsl --shutdown` and launch the app again. This only hides the title warning; it does not change whether WSLg is using RAIL or VAIL internally.

### Headless Linux/macOS

```sh
./build/dlna-server --port 8200 --name "DLNA Server" --source /path/to/media
```

Useful options:

- `--port <port>` sets the HTTP port.
- `--name <name>` sets the friendly DLNA device name.
- `--uuid <uuid>` sets a persistent device UUID.
- `--debug` enables extra discovery logging.
- `--source <path-or-url>` adds a folder, playlist file, or FTP share. You can pass it more than once.

Examples:

```sh
./build/dlna-server --source /srv/media --source /srv/playlists/radio.m3u
./build/dlna-server --source 'ftp://user:pass@server:21/media'
```

## Configuration

Settings are stored in `config.ini` beside the executable:

- Windows release output: `output/winx64/config.ini` or `output/winx86/config.ini`
- Windows CMake build output: `build/<Config>/config.ini`
- POSIX release output: `output/linux/install/bin/config.ini`
- POSIX CMake build output: `build/config.ini`
- Linux user install: `~/.local/bin/config.ini`

If `config.ini` is missing, the app creates it on startup with default settings, the computer hostname as `ServerName`, and a generated UUID. On later starts, values from `config.ini` take precedence over defaults.

Server workers read a config snapshot when they start a scan or handle a request. `FileServerPort` stays in the file for compatibility; media files are served on `Port`.

The server also writes `media-cache.tsv` beside `config.ini`. It stores stable file IDs plus scan-error and metadata cache records. Deleting it is safe, but clients may see media item IDs change after the next scan.

When **Debug Log** is enabled, the server writes `debug.log` beside `config.ini` on every platform.

Example:

```ini
[Settings]
ServerName=my-computer-name
Port=8200
FileServerPort=8201
DebugLog=0
RunOnBoot=0
DefaultPlaylistEnabled=0
DefaultPlaylistPath=C:\Path\To\App\default.m3u
IPWhiteList=192.168.1.0/24,fd00::/8
DeviceUUID=11111111-2222-3333-4444-555555555555
DeviceManufacturer=dlna-server contributors
DeviceModelName=dlna-server
PresentationURL=/
MediaSources=C:\Media|D:\Videos|C:\Playlists\radio.m3u|ftp://user:pass@server:21/media
```

## Testing

Run the Windows build and protocol smoke test:

```powershell
cmake -S . -B build-windows
cmake --build build-windows --config Release
cmake --install build-windows --config Release --prefix output\winx64
.\tests\verify-smoke.ps1
```

Run Android reachability checks through ADB against the Windows app:

```powershell
.\tests\verify-android-smoke.ps1 -Target Windows
```

If more than one ADB device is connected, pass the phone serial:

```powershell
.\tests\verify-android-smoke.ps1 -DeviceSerial DKJ9X18709W05461
```

Run the hybrid POSIX-over-WSL Android check with:

```powershell
.\tests\verify-android-smoke.ps1 -Target PosixWsl -DeviceSerial DKJ9X18709W05461
```

The POSIX WSL mode uses `adb reverse` for Android HTTP/SOAP/VLC direct playback and validates SSDP locally inside WSL, because normal Android multicast discovery cannot reliably reach WSL2 NAT.

The Android smoke test requires Windows Firewall access for the built app. If the test reports missing firewall rules, run the helper once from an elevated PowerShell:

```powershell
.\output\winx64\DLNA Server.exe --configure-firewall --port 18200
```

The `--port` value is kept for compatibility with the test command. On Windows, the TCP rule is app-wide and limited to `LocalSubnet`; UDP discovery remains limited to port `1900`. The normal Windows app also prompts to configure these rules the first time the server starts.

Run the POSIX headless build in Termux over SSH and detect it from the Windows computer:

```powershell
.\tests\verify-posix-ssh.ps1
```

`tests\verify-posix-ssh.ps1` reads SSH credentials from `.env`:

```ini
username=your-username
password=your-password
```

The script builds `dlna-server` on the remote device, starts it headless, verifies SSDP `ssdp:alive` multicast from this computer, fetches `description.xml`, checks a unicast SSDP response, and stops the remote server.

## Project Structure

- `src/main.cpp`, `src/mainwindow.cpp`, `src/settingsdlg.cpp` - Windows UI entry points.
- `src/server.cpp`, `src/httpserver.cpp`, `src/ssdp.cpp` - Windows server and discovery implementation.
- `src/posix_main.cpp`, `src/posix_httpserver.cpp`, `src/posix_ssdp.cpp` - headless POSIX implementation.
- `src/fltk_gui_main.cpp`, `packaging/linux`, `packaging/macos` - native POSIX GUI and packaging inputs.
- `src/contentdirectory.cpp` - shared UPnP XML and Browse response generation.
- `src/media_sources.cpp`, `src/posix_media_sources.cpp` - media indexing.
- `tests/verify-smoke.ps1`, `tests/verify-android-smoke.ps1`, `tests/verify-posix-ssh.ps1`, `tests/verify-wslg-gui.ps1` - protocol and device smoke tests.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for local development, testing, and pull request guidance.
