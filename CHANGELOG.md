# Changelog

All notable changes to WinDLNAServer will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Headless POSIX `dlna-server` target for Linux, macOS, and Termux-style testing.
- POSIX implementations for config loading, logging, interface enumeration, HTTP serving, SSDP discovery, and media scanning.
- SSH-based POSIX verification script that builds in Termux and verifies SSDP `ssdp:alive`, `description.xml`, and unicast SSDP response from Windows.
- Android USB smoke test for ADB, VLC package presence, and phone-to-server HTTP reachability.
- IP whitelist enforcement in the POSIX HTTP server.

### Changed
- Configuration now lives beside the executable as `config.ini` instead of under `%APPDATA%\WinDLNAServer`.
- The Windows app loads configuration at startup and creates `config.ini` if it is missing.
- Smoke tests now seed and verify root-local configuration.
- Malformed POSIX HTTP and SOAP numeric inputs now fail with error responses instead of escaping request handlers.

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
