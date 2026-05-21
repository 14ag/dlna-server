# Changelog

All notable changes to dlna-server will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed
- Rebranded Windows and Linux release artifacts to `dlna-server`.
- Standard CMake installs now default to `./output`, so Linux users can build without PowerShell.
- Missing or empty server names now default to the computer hostname. Values already set in `config.ini` still win.
- Linux desktop installs now include appstream metadata and a richer desktop entry.

### Fixed
- The Linux GUI launcher now runs the native FLTK binary through a wrapper and forces FLTK's X11 backend under WSLg when a Windows display is available.

## [1.2.0] - 2026-05-21

### Added
- Headless POSIX `dlna-server` target for Linux, macOS, and Termux-style testing.
- POSIX implementations for config loading, logging, interface enumeration, HTTP serving, SSDP discovery, and media scanning.
- SSH-based POSIX verification script that builds in Termux and verifies SSDP `ssdp:alive`, `description.xml`, and unicast SSDP response from Windows.
- Android USB smoke test for ADB, VLC package presence, and phone-to-server HTTP reachability.
- IP whitelist enforcement in the POSIX HTTP server.
- Shared DLNA utility layer for header parsing, byte ranges, MIME lookup, subtitle MIME lookup, and natural sorting.
- Companion subtitle discovery and Samsung `sec:CaptionInfoEx` advertisement.
- Native Linux FLTK GUI target with main window, settings dialog, log viewer, media-source management, and start/stop control.
- Linux AppDir and AppImage build scripts with desktop metadata and icon validation.
- WSLg GUI smoke script for native Linux GUI launch checks.
- UMS-inspired hardening roadmap for future clean-room upgrades.

### Changed
- Configuration now lives beside the executable as `config.ini`.
- The Windows app loads configuration at startup and creates `config.ini` if it is missing.
- Smoke tests now seed and verify root-local configuration.
- Malformed POSIX HTTP and SOAP numeric inputs now fail with error responses instead of escaping request handlers.
- Windows and POSIX HTTP servers now share stricter range, `HEAD`, subtitle, and invalid `Content-Length` behavior.
- Media scanners skip hidden/system/unreadable files and avoid reparse/symlink loops.
- Linux AppImage builds now force the native FLTK GUI instead of the Python/Tk fallback.

### Fixed
- Empty-file byte-range requests now return `416` with `Content-Range: bytes */0`.
- Malformed SOAP XML and missing required Browse tags now return SOAP fault `401`.
- Windows smoke tests now validate description XML, Browse, subtitles, `HEAD`, `206/416`, empty-file ranges, and bad `Content-Length`.

## [1.0.0] - 2026-04-10

### Added
- Native Windows UI to manage folders and server settings (no Electron or Qt overhead).
- System tray integration with startup minimization support.
- Media streaming over HTTP with full byte-range request support.
- Multicast SSDP device discovery engine for local network broadcasting.
- Support for `ContentDirectory:1` Browse SOAP actions.
- Real-time directory traversal and flat-folder view generation.
- Extensive media support (MP4, MKV, AVI, TS, MP3, FLAC, JPG, and more).
- Device whitelist via IP checking.
- User-configurable server properties.
- Support for grouping audio files into Artist/Album virtual containers.
- Start-on-boot configuration via standard Windows registry keys.
