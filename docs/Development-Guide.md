# Development Guide

This page explains how to build, package, release and extend dlna-server. Work on the `review` branch.

## Tools

| Need | Windows | Linux |
|---|---|---|
| Compiler | Visual Studio 2022 (MSVC, C++17) | GCC or Clang with C++17 |
| Build system | CMake 3.20 or newer | CMake 3.20 or newer |
| libcurl | vcpkg, static triplet (`x64-windows-static` or `x86-windows-static`) | `libcurl4-openssl-dev` |
| Window toolkit | Win32 (in the SDK) | GTK 4 (`libgtk-4-dev`), `libx11-dev` |
| Scripts | PowerShell, Python 3, `gh` (releases) | Bash, Python 3, `gh` (releases) |
| Tests | Python 3 and pytest | Python 3 and pytest |

The Windows build links the static MSVC runtime. `dlna_core` and the GTK program compile with `/W4 /permissive-` (MSVC) or `-Wall -Wextra -Wpedantic` (others).

## Source layout

| Folder | Content |
|---|---|
| `src/` | All C++ code. |
| `resources/` | `app.rc`, `resource.h`, icons, `fonts/` (Selawik), `gtk/` style sheets and SVG controls. |
| `packaging/` | `linux/` (launcher, desktop, metainfo, AppRun), `flatpak/`, `macos/`. |
| `scripts/` | Build, packaging and release scripts, `version.py`, `lib_identity.sh`. |
| `tools/` | `gen_gtk_style.py`. |
| `tests/` | Python tests. |
| `identity.env` | Product name and application IDs. |

### Identity

`identity.env` is the single source for the names the app uses. CMake and the scripts read it. It must define `DLNA_APP_ID`, `DLNA_REVERSE_DNS_PREFIX` and `DLNA_PRODUCT_NAME`. The GTK application ID is `<prefix>.<app id>`. CMake writes `generated/version.h` and `generated/app_identity.h` into the build folder. Do not hard-code these names in code or scripts.

## Build

### Windows

```bat
build.bat --x64
build.bat --x86
build.bat                 :: both
build.bat --version v2.26.7631-build5
```

`build.bat` calls `scripts/build-windows.ps1`. It finds vcpkg through `VCPKG_ROOT`, `VCPKG_INSTALLATION_ROOT` or `%USERPROFILE%\vcpkg`, installs curl if needed, runs CMake, and writes `output\winx64` or `output\winx86` with `dlna-server.exe` and a zip file.

For Visual Studio, `CMakeSettings.json` has `x64-Debug-vcpkg` and `x64-Release-vcpkg` configurations. Set `VCPKG_ROOT` first.

### Linux

```bash
./build.sh                 # binaries in output/linux
./build.sh --deb           # also a .deb
./build.sh --appimage
./build.sh --flatpak
./build.sh --install       # build a .deb, remove old installs, install
```

Outputs in `output/linux`: `dlna-server`, `dlna-server-gui`, `dlna-server-gui-bin`, `share/`, and any package files.

> **Warning.** `scripts/build_linux.sh` stops running `dlna-server*` processes, empties `output/linux`, and installs missing build packages with `sudo`. It needs passwordless `sudo`, or the `DLNA_SUDO_PASSWORD` environment variable. It builds in a temporary folder under `/tmp`.

Build from Windows with `build.bat --linux`. It runs the Linux script in WSL (distribution `Ubuntu`). `build.bat --install` runs `scripts/install_linux.sh` in WSL.

### CMake options

| Option | Default | Effect |
|---|---|---|
| `DLNA_VERSION` | `0.0.00000` | Version. The scripts set it from `version.py`. |
| `DLNA_ENABLE_GTK4_GUI` | `ON` (non-Windows) | Builds `dlna-server-gui-bin`. |
| `DLNA_FLATPAK_BUILD` | `OFF` | Uses the Flatpak application ID. |

### Targets

- `dlna_core`: static library with all shared and platform code.
- `dlna-server`: Windows window program, or the Linux command-line program.
- `dlna-server-gui-gtk4`: output name `dlna-server-gui-bin`.
- `dlna_gtk_style`: runs `tools/gen_gtk_style.py` to write `resources/gtk/styles1.css`.

## Version numbers

