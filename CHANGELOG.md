# Changelog

- The Windows program is now called `dlna-server.exe`. It was `DLNA Server.exe`.
- On Linux, the commands are `dlna-server` (CLI), `dlna-server-gui` and `dlna-server-gui-bin` (GUI).
- Installed files now use the internal app id `com.github.dlna_server_14ag`. This includes the desktop entry, icons, and the `.deb` file name.
- The Windows "Run on startup" entry now uses the new app id. When you turn it off, the old `dlna-server` entry is also removed.
- The version number is now in the file names of the `.deb` and the Windows `.zip`.

New version format: `v1.YY.DDD-buildN` (for example `v1.26.76153-build7`). The old fixed version was `1.7.0`.

---

# Highlights

## Windows app

- The main window can no longer be resized.
- Long source paths now show a full-path tooltip when you hover over them. The tooltip has no delay. It only shows when the path is cut off.
- If you click the main window while a dialog is open, the dialog now flashes and comes to the front. Before, nothing happened.
- The Help window can no longer be minimized.
- If a core part of the server stops by itself (HTTP or discovery), the app now stops the server safely and shows this message: "The server stopped unexpectedly and has been shut down. Press Start to resume". It waits for 4 bad checks in a row first, so short restarts do not trigger it.
- When you change the **Default playlist** setting while the server is running, the app now asks you to restart the server.
- The app no longer repeats the Windows Firewall check on every start. This makes Start faster after the first time.
- A very large file (over 2 GB) is now sent correctly to the player. Before, the file size could wrap around and break playback.
- The "Debug log" no longer writes the control sizes of the Settings window. A new hidden flag does that now.

## Linux app (GTK4)

- New look that matches the Windows 10 dark style: custom title bar with minimize and close buttons, new colors, and the Selawik font.
- The window now has a 16 px icon in the title bar. A new 16 px icon is installed for the desktop.
- Dialogs now block the main window. Clicking the main window flashes the dialog.
- Press **Esc** to close any dialog. Press **Backspace** to close it when you are not typing in a text field.
- Hovering over a cut-off source path shows a popup with the full path.
- Message boxes now open on top of the dialog that is open (for example, on top of Settings).
- Underlined access keys (mnemonics) now work on buttons in the main window and in the dialogs.
- Closing the window while the server is starting or stopping now works correctly. The app finishes the stop and then exits.
- The WSLg Start Menu and taskbar icon now use one matching short id. The launcher script `dlna-server-gui` finds the real program more reliably and writes a log to `~/.cache/dlna-server/gui-launch.log` when it fails.
- New package formats: **AppImage** and **Flatpak** (besides `.deb`).
- A new `postinst` step for the `.deb` package.

## Command line

- `--help` now prints a full, aligned help text with a usage line. The program name is correct for each platform.
- New flag `--no-debug`: turns off debug logging for this run only. It does not change your saved config.
- Plain paths without a flag (`dlna-server PATH...`) work the same as `--source`. This is now in the help.
- `--configure-firewall` and the "Run on Windows startup" setting are only shown on Windows.
- An unknown option now prints "Unknown option" and the help, and exits with code 2.
- `--print-app-id` prints the app id.
- If a second app is started with `--source`, the first one switches to the new sources. You can read the current sources with `--print-effective-media-sources`. It now also works on Windows.
- On Linux, the single-instance lock and socket are in `/tmp/com.github.dlna_server_14ag-<uid>/`. If the lock cannot be opened, a second app does not start (it fails closed).

## Finding the server (discovery) and playing media

These changes make the server easier to find and more reliable for players.

- **Faster discovery after start.** The server now sends "alive" announcements at 0, 1, 3, 8, and 20 seconds after start. Before, it sent one short burst.
- **More frequent refresh.** The server now repeats its announcements every 4 to 6 minutes. It was 12 to 14.5 minutes. Players with a short discovery window find it more often.
- **Replies to searches are more reliable.** Each reply is sent twice, has `Content-Length: 0`, and is never delayed more than 1 second. Some players (for example VLC) stop waiting for a reply after a short time.
- **One address per network card.** The server now advertises one address for each adapter. Before, a second address on the same adapter could replace a good address on the player and break the connection.
- **Better address choice on IPv6.** The server now picks the right network card for link-local IPv6 devices.
- **Search replies on Linux use the right network card.** The unicast interface option was in the wrong byte order and did nothing. It is fixed.
- **No restart storms.** When the network changes (cable, Wi-Fi, sleep and resume), the server waits a short time and restarts discovery only if the real address list changed. Rapid changes are joined into one restart.
- **Stable boot id.** The server keeps the same `BOOTID` and `CONFIGID` while the app runs. Players do not reload the device description after each restart.
- **Goodbye messages and last replies.** On stop, the server sends pending search replies that are due, then sends "byebye".
- **Link-local IPv6 is dropped** when a better address exists. Phones with Wi-Fi and mobile data work better this way.
- **Virtual network cards are skipped**, unless they have a default gateway (for example, a bridged VM card). Real LAN bridges still work.
- **Zero-byte files** now get `Content-Length: 0` on Linux. Before, the player could wait forever.
- **Browse requests with `RequestedCount=0`** now return at most 16,000 items per answer. Players page the rest.
- **Search** with a title or class filter is faster and uses a memory limit (256 folders and 200,000 items).
- **Playlist subtitles and remote files** are handled in the same way on both platforms.
- The server icon is read once and kept in memory.

