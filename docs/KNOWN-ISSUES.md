# Known Issues

## Samsung 1GiB Content-Length for remote files of unknown size

When a remote (proxied) media file's size cannot be determined (no `Content-Length` from the upstream, no cached probe result) and the client `User-Agent` is empty or starts with `SEC_HHP_` (Samsung TVs), the server sends a placeholder `Content-Length: 1073741824` (1 GiB) instead of omitting the header. Samsung DLNA clients refuse to play files that lack `Content-Length` entirely, so a fabricated large-enough value is sent as a pragmatic workaround. The Samsung TV will seek/play within whatever portion of the file the server can actually serve; playback stops when the real content ends before the advertised size.

See: `contentdirectory.cpp` — `ShouldFakeContentLengthForSamsung()`; `httpserver.cpp` / `posix_httpserver.cpp`.

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

## `RunOnBoot` is a no-op on POSIX

The `RunOnBoot` config field is loaded and saved on all platforms, but `Config::SetRunOnBoot()` only writes/removes a `HKCU\...\Run` registry value on Windows. On POSIX, the method is a no-op. Users who want auto-start on Linux should configure it through their desktop environment or init system directly.
