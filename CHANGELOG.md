# Changelog

All notable changes to DLNA Server will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Windows keeps the PC awake while the DLNA server is starting, running, or stopping.
- Settings now include default playlist entry creation with browse buttons for movie and subtitle paths.
- DLNA device descriptions now advertise bundled 48, 120, and 256 px PNG icons.

### Changed
- Start, stop, and restart now run outside the UI thread and show busy status text.
- Removed disabled thumbnail placeholder controls from desktop settings.

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
