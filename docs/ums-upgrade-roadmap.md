# UMS-Inspired Upgrade Roadmap

This roadmap is clean-room and MIT-safe. Universal Media Server was used as a behavioral benchmark only: its public README, renderer profile concepts, changelog themes, and high-level architecture informed this backlog. GPL implementation code must not be copied into this project.

## No New Dependencies

- HTTP correctness: shared header parsing, strict numeric parsing, valid byte ranges, `HEAD` support, `416` responses with `Content-Range`, empty-file handling, bad `Content-Length` rejection, and IPv6-safe host URLs.
- DLNA Browse hardening: SOAP faults for malformed requests, `GetSearchCapabilities`, `GetSortCapabilities`, correct `TotalMatches`, deterministic sort handling, and consistent XML escaping.
- Media scanning: shared extension-to-MIME table, safe hidden/system/unreadable file filtering, symlink/reparse loop avoidance, companion subtitle detection on every backend, and natural ordering.
- Code efficiency: move duplicated Windows/POSIX parsing and media-format rules into shared helpers, keep platform code focused on sockets/filesystem APIs, and grow tests around shared behavior first.
- Logging/tests: keep logs concise but actionable, add blackbox smoke checks for description XML, Browse, range `206/416`, `HEAD`, and subtitle URLs.

## Optional FFmpeg/ffprobe

- Add optional `ffprobe` discovery and cache media duration, bitrate, codecs, dimensions, audio channels, subtitle tracks, and container metadata.
- Add thumbnail extraction for video/image/audio art with cached output and DLNA album-art/resource links.
- Use metadata to improve DLNA protocol info and choose direct-stream vs future transcode paths.
- Keep direct streaming default when FFmpeg is unavailable.

## Index/Database

- Replace volatile scan IDs with stable IDs based on canonical path plus generation/version.
- Add incremental scan support for changed folders instead of full rescans.
- Store media metadata, thumbnails, resume/bookmark data, last modified state, and scan errors in a lightweight local database.
- Add subtree search and fast folder browsing for large libraries.

## Renderer Profiles

- Detect renderers from `User-Agent`, extra HTTP headers, and UPnP device details.
- Add renderer-specific DLNA flags, MIME substitutions, subtitle delivery quirks, seek behavior, folder limits, and max resolution/bitrate preferences.
- Track connected renderers with allow/block state and concise diagnostics for unknown devices.

## UPnP Advanced

- Implement `Search` with useful criteria mapping once the index exists.
- Add ContentDirectory eventing for `SystemUpdateID` changes.
- Add Samsung-compatible feature/bookmark actions behind safe defaults.
- Add ConnectionManager action responses that include richer source protocol info.

## Later New Features

- Web settings/player UI.
- Push playback and renderer remote control.
- FFmpeg-based transcoding/remuxing with bandwidth and device profiles.
- Playlists, online feeds, archive browsing, and virtual folders.
- Accounts, per-device permissions, and external network access controls.

## First Patch Acceptance

- No new user-facing feature beyond hardening existing streaming/browsing/scanning behavior.
- Shared helper module owns duplicated parsing, range, MIME, subtitle MIME, and natural-sort logic.
- Windows and POSIX both support hardened HTTP streaming, `HEAD`, and subtitle serving.
- Tests cover roadmap content and source-level behavior contracts.
