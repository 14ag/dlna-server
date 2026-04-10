# Changelog

All notable changes to the WinDLNAServer will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
- User-configurable server properties, stored in `%APPDATA%\WinDLNAServer\config.ini`.
- Support for grouping audio files into Artist/Album virtual containers.
- Start-on-boot configuration via standard Windows registry keys. 