## Folders, scanning, and the library

- A rescan no longer makes media disappear while it runs. Old items stay playable until the scan finishes. Then only the items that are really gone are removed.
- Stopping the server now stops all running scans.
- Scanning is faster and uses fewer threads: each source runs in a limited thread pool. Subtitles and cover art are found with one folder listing, not many file checks.
- Folder watching (automatic rescan) now works on Windows. Before, the change message could never arrive. On Linux, it handles deep folders (limit 64 levels) and busy folders better.
- Item IDs are reused after cleanup, so the ID space does not run out on long runs.
- Cover art file names are now matched without case problems (`Folder.JPG`, `cover.jpg`, and so on).
- A source that is one single media file now shows the file directly under the root. It does not make a folder around it.

---


## Crashes and data races

- An error inside a worker thread no longer ends the whole app. The thread pool and the Windows thread pool now catch and log errors.
- Closing the app or using `--kill-server` while the server starts or stops no longer leaks memory or loses the result message.
- The server `Start` and `Stop` calls, and the HTTP and SSDP `Start` and `Stop` calls, now reject a second call at the same time. This removes a race.
- `Start` and `Rescan` use one lock, so a rescan cannot wipe the library while the server starts.
- The thread pool runs leftover tasks on exit. A waiting scan can no longer hang forever.
- The Windows dialogs now handle a failed message loop (`GetMessage` returns -1) safely.
- Config values are read and saved safely from many threads. The config file is written without holding the lock.
- Messages received from another app (`WM_COPYDATA`) are checked for a valid size before use.
- Number parsing is now strict. Wrong numbers no longer throw errors.

## Memory and speed

- The log file path is calculated once. The log file is written outside the main log lock, so one slow write cannot block all threads.
- The IP address and settings caches have limits. They use batch removal where needed.
- Search memory has a total item limit.
- The catalog list reserves memory from the size of the last scan.
- GENA event delivery reuses one curl handle per worker thread.
- IPv4 and IPv6 sends use separate locks.
- Moved shared code into common files (`server_common.cpp`, `netutils_common.cpp`, `fs_backend`) so both platforms run the same logic.
- Removed old and unused code: `fs_change_debounce.h`, the old Windows firewall helpers, and the old startup scripts.

---

# Part 3: For developers and maintainers

## Build and release

- One new identity file, `identity.env`, holds the app id, the reverse DNS prefix, and the product name. CMake builds `app_identity.h` from it.
- New `scripts/version.py` creates the version. It no longer asks questions. It adds a build number when that version already exists in `output/`.
- New scripts: `build_linux.sh`, `build_deb.sh`, `build_appimage.sh`, `build_flatpak.sh`, `install_linux.sh`, `lib_identity.sh`. Old scripts `build-linux.sh` and `install-dlna-server-linux.sh` are removed.
- `build.sh` options: `--deb`, `--appimage`, `--flatpak`, `--install`, `--release [--notes] [--update=<tag>]`, `--version=<tag>`. `build.bat` accepts `--version`.
- The GitHub release workflow now needs a tag when it is started by hand. It checks that the tag has the form `vX.Y.Z` or `vX.Y.Z-buildN`. It uses `GH_TOKEN`. It uses Gemini release notes only when `GEMINI_API_KEY` is set.
- The release publish step only selects files that match the version and the platform.
- The CMake zlib search code was removed. The Windows resource version is now split into the 4 fields the resource compiler needs.
- The GTK app now links with X11.
- The generated files `generated/version.h`, the old Flatpak files, and test logs were removed from the repository. New `.gitignore` entries cover them.
- `.gitattributes` forces LF line endings for the Linux and macOS launcher scripts.
- Agent tool files moved or changed: `.opencode/opencode.json`, `.codex/config.toml`, `AGENTS.MD`, `UI/AGENT.md`.

## Tests

- Tests use Python (`pytest`). `pytest.ini` has new platform markers. The test folder under `tmp/` is cleaned after each session.
- Many new hidden `--print-*` flags let tests check logic without a GUI. They are split into `cli_print_hooks_win.cpp` and `cli_print_hooks_posix.cpp`. Examples: health check, close pending state, modal stack, SSDP search response, SSDP boot id, search cache budget, icon cache, interface policy.
- `--dump-widget-geometry` (and the matching GTK flag) prints control positions and sizes for UI comparison.

## Documentation

- New: `docs/TESTING.md`, `docs/UI.md`, `docs/gtk4_windows10_ui_design_guide.md`, `docs/release-pipeline-white-box-report.md`.
- Updated: `README.md`, `docs/BUILD.md`, `docs/ARCHITECTURE.md`, `docs/CONFIGURATION.md`, `docs/HTTP-SERVER.md`, `docs/specs.md`.
- The README credits the B00merang-Project Windows 10 theme.
- The font Selawik (SIL Open Font License) is now included.

## Known risks (from the release report)

- The hosted Linux build (AppImage, Flatpak, apt packages) still needs a real GitHub Actions run to confirm it works.
