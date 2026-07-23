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

### POSIX install

WSL Ubuntu / Linux install now uses the repo script:

```bash
sudo ./build-assets.sh --install
```

This builds GUI assets by default, writes the `.deb` into `output/linux/`, and installs that package with `dpkg -i` so it shows up in the package database.

For CLI-only installs:

```bash
sudo ./build-assets.sh --cli --install
```

For release artifact builds without installing:

```bash
sudo ./build-assets.sh --platform linux
```

Raw CMake install flow still exists for manual builds:

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
| `--headless` (Windows only) | Starts without showing the main window; forced automatically when any runtime source is supplied via `--source` |
| `--kill-server, -k` (Windows only) | Stops the running server instance via the named pipe and exits |
| `--configure-firewall --port <n>` (Windows only) | Opens the Windows firewall rule and exits; invoked internally via `ShellExecuteW("runas", ...)`, not meant to be run by a user directly |
| `--print-scan-concurrency <n>` | Evaluates the playlist scan concurrency formula for `n` items and exits — for tuning the internal thread budget without running a full scan |
| `--print-scan-cancellation-lifecycle` | Exercises the `BeginScan()` → `IsCancelled()` → `RequestCancel()` cycle and prints the three state values (`0` or `1` each); validates cooperative cancellation state machine without a live scan |
| `--print-mnemonics <csv-labels>` | Converts comma-separated labels to access-key mnemonics via `AssignMnemonics()`, prints the assigned key per label |
| `--print-cue-state <seq>` | Drives a `KeyboardCueState` through a sequence of `k` (keyboard) and `m` (mouse) inputs, prints `HideAccel,HideFocus` after each step |
| `--print-hover-focus-state <csv-events>` | Drives `HoverFocusState` through comma-separated event tokens (`e5`=enter id5, `l5`=leave id5, `f3`=focus id3, `b3`=blur id3), prints the highlighted control ID after each event |
| `--print-any-field-has-content <csv-lengths>` | Passes a comma-separated list of field lengths to `AnyFieldHasContent()` and prints `1` if any has content, `0` otherwise |
| `--print-is-recognized-playlist <path> <textfile>` | Reads `<textfile>`, passes its content to `IsRecognizedPlaylistText()` with the given path, prints `1` or `0` |
| `--print-parse-quoted-comma-list <text>` | Parses `<text>` with `ParseQuotedCommaList()` and prints each field on its own line |
| `--print-decode-legacy-pipe-sources <text>` | Decodes pipe-delimited sources via `DecodeLegacyPipeDelimitedSources()`, prints each entry on its own line |
| `--print-resolve-relative-url <base> <relative>` | Resolves `<relative>` against `<base>` via `ResolveRelativeUrl()` and prints the result |
| `--print-rewrite-hls-manifest <baseUrl> <textfile>` | Reads `<textfile>`, passes its content to `RewriteHlsManifestUrisToAbsolute()`, and prints the rewritten manifest |
| `--print-should-start-headless <explicitFlag> <hasSources>` | Passes two `0`/`1` flags to `ShouldStartHeadless()` and prints the result (`0`/`1`) |
| `--print-debug-log-requires-restart <before> <after>` | Compares `before` and `after` debug-log enablement (`0`/`1` each) via `DetermineSettingsRequiringRestart()`, prints `1` if a restart is needed |
| `--print-media-browsing-restart-required <before-bits> <after-bits>` | Compares two 7-character bitmasks (flags: AddArtistAlbumFolders, DoNotShowAllMediaFolders, SortByTitle, FlatFolderStyle, ShowFileNamesInsteadOfTitles, ProxyStreams, BackgroundScanEnabled) via `DetermineSettingsRequiringRestart()`, prints `1` if restart is needed |
| `--print-should-allow-source-drop <busyOrRunning>` | (Windows only) Passes `0`/`1` to `ShouldAllowSourceDrop(drag)`, prints `0`/`1` |
| `--print-is-supported-source-path <path>` | Prints `1` if `<path>` is a supported local media or playlist path per `IsSupportedLocalMediaOrPlaylistPath()`, `0` otherwise |
| `--print-media-sources` | Dumps current `mediaSources` from a fresh `Config::Snapshot()`, one path per line |
| `--print-effective-media-sources` | Dumps `effectiveMediaSources` (sources + runtime overrides), one per line |
| `--print-clear-override-then-effective` | Calls `ClearRuntimeSourceOverride()`, then dumps `effectiveMediaSources` — verifies that cleared-override state matches the persisted list |
| `--print-source-override-lifecycle <quoted-comma-list>` | Sets runtime source override, starts the server, polls scan progress until complete, then stops — validates the override-start-stop lifecycle end-to-end |
