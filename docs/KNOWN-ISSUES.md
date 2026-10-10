# Known Issues

> Verification status (Oct 2026): each entry below was checked against current
> source (`src/`) and the built binaries. Statuses recorded per item.

## Samsung 1GiB Content-Length for remote files of unknown size

When a remote (proxied) media file's size cannot be determined (no `Content-Length` from the upstream, no cached probe result) and the client `User-Agent` is empty or starts with `SEC_HHP_` (Samsung TVs), the server sends a placeholder `Content-Length: 1073741824` (1 GiB) instead of omitting the header. Samsung DLNA clients refuse to play files that lack `Content-Length` entirely, so a fabricated large-enough value is sent as a pragmatic workaround. The Samsung TV will seek/play within whatever portion of the file the server can actually serve; playback stops when the real content ends before the advertised size.

See: `httpserver.cpp` line 588 (`ua.find("SEC_HHP_")`); `posix_httpserver.cpp` line 511; header emitted at line 597 / 520.


## SMB source support removed

`smb://` and `smbs://` entries in `config.ini` are recognized and logged as unsupported but silently skipped during scan. They are not rejected at config-load time — this preserves the entry for users who may switch back to a build with SMB support, or who want to keep their config file portable. The feature was removed because libcurl's SMB backend only supports SMB1/CIFS (which most current servers disable or refuse) and has no directory-listing capability for any dialect.

## `FileServerPort` accepted but not used for serving

A `FileServerPort` value in `config.ini` is loaded without error and persists on save, but all media content is served on the `Port` value. If the two differ, a log message notes the discrepancy. This exists for backward compatibility with very old config files.

## Top-level unreachable sources appear as empty containers

When a media source (local folder, FTP URL, playlist) cannot be read or fetched at scan time, a top-level container is still created with zero children. A renderer browsing the tree sees an empty folder and cannot distinguish "folder exists but was unreachable" from "folder is genuinely empty." Nested unreachable playlist entries do not create empty folders — they are silently skipped (see `docs/MEDIA-SCANNING.md`). Fixing this for top-level sources would require a change to the scan source-dispatch path (e.g., deferring container creation until the first successful probe of each source).

## Watch-mode time-of-check/time-of-use gap

The watch loop (`source_watcher.cpp`) computes an FNV-1a hash over source metadata (path, mtime, size for local sources; existence check for remote URLs) every 5 seconds. A hash change triggers `Rescan()`. There is a window between the hash check and the actual scan read during which a source may change again — the scan will see whichever state the filesystem is in at scan time, but an intermediate state may be missed entirely (e.g., a file added then removed between poll ticks). This is inherent to poll-based monitoring and is not considered a defect, but it is documented so callers are aware that near-simultaneous changes may coalesce.

## No HTTP directory listing

`http://` and `https://` URLs can be used as single-file sources (added via `--source` or as playlist entries) but cannot be walked as directories. `ListRemoteDirectory()` in `network_sources.cpp` only supports FTP/FTPS for remote directory enumeration. An HTTP URL that points to a directory listing page or an RSS/JSON feed of files will not be expanded.

## `ConnectionManager` GENA subscriptions never fire

`SUBSCRIBE`/`UNSUBSCRIBE` to `/upnp/event/connection_manager` are accepted and tracked, but `NotifySystemUpdateId()` only dispatches to `/upnp/event/content_directory` subscribers. ConnectionManager eventing is reserved for future use (e.g., when the server needs to report connection status changes).

`upnp_eventing.cpp` line 24 dispatches to both paths for subscribe tracking, but `DispatchNotifyToSubscribersLocked` (line 208–227) only queues jobs when `IsContentDirectoryEventPath` (line 216) is true; ConnectionManager subscriptions are stored but never notified.

## `RunOnBoot` is a no-op on POSIX

The `RunOnBoot` config field is loaded and saved on all platforms, but `Config::SetRunOnBoot()` only writes/removes a `HKCU\...\Run` registry value on Windows. On POSIX, the method is a no-op. Users who want auto-start on Linux should configure it through their desktop environment or init system directly.


## WSLg (Linux GUI on Windows) drag-and-drop not supported

Dragging files from Windows Explorer into the GTK4 GUI binary running under WSLg does not deliver the drop to the application. This is a platform limitation, not a bug in this project: WSLg's Weston compositor does not implement cross-VM drag-and-drop over its RDP backend (clipboard text/bitmap is bridged, but DnD is not). See upstream tracking issue [microsoft/wslg#84](https://github.com/microsoft/wslg/issues/84) and the WSLg architecture post which states "Drag and drop is not currently supported."

Workarounds for getting files into the GUI:
- Use the file picker and navigate to `/mnt/c/...` (same files, one extra click).
- Paste a Windows path as text — clipboard is bridged.
- Pass the path on the command line via `--source`.

The only known third-party workaround (`wsl-drag-relay`) injects into classic X servers (VcXsrv/Cygwin/X/Xming) and explicitly does not support WSLg; using it requires disabling WSLg entirely, which is not recommended for a single drop target.

This project cannot fix the gap from inside the GTK application — DnD events never reach the Wayland/X surface from the host.

- Status: **still applies** — WSLg has not shipped cross-VM DnD as of Oct 2026; no in-repo change can deliver host drops to the app.


