# WinDLNAServer

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows-lightgrey.svg)]()

WinDLNAServer is a native Windows desktop application that streams your local video, audio, and image files to DLNA-compatible clients on your network. 

We built this project around C++17 and raw Win32 APIs to avoid heavy frameworks like Qt or Electron. The UPnP network layer runs entirely on background threads. When you want to add media folders or change settings, you do it all through a standard, lightweight Windows dialog box. 

## Features

- Streams MP4, MKV, MP3, FLAC, and most standard image formats over HTTP.
- Supports byte-range requests. This allows your TV or mobile player to scrub and seek through video files.
- Advertises the server over the local network using SSDP multicast.
- Ships with a native Windows UI to manage folders and server state.
- Minimizes cleanly to the system tray and supports optional Windows autostart.
- Implements IP whitelisting for basic device access control.
- Requires zero third-party dependencies outside of the standard compiler toolchain.

## Compiling

You need a Windows 10 machine (version 1903 or newer), Visual Studio 2022, and CMake to compile the source code.

To build it from the command line:

1. Clone or download this repository.
2. Generate the build files:
   ```cmd
   cmake -B build
   ```
3. Build the executable:
   ```cmd
   cmake --build build --config Release
   ```

You will find `WinDLNAServer.exe` sitting in the `build/Release` folder. 

## Usage

Launch the executable. The main window will open, and you can click the `+` button in the toolbar to add folders that contain your media. Once you have a folder added, click the Start button (▶). 

Your smart TV or DLNA player should practically instantly see "WinDLNA Server" pop up on the local network. 

If you close the main window, the server does not shut down. It retreats to the system tray and keeps running. Right-click the tray icon when you actually want to stop the server or exit the application. 

Server settings are saved locally to `%APPDATA%\WinDLNAServer\config.ini`. You can toggle flat-folder views, configure custom ports, or set up an IP whitelist directly through the application's settings dialog.

## Contributing

We welcome patches and bug reports. Please see [CONTRIBUTING.md](CONTRIBUTING.md) for instructions on setting up your local environment and submitting pull requests. If you find a security issue, refer to our [SECURITY.md](SECURITY.md) guidelines.
