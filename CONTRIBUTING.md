# Contributing to WinDLNAServer

Welcome to the project. We appreciate your interest in making this server better. WinDLNAServer is built as a clean, dependency-free Win32 application, and we want to keep that architecture intact. 

## Getting Started

1. Fork the repository and create a branch from `main`.
2. Keep your commits focused. If you fix a bug, just fix the bug. If you want to add a major feature, please open an issue first so we can talk about the design.
3. Build the project locally and confirm it works before you submit a pull request.

## Local Development Setup

To compile the application, you need:
- Windows 10 (version 1903 or newer)
- Visual Studio 2022 (with the MSVC v143 C++ desktop toolchain installed)
- CMake 3.20 or newer

Open PowerShell or Command Prompt, and run:

```bash
# Generate the solution
cmake -B build

# Compile a debug build
cmake --build build --config Debug
```

You will find `WinDLNAServer.exe` in the `build\Debug` directory. Run it from there to test your changes.

## Reporting Bugs

Check the existing GitHub issues to see if someone else already reported the problem. If they haven't, open a new bug report. 

Please tell us your Windows version and the exact device (like an LG TV or VLC on Android) that was trying to connect. Turn on "Debug mode" in the server settings, reproduce the crash or strange behavior, and attach the relevant lines from `%APPDATA%\WinDLNAServer\debug.log` to your report.

## Writing Your Code

Your code should blend in with the rest of the project. We rely exclusively on Win32 APIs and standard C++17. Do not drag in heavy third-party libraries. If a problem can be solved with a few lines of COM or MSXML, use the built-in Windows tools instead of adding a dependency.

We also use raw WinSock2 for the networking layer. Always check your thread safety when passing state between the background HTTP workers and the GUI thread.

## Pull Requests

When you are ready, push your branch and open a pull request. Write a descriptive commit message that explains *why* you made a change, not just what files you edited. For example: `Fix: Handle long path names properly in the media scanner`.

If your PR modifies the UI, please include a quick screenshot.