`scripts/version.py` makes tags like `v2.26.7631-build5`.

```
v2.<YY>.<MM><DD><patch>-build<minute>
```

- `2` is the major version, fixed in the script.
- `MM` is `floor(month × 100 / 13)` and `DD` is `floor(day × 100 / 32)`. Both have two digits.
- `patch` is one optional digit. Set it with `--patch` or the `DLNA_PATCH` variable.
- If a file with that version already exists under `output/`, the script raises the patch digit until the version is new.
- `version.py --numeric-of <tag>` prints the number without `v` and `-build`.

## Packaging

| Script | Output | Notes |
|---|---|---|
| `scripts/build_deb.sh` | `.deb` | Calls `build_linux.sh --deb`. Uses CPack. Depends on `libcurl4`. |
| `scripts/build_appimage.sh` | `.AppImage` | Downloads `linuxdeploy` and checks its SHA-256. |
| `scripts/build_flatpak.sh` | `.flatpak` | Needs `flatpak` and `flatpak-builder`. Uses the GNOME 47 runtime. |
| `scripts/install_linux.sh` | none | Removes old installs and old desktop files, then installs the newest `.deb`. |

## Release

Tag the commit, build, then upload:

```bash
./build.sh --release                      # Linux assets
./build.sh --release --notes              # with generated notes
./build.sh --release --update=<tag>       # add assets to an existing release
```

```bat
build.bat --release
```

- Needs the `gh` command, signed in.
- Windows zips come from `output\winx64` and `output\winx86`. Linux files come from `output/linux`. Only files that contain the tag's version number are uploaded.
- `--notes` calls the Gemini API with the commit list. It needs `GEMINI_API_KEY`. If the call fails, the notes are the commit list.
- `scripts/release-diff.sh <prev_tag> <tag>` writes a `src/` diff for the notes.

## Code conventions

### Share logic, split platform code

- Put shared code in one file. Put platform code in a pair such as `httpserver.cpp` and `posix_httpserver.cpp`. The Windows file name has no prefix, or ends in `_win`.
- If both platforms need the same rule, move it to a `*_common.cpp` or a header. Examples: `http_common.cpp`, `ssdp_common.cpp`, `netutils_common.cpp`, `media_sources_common.cpp`, `server_common.cpp`.
- The scan code calls only the functions in `fs_backend.h`. Win32 and POSIX each implement it, so both run the same scan logic.

### Pure decision headers

Rules that decide behavior live in small header-only functions or classes with no window or socket types. The two user interfaces call them. Tests call them through hidden command-line flags.

| Header | Decision |
|---|---|
| `startup_mode.h` | Run without a window? |
| `server_close_policy.h`, `close_pending_state.h` | May the app close now? Close after a stop? |
| `health_check_policy.h` | How many failed checks mean "dead"? |
| `source_drop_policy.h` | Is a drop allowed? |
| `function_key_action.h` | What does F1 or F5 do? |
| `source_list_focus.h`, `hover_focus_state.h` | Delete button and highlight state. |
| `modal_stack.h` | Which dialog is on top? |
| `input_gate.h` | Is the **Add** button enabled? |
| `network_interface_policy.h` | Use an adapter that is not on the allow list? |
| `browse_page_cap.h`, `search_cache_budget.h` | Response and cache limits. |
| `transmitfile_chunking.h` | Chunk sizes for `TransmitFile`. |
| `copydata_validation.h` | Is a `WM_COPYDATA` size plausible? |
| `geometry_dump_policy.h` | Dump dialog geometry? |
| `settings_restart.cpp` | Which changed settings need a restart? |
| `tray_notify.cpp` | Decode a tray callback. |
| `access_keys.cpp` | Choose access letters for labels. |

### Hidden test hooks

Flags that start with `--print-` or `--dump-` run one small check, print the result, and exit. `cli_print_hooks_win.cpp` and `cli_print_hooks_posix.cpp` implement them. `IsTestOnlyFlag()` in `cli_flags.h` recognizes them. The GTK program has its own hooks, for example `--print-settings-reopen`. To add a check:

1. Put the logic in a pure header.
2. Add a `--print-<name>` branch in both hook files, or in `gtk4_gui_main.cpp` for GTK-only checks.
3. Write the Python test.

### Threads, locks and logging

