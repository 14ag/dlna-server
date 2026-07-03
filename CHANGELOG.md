# Changelog

## [Unreleased]

### Changed
- Media indexing now builds lookup maps off-lock and swaps completed scans into place.
- Server startup now begins HTTP and SSDP before the background media scan completes.
- Running servers now watch local media sources and rescan after file changes without a manual restart.
- Media scans now persist stable file IDs, scan errors, and metadata cache records in `media-cache.tsv` beside `config.ini`.
- GENA ContentDirectory subscribers now receive async `SystemUpdateID` `NOTIFY` callbacks after successful scan swaps.
- Remote media access now uses an optional `libcurl` backend instead of spawning `curl` per request.
- SSDP M-SEARCH MX delays now run on response workers instead of blocking receive loops.
- Scan, HTTP, SSDP, and description workers now read immutable config snapshots for request-time behavior.
- Existing media organization settings now apply more consistently across local folders, playlists, and SMB/FTP sources.
- Playlist order is preserved unless title sorting is enabled or requested by the DLNA client.
- `FileServerPort` remains accepted in `config.ini`; media file serving uses `Port`.

### Fixed
- FTP-hosted playlists now recurse into nested `.m3u8` manifests, log rejected entries, and redact credentials in remote URL logs.
- POSIX endpoint discovery now skips APIPA IPv4 addresses; Windows advertises all usable IPv4 aliases.
- Device description metadata can now be configured, including manufacturer, model name, and presentation URL.
- SOAP control dispatch now uses action-element matching and bounded POST body reads.
- Windows media, subtitle, and album-art streaming now uses scoped file handles.
- IP whitelist checks now support CIDR ranges and exact-match hash lookup.
- POSIX HTTP client threads and in-memory logs are bounded.
- Remote source failures now produce clearer logs, companion album art lookup covers `thumb` names, and Browse no longer advertises missing album art.
- Background rescans are now serialized so watch-triggered and manual rescans cannot race on the scan thread handle.
- Watch mode now reads current config snapshots each poll so source changes are monitored without restart.
- Watch mode no longer ignores local media entries after a fixed scan-signature cap.
- Remote content-length probing now uses a concurrency limiter to bound parallel network requests during media scans.

## [1.4.0] - 2026-05-29

### Added
- Windows keeps the PC awake while the DLNA server is starting, running, or stopping.
- Settings now include default playlist entry creation with browse buttons for movie and subtitle paths.
- DLNA device descriptions now advertise bundled 48, 120, and 256 px PNG icons.
- Windows and FLTK source lists now support a text-labeled **Delete** button and keyboard `Delete`.
- DLNA service handling now includes ConnectionManager SOAP responses, ContentDirectory Search, and companion album-art serving.
- Advertised UPnP event URLs now accept GENA subscribe and unsubscribe requests.
- POSIX SSDP now supports IPv6 sockets, M-SEARCH responses, and periodic `ssdp:alive` refreshes.
- Android VLC smoke tests now cover both the Windows app and the POSIX WSL server path.
- CI now runs Python tests plus CMake builds on Windows and Linux.

### Changed
- Start, stop, and restart now run outside the UI thread and show busy status text.
- Main toolbar buttons now use text labels, Windows-style spacing, and a dark Windows title bar/frame.
- Removed disabled thumbnail placeholder controls from desktop settings.
- Media protocol metadata is now generated from a shared DLNA format table for Browse, HTTP streaming, and ConnectionManager responses.
- Release scripts now verify downloaded packaging inputs, avoid stale AppImage reuse, and publish Windows assets from GitHub Actions.
- Added a PDF-derived DLNA framework upgrade blueprint for future protocol hardening work.
- POSIX desktop builds now use the native FLTK GUI only. `DLNA_ENABLE_FLTK_GUI=OFF` builds the headless server and skips the GUI launcher.
- Remote SMB and FTP fetching now launches `curl` without a shell.
- Config, CLI, and desktop settings now reject invalid ports before the server binds or advertises them.

### Fixed
- Windows modal sub-windows now preserve quit messages and use the dark DWM frame consistently.
- IP whitelist reloads are synchronized with HTTP workers.
- POSIX HTTP workers are tracked and joined during shutdown, with receive and send timeouts applied.
- POSIX media scanning continues through unreadable directory entries.

### Removed
- Removed the Python/Tk POSIX GUI fallback and its packaging paths.

### Security
- Removed shell command construction from remote media fetching to block command injection through remote URLs.
- Rejected out-of-range ports consistently across supported configuration paths.

## [1.3.0] - 2026-05-24

### Changed
- Windows and Linux desktop menus now show the app as **DLNA Server**.
- DLNA Server now accepts `.m3u`, `.m3u8`, and `.pls` playlist files as media sources.
- DLNA Server can read media from SMB and FTP shares such as `smb://user:pass@server/share` and `ftp://user:pass@server:21/media`.
- Windows, Linux, and macOS desktop screens now let you add folders, playlist files, and network shares as media sources.
- Linux keeps the command names `dlna-server` and `dlna-server-gui`, so existing shortcuts and scripts keep working.
- If the server name is blank, DLNA Server now uses the computer hostname. A name saved in `config.ini` still takes priority.
- Linux desktop installs now include richer app details for desktop software centers and app launchers.

### Fixed
- The Linux desktop app opens more reliably under WSLg.

## [1.2.0] - 2026-05-21

### Added
- Linux, macOS, and other POSIX systems can now run DLNA Server from the command line.
- Linux now has a native desktop app with folder management, settings, logs, and start/stop controls.
- DLNA Server can enforce an IP whitelist on POSIX systems.
- Companion subtitle files are now discovered and advertised to compatible Samsung DLNA clients.
- Media responses now include better byte-range support for seeking.
- The server now handles more Browse requests through `ContentDirectory:1`.

### Changed
- Configuration now lives beside the app as `config.ini`.
- Windows and POSIX builds now use the same behavior for byte ranges, `HEAD` requests, subtitles, and invalid request bodies.
- Media scanning now skips hidden, system, and unreadable files. It also avoids symlink and reparse loops.
- Linux AppImage downloads now use the native Linux desktop app.

### Fixed
- Empty-file byte-range requests now return the correct `416` response.
- Malformed Browse requests now return a DLNA SOAP fault instead of breaking the request.
- Bad `Content-Length` requests are now rejected more consistently.

## [1.0.0] - 2026-04-10

### Added
- Native Windows app for managing media folders and server settings.
- System tray support with startup minimization.
- Media streaming over HTTP with byte-range seeking.
- Multicast SSDP discovery for local DLNA and UPnP devices.
- `ContentDirectory:1` Browse support.
- Directory scanning with flat-folder browsing.
- Support for common video, audio, and image files, including MP4, MKV, AVI, TS, MP3, FLAC, and JPG.
- Device access control by IP address.
- User-configurable server name, ports, and media settings.
- Artist and album grouping for audio folders.
- Optional start-on-boot support on Windows.
