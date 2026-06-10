# UMS Upgrade Roadmap

This roadmap records clean-room upgrade work derived from protocol behavior notes. GPL implementation code must not be copied.

## No New Dependencies

- Share protocol helpers for range parsing, header lookup, content features, source protocol info, subtitles, and natural sort.
- Keep HTTP GET and HEAD behavior aligned on Windows and POSIX.

## Optional FFmpeg/ffprobe

- Add metadata and thumbnail probes only behind build-time and runtime checks.
- Keep existing scan behavior when the optional tools are unavailable.

## Index/Database

- Extend media indexing after in-memory scan swap behavior is stable.
- Keep SystemUpdateID tied to successful index replacement.

## Renderer Profiles

- Add renderer matching after shared DLNA format tables are complete.
- Keep default behavior renderer-neutral.

## UPnP Advanced

- Add event subscription state, request validation, and UPnP control parity in dependency order.
- Cover each Event subscription request path with HTTP tests.

## Later New Features

- Track new user-facing capabilities separately from fixes to existing features.
- Require approval before adding Android-only features to desktop builds.
