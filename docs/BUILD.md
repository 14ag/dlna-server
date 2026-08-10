# Build Reference

## Requirements

- CMake >= 3.20 (`cmake_minimum_required` in `CMakeLists.txt`)
- C++17 (`CMAKE_CXX_STANDARD 17`, `CMAKE_CXX_STANDARD_REQUIRED ON`)
- libcurl — `find_package(CURL REQUIRED)` at the top level; every target links `CURL::libcurl` and defines `DLNA_HAS_LIBCURL=1`
- On Windows: a vcpkg installation with the `curl` port, since `find_package(CURL)` resolves through vcpkg's toolchain file
- On POSIX, optionally: GTK4 development files (`libgtk-4-dev` on Debian/Ubuntu, `gtk4-devel` on Fedora) — CMake finds GTK4 via `pkg-config`

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
sudo ./build.sh --install
```

This builds GUI assets by default, writes the `.deb` into `output/linux/`, and installs that package with `dpkg -i` so it shows up in the package database.

For CLI-only installs:

```bash
sudo ./build.sh --cli --install
```

For release artifact builds without installing:

```bash
sudo ./build.sh
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

Controlled by `DLNA_ENABLE_GTK4_GUI` (default `ON`):

```
cmake -B build -S . -DDLNA_ENABLE_GTK4_GUI=OFF
```

When enabled, this builds `dlna-server-gui-gtk4`, a second executable that compiles `src/gtk4_gui_main.cpp` and `src/posix_tray.cpp` against GTK4, plus the shared `dlna_core` library.

### macOS app bundle

If `DLNA_ENABLE_GTK4_GUI=ON` and the platform is Apple, an `DLNAServerApp` custom target assembles `DLNA Server.app`:

```
Contents/MacOS/dlna-server            <- from dlna-server target
Contents/MacOS/dlna-server-gui        <- packaging/macos/dlna-server-gui launcher script
Contents/MacOS/dlna-server-gui-bin    <- from dlna-server-gui-gtk4 target
Contents/Resources/*.png, app.ico
Contents/Info.plist                   <- from packaging/macos/Info.plist.in
```

`install(DIRECTORY ...)` installs the whole bundle with `USE_SOURCE_PERMISSIONS`.

### Linux packaging

Non-Apple Unix builds with `DLNA_ENABLE_GTK4_GUI=ON` install:

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
| `--print-clamp-browse-requested-count <requested> <available>` | Clamps a requested Browse result count against the available item count via `ClampBrowseRequestedCount()`, prints the result |
| `--print-close-pending-lifecycle` | Drives `ClosePendingState` through request → stop → restart transitions and prints each state value |
| `--print-concurrent-start-rescan-safety` | Starts the server while `Rescan()` runs concurrently; prints `start-ok` and the resulting leaf-media-item count |
| `--print-config-load-lockstate` | Loads the config through `Config::Load()` and reports whether it completes |
| `--print-config-path` | Prints the resolved config file path |
| `--print-debug-log-session-truncation <path>` | Reuses the debug-log handle across two writes and prints whether the same handle was reused |
| `--print-default-playlist-path` | Prints the default playlist path resolved next to the config file |
| `--print-dlna-server-header` | Prints the SSDP `SERVER:` header value produced by `GetDlnaServerHeader()` |
| `--print-function-key-action <vkCode> <isRunning> <isBusy> <isScanning>` | Runs `DecideFunctionKeyAction()` with four `0`/`1` flags and prints the action (`show-help`, `rescan`, `refresh-source-list`, `show-context-menu`, or `none`) |
| `--print-is-plausible-copydata-size <cbData>` | Passes a copy-data payload size to `IsPlausibleWideStringCopyDataSize()` and prints `1`/`0` |
| `--print-log-since-lifecycle` | Exercises `GetSystemLogSince()` and prints latest-sequence values across two writes |
| `--print-max-client-threads` | Prints the `kMaxClientThreads` constant shared by both HTTP server implementations |
| `--print-media-database-id-overflow-guard <dbPath>` | Drives `MediaDatabase` stable-ID allocation past the overflow guard and prints the result |
| `--print-media-database-id-reuse-lifecycle` | Exercises `MediaDatabase` stable-ID reuse across scan passes and prints the assigned IDs |
| `--print-media-display-title <showFileNames> <titleOverride> <path>` | Builds the display title via `BuildDisplayTitleForMediaFile()` and prints it |
| `--print-media-resource-url-suffix <path>` | Prints the resource-URL extension suffix via `BuildMediaResourceUrlExtensionSuffix()` |
| `--print-media-source-file-extensions` | Prints each registered media-source file extension on its own line |
| `--print-movie-title-from-path <path>` | Derives a movie title from a path via `SourceStemName()` and prints it |
| `--print-network-endpoint-count <port>` | Enumerates network endpoints for `<port>` and prints the count |
| `--print-notify-pool-worker-count` | Prints the GENA notify pool worker count constant |
| `--print-remote-probe-cache-lifecycle <url>` | Probes a remote URL twice through the probe cache and prints the recompute counters (hit vs miss) |
| `--print-resolve-bundled-resource <name>` | Resolves a bundled resource path via `ResolveBundledResourcePath()` and prints it |
| `--print-routable-host-cache-invalidation` | Calls `GetRoutableHostUrl()` twice and prints whether the cached value was recomputed |
| `--print-routable-host-url-twice <port1> <port2>` | Resolves the routable host URL twice for two ports and prints both results |
| `--print-should-close-now <isRunning> <isBusy>` | Passes two `0`/`1` flags to `ShouldCloseNow()` and prints the decision |
| `--print-should-drop-link-local-endpoint <candidateIsLinkLocal> <anyNonLinkLocalExists>` | Passes two `0`/`1` flags to `ShouldDropLinkLocalEndpoint()` and prints the decision |
| `--print-should-use-unlisted-interface <isVirtual> <hasGateway>` | Passes two `0`/`1` flags to `ShouldUseUnlistedInterface()` and prints the decision |
| `--print-single-file-source-scan` | Starts the server with a single-file source, waits for scan completion, prints the leaf-media-item count |
| `--print-single-instance-lifecycle` | Exercises the single-instance lock acquire/listen/stop lifecycle and prints each transition |
| `--print-single-instance-retry-timing` | Prints the single-instance retry timing (attempt count and delay) used when the peer is not yet reachable |
| `--print-sockaddr-length-safety <reportedLength> <destinationCapacity>` | Passes a sockaddr length and destination capacity to `IsSockaddrLengthSafeToCopy()` and prints `1`/`0` |
| `--print-strip-resource-id-extension <suffix>` | Strips an extension suffix from a resource ID via `StripResourceIdExtension()` and prints the result |
| `--print-thread-guard-behavior` | Runs a guarded thread entry that throws and prints whether the exception was caught (`RunGuarded` behavior) |
| `--print-thread-pool-exception-resilience` | Submits a throwing task to a `BoundedThreadPool` and verifies the pool survives; prints `1`/`0` |
| `--print-transmitfile-chunk-plan <totalBytes>` | Computes the transmit-file chunk sizes via `ComputeTransmitFileChunkSizes()` and prints each chunk size |
| `--print-tray-notify-decode <rawLParam> <expectedIconId>` | Decodes a tray notification `lParam` via `DecodeTrayNotifyEvent()` and prints the action |
| `--print-trim-wide <text>` | Trims leading/trailing whitespace via `TrimWide()` and prints the result |
