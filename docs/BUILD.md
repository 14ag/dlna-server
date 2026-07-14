# Build Reference

## Requirements

- CMake >= 3.20 (`cmake_minimum_required` in `CMakeLists.txt`)
- C++17 (`CMAKE_CXX_STANDARD 17`, `CMAKE_CXX_STANDARD_REQUIRED ON`)
- libcurl — `find_package(CURL REQUIRED)` at the top level; every target links `CURL::libcurl` and defines `DLNA_HAS_LIBCURL=1`
- On Windows: a vcpkg installation with the `curl` port, since `find_package(CURL)` resolves through vcpkg's toolchain file
- On POSIX, optionally: FLTK 1.4.x — if `find_package(FLTK)` doesn't find a system install, CMake fetches `release-1.4.5` from the FLTK git repository via `FetchContent` and builds it as part of the tree

## Windows

```
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
cmake --install build
```

Notes:

- The executable target is `dlna-server`, built `WIN32` (no console window), with output name `DLNA Server.exe` (`set_target_properties(... OUTPUT_NAME "DLNA Server")`).
- `dlna_core` sets `MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"` — static CRT, matching the executable's runtime library setting. Don't change one without the other; a mismatch is a link error, not a runtime bug.
- Windows-only compile definitions: `UNICODE`, `_UNICODE`, `WINVER=0x0A00`, `_WIN32_WINNT=0x0A00`, `WIN32_LEAN_AND_MEAN`.
- Linked system libraries: `ws2_32 shell32 ole32 oleaut32 uuid comctl32 shlwapi dwmapi rpcrt4 iphlpapi`.
- `install(TARGETS dlna-server RUNTIME DESTINATION .)` — no versioned subdirectory, installs flat.

## Linux / macOS

```
cmake -B build -S .
cmake --build build
sudo cmake --install build
```

Notes:

- `dlna_core` on POSIX compiles a different source list than Windows (see `docs/ARCHITECTURE.md`) and defines `DLNA_POSIX=1` and `DLNA_RESOURCE_DIR="<source-dir>/resources"`.
- Platform name macro: `DLNA_PLATFORM_NAME="macOS"` or `"Linux"`, used in the SSDP `SERVER:` header (`GetDlnaServerHeader()` in `dlna_utils.cpp`).
- `find_package(Threads REQUIRED)` — `dlna_core` links `Threads::Threads`.

### Native GUI (optional)

Controlled by `DLNA_ENABLE_FLTK_GUI` (default `ON`):

```
cmake -B build -S . -DDLNA_ENABLE_FLTK_GUI=OFF
```

When enabled, this also builds `dlna-server-gui-native`, a second executable compiling the same POSIX source files a second time (it does not link `dlna_core`) plus `src/fltk_gui_main.cpp`. Any change to a POSIX source file needs to build cleanly against both target's include paths and compile definitions — check `target_compile_definitions` for both `dlna_core` and `dlna-server-gui-native` if you add a new `#ifdef`.

### macOS app bundle

If `DLNA_ENABLE_FLTK_GUI=ON` and the platform is Apple, an `DLNAServerApp` custom target assembles `DLNA Server.app`:

```
Contents/MacOS/dlna-server            <- from dlna-server target
Contents/MacOS/dlna-server-gui        <- packaging/macos/dlna-server-gui launcher script
Contents/MacOS/dlna-server-gui-bin    <- from dlna-server-gui-native target
Contents/Resources/*.png, app.ico
Contents/Info.plist                   <- from packaging/macos/Info.plist.in
```

`install(DIRECTORY ...)` installs the whole bundle with `USE_SOURCE_PERMISSIONS`.

### Linux packaging

Non-Apple Unix builds with `DLNA_ENABLE_FLTK_GUI=ON` install:

- a launcher script generated from `packaging/linux/dlna-server-gui`
- an SVG icon into the hicolor theme
- three PNG icons (48/120/256) into `share/dlna-server/icons`
- AppStream metadata generated from `packaging/linux/dlna-server.appdata.xml.in`
- desktop entry installation via `packaging/linux/install_desktop.cmake.in`

CPack is configured unconditionally for `UNIX AND NOT APPLE`:

```
cd build
cpack
```

Produces a `.deb` with `libcurl4` as a declared dependency (`CPACK_DEBIAN_PACKAGE_DEPENDS`) and automatic `dpkg-shlibdeps` resolution (`CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON`).

## Environment variables read at runtime

- `DLNA_SERVER_SKIP_FIREWALL` (Windows) — if set, `Server::Start()` skips the firewall-access check entirely. Useful in CI or when firewall rules are provisioned out-of-band.

## Command-line flags (all platforms)

Parsed in the same way on every platform — overrides are applied after config is loaded, so the config file value is used when a flag is not supplied. The POSIX GUI binary accepts the same flags as the CLI:

| Flag | Effect |
|---|---|
| `--port <n>` | Overrides `config.ini` port; validated with `TryParsePortStrict` |
| `--name <name>` | Overrides server (friendly) name |
| `--uuid <uuid>` | Overrides device UUID |
| `--source <path-or-url>` | Adds a media source; repeatable |
| `--debug` | Enables debug logging (writes `debug.log` next to `config.ini`) |
| `--help` | Prints usage and exits |
| `--headless` (Windows only) | Starts without showing the main window |
| `--configure-firewall --port <n>` (Windows only) | Runs the elevated firewall-rule helper and exits; invoked internally via `ShellExecuteW("runas", ...)`, not meant to be run by a user directly |
