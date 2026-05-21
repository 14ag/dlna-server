# Contributing to dlna-server

dlna-server has two supported build surfaces:

- A native Win32 GUI application for Windows.
- A headless POSIX `dlna-server` target for Linux, macOS, and Termux-style environments.

Keep changes small, test the protocol behavior they affect, and avoid adding third-party dependencies unless the benefit is clear.

## Getting Started

1. Fork the repository and create a branch from `main`.
2. Keep each commit focused on one bug fix, feature, or documentation update.
3. Build locally before opening a pull request.
4. Run the smoke tests that match the files you changed.

## Windows Development

Requirements:

- Windows 10 version 1903 or newer
- Visual Studio 2022 with the MSVC v143 C++ desktop toolchain
- CMake 3.20 or newer

Build:

```powershell
cmake -B build
cmake --build build --config Debug
```

Release build installed to `output/`:

```powershell
cmake -S . -B build-windows
cmake --build build-windows --config Release
cmake --install build-windows --config Release
```

Run the main Windows protocol smoke test:

```powershell
.\verify-smoke.ps1
```

## POSIX Development

Requirements:

- CMake 3.20 or newer
- A C++17 compiler such as `clang++` or `g++`
- `make` or another CMake-supported build tool

Build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Release build installed to `output/`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build
```

Run:

```sh
./output/bin/dlna-server --port 8200 --name "DLNA Server" --source /path/to/media
```

For Termux-based verification from Windows, put SSH credentials in `.env`:

```ini
username=u0_a120
password=your-password
```

Then run:

```powershell
.\verify-posix-ssh.ps1
```

Use `-InstallTools` if the Termux environment still needs `clang`, `cmake`, `make`, or `python`.

## Configuration

The app stores settings in `config.ini` beside the executable. The file is created on startup when missing, then read on later launches.

Do not add new settings without updating:

- `src/config.cpp`
- `src/posix_config.cpp`
- `README.md`
- Relevant smoke tests

## Reporting Bugs

Check existing GitHub issues before opening a new one.

For discovery or playback bugs, include:

- Operating system and build target.
- DLNA client name and version, such as VLC Android, Kodi, or a TV model.
- Whether `description.xml` is reachable in a browser or with `curl`.
- Relevant SSDP or HTTP log lines.
- Exact media format when playback fails.

For Windows debug logs, enable debug mode and attach only the relevant lines from `%APPDATA%\dlna-server\debug.log`.

## Coding Guidelines

- Use C++17.
- Keep shared UPnP and content logic platform-neutral where practical.
- Keep Windows-specific code in the Win32 implementation files.
- Keep POSIX socket and filesystem behavior in the `posix_*` files.
- Preserve byte-range HTTP behavior and SSDP header compatibility.
- Be careful with background threads and shared state.

## Pull Requests

Before opening a pull request:

1. Rebuild the changed target.
2. Run the matching smoke test.
3. Update documentation when commands, config paths, or user-visible behavior change.
4. Include screenshots for Windows UI changes.

Use commit messages that explain the reason for the change, not only the files touched. Example:

```text
Fix config loading from executable directory
```