- Wrap every thread entry that can allocate or call libraries in `RunGuarded(L"name", fn)` (`thread_guard.h`). An exception that leaves a thread function ends the process.
- Pool tasks and Windows thread-pool callbacks already catch exceptions.
- Never call `LogPrint` while holding `Config::m_mutex`. `LogPrint` takes a shared lock on it and Linux throws `EDEADLK` on a second lock by the same thread.
- Do not do file or network I/O while holding a lock.
- `Config::mediaSources` may be read directly only on the interface thread. If any background thread writes it, change every direct read to `Snapshot()`.
- Use `Config::Mutate()` for any write that can race with other threads after the server starts.
- Start and stop functions guard against re-entry with an atomic busy flag. Keep that pattern.

### Memory

Every cache and queue has a limit. See [Architecture](Architecture.md#resource-limits). When you add a cache, add a limit and a pure predicate for the eviction rule.

## How to extend

### Add a media type

1. Add a row to `kFormats` in `dlna_utils.cpp`: extension, MIME type, UPnP class, DLNA profile, byte-seek flag.
2. Add the extension (without dot) to `GetMediaSourceFileExtensions()` in `media_source_file_types.h`. This list feeds the file pickers on both platforms. It lists video, audio and playlists. Images are in `kFormats` but not in this list.
3. For a playlist type, also update `IsPlaylistSourcePath()` in `network_sources.cpp` and the parser.

### Add a setting

1. Add the field to `Config` and to `ConfigSnapshot` in `config.h`. `Config::Snapshot()` fills `ConfigSnapshot` by position, so keep the field order identical in both structs.
2. Read and write it in `config.cpp` and `posix_config.cpp` (`Load`, `Save`, `Snapshot`).
3. Add a row to `DetermineSettingsRequiringRestart()` in `settings_restart.cpp` if it is read only at start.
4. Add controls: `resource.h`, `app.rc`, `settingsdlg.cpp` (Windows) and `gtk4_gui_main.cpp` (GTK). Keep both dialogs in step.
5. Add a row to `settings_help.h`.
6. Add the key to [Configuration](Configuration.md).

### Add a command-line option

1. Add a row to `GetCliFlagTable()` in `cli_flags.h`. The help dialog and `--help` use it.
2. Handle the option in `main.cpp`, `posix_main.cpp` and `gtk4_gui_main.cpp`. The hook files have a short pre-scan loop that applies the same options before a hook runs. Update it too.

## GTK style sheets

The Linux window loads four sheets in this order. A later layer wins over an earlier one.

| Order | File | Role | Priority |
|---|---|---|---|
| 1 | `resources/gtk/styles1.css` | Generated from `src/ui_tokens.h` by `tools/gen_gtk_style.py`. Do not edit by hand. | `USER` |
| 2 | `styles2.css` | Square, Windows-10-style defaults for lists, entries, menus, scroll bars, switches and tooltips. | `USER + 1` |
| 3 | `styles3.css` | Replaces rounded GTK shapes with square ones for toolbar buttons, entries, the source list, and popovers. | `USER + 1` |
| 4 | `styles4.css` | Checked subset: focus ring, disabled opacity, frames, selection, flat buttons. | `USER + 1` |
| 5 | `styles0.css` | Overrides from the design file (title bar, toolbar, dialogs, source list). | `USER + 2` |

The loader is `OnAppStartup()` in `gtk4_gui_main.cpp`. Sizes and positions for the Linux window are in `ui_tokens_posix.h`. They differ from `ui_tokens.h` on purpose, so do not "fix" one to match the other.

Settings that GTK 4 does not allow, such as a short tooltip delay, are done in code. The source list shows its full-path hint with a popover that follows the pointer.

## Tests

Tests are Python and use pytest. `pytest.ini` defines these markers:

| Marker | Meaning |
|---|---|
| `posix_only` | Runs on Linux and macOS only. |
| `windows_only` | Runs on Windows only. |
| `gui_only` | Needs the GTK window and a display. Deselected by default. |
| `needs_xvfb` | Starts the window under its own Xvfb. Run with `-p no:xvfb` (already in `addopts`). |
| `needs_wm` | Needs a window manager. Deselected when none is installed. |

`pytest.ini` also skips the build output folders. Test details are in the separate testing document.
