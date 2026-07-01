# Fix Plan: dlna-server

> 33 findings across 18 tasks in 4 phases
> Source: dlna-server-code-review-main-30-6-26.md

---

## Phase 1: Prep

_Remove unused declarations and wrappers before changing adjacent behavior._

- [ ] 1. Remove unused subtitle and XML helpers
  - **Type:** dead-code
  - **Findings:** #24, #25, #26
  - **Files:** src/dlna_utils.cpp, src/dlna_utils.h, src/contentdirectory.cpp
  - Delete `IsSubtitleExtension` declaration and definition, or wire it in before removal scope changes
  - Replace `ExtractTag(...)` calls with `ExtractTagValue(...)`
  - Delete unused `ContentDirectory::XMLEscape`
  - _Depends on: none_

- [ ] 2. Remove unused libupnp accessors
  - **Type:** dead-code
  - **Findings:** #27, #28, #29
  - **Files:** src/upnp_libupnp_win.h, src/upnp_libupnp_win.cpp
  - Delete unused `GetDeviceHandle`
  - Delete unused `LibUPnPWrapper::GetHttpAddr`
  - Delete unused duplicate `LibUPnPWrapper::NotifyUpdateId`
  - _Depends on: none_

- [ ] 3. Remove dangling GUI toolbar declaration
  - **Type:** dead-code
  - **Findings:** #31
  - **Files:** src/mainwindow.h
  - Delete `RenderToolbar` declaration with no definition
  - Verify no caller references it
  - _Depends on: none_

- [ ] 4. Decide metadata cache ownership
  - **Type:** dead-code
  - **Findings:** #30
  - **Files:** src/media_database.cpp, src/media_database.h, src/media_sources.cpp, src/posix_media_sources.cpp
  - Either call `MediaDatabase::CacheMetadata` from scan after `MediaItem` metadata is known
  - Or remove unused method and unused persisted metadata columns
  - Add a focused cache contract test for chosen behavior
  - _Depends on: none_

## Phase 2: Correctness

_Fix playlist, remote URL, protocol, and startup behavior that affects FTP-hosted playlists._

- [ ] 5. Fix playlist manifest indexing
  - **Type:** bug-logic
  - **Findings:** #1, #2, #3, #4, #5, #6
  - **Files:** src/dlna_utils.cpp, src/media_sources.cpp, src/posix_media_sources.cpp, src/media_sources.h
  - Add `.m3u` and `.m3u8` to media format recognition as playlist-compatible remote media
  - Route playlist entries matching `IsPlaylistSourcePath(...)` through recursive `ScanPlaylist`
  - Add a playlist scan depth parameter and depth-limit log
  - Log rejected media paths with extension and redacted remote URL
  - _Depends on: none_
  - _Note: Coordinate with task 15 for URL redaction helper._

- [ ] 6. Guard remote stream curl stalls
  - **Type:** bug-logic
  - **Findings:** #8, #9, #10
  - **Files:** src/network_sources.cpp
  - Set explicit `CURLOPT_USERAGENT`
  - Set FTP response and transfer options in `CreateCurlHandle`
  - Set `CURLOPT_LOW_SPEED_LIMIT` and `CURLOPT_LOW_SPEED_TIME` for streaming
  - _Depends on: none_

- [ ] 7. Fix remote DIDL exposure
  - **Type:** protocol
  - **Findings:** #17, #18
  - **Files:** src/contentdirectory.cpp, src/config.cpp
  - Force proxy URLs for non-http remote schemes
  - Force proxy URLs for credentialed remote URLs
  - Ensure DIDL `<res>` protocol and URL agree
  - _Depends on: none_
  - _Note: Coordinate with task 15 for URL redaction helper._

- [ ] 8. Fix media cache default path
  - **Type:** bug-logic
  - **Findings:** #21
  - **Files:** src/media_database.cpp
  - Detect empty default playlist directory
  - Use app data directory for `media-cache.tsv`
  - Add source or unit test covering unset default playlist
  - _Depends on: none_

- [ ] 9. Guard ASCII narrowing
  - **Type:** bug-logic
  - **Findings:** #23
  - **Files:** src/dlna_utils.cpp
  - Replace raw `static_cast<char>` truncation with UTF-8 conversion or ASCII validation
  - Add tests for non-ASCII input behavior
  - _Depends on: none_

- [ ] 10. Validate direct child containers
  - **Type:** bug-logic
  - **Findings:** #32
  - **Files:** src/contentdirectory.cpp, src/media_sources.cpp, src/media_sources.h
  - Add one locked accessor that validates object existence and `isFolder`
  - Replace double mutex acquisition in `BrowseDirectChildren`
  - Return correct error for non-container object IDs
  - _Depends on: none_

- [ ] 11. Delay readiness until first scan
  - **Type:** protocol
  - **Findings:** #33
  - **Files:** src/server.cpp, src/posix_server.cpp
  - Complete initial scan before announcing readiness or expose scan-in-progress state
  - Ensure early Browse does not return empty library as ready state
  - Add blackbox or source-contract test for startup ordering
  - _Depends on: none_

## Phase 3: Safety

_Remove credential leaks and improve remote-source change visibility._

- [ ] 12. Redact URLs in logs
  - **Type:** thread-safety
  - **Findings:** #19, #20
  - **Files:** src/network_sources.cpp, src/network_sources.h, src/media_sources.cpp, src/posix_media_sources.cpp
  - Add one `RedactUrlForLog` helper for remote URL logging
  - Replace auth, parse, listing, depth, and rejected-media URL log arguments
  - Add tests proving `user:pass@` does not appear in source log calls
  - _Depends on: none_

- [ ] 13. Surface remote source rescan behavior
  - **Type:** thread-safety
  - **Findings:** #12, #13
  - **Files:** src/source_watcher.cpp, src/server.cpp, src/posix_server.cpp, README.md
  - Include a remote content signal in `ComputeMediaSourceSignature`, or document manual rescan
  - Keep scan loop bounded when remote source is unavailable
  - Update user docs for remote playlist update behavior
  - _Depends on: none_

## Phase 4: Performance

_Reduce blocking and load hotspots after correctness and safety changes._

- [ ] 14. Parallelize remote content-length probing
  - **Type:** perf-io
  - **Findings:** #7
  - **Files:** src/network_sources.cpp, src/media_sources.cpp, src/posix_media_sources.cpp, src/httpserver.cpp, src/posix_httpserver.cpp
  - Move remote HEAD calls out of live request path where cached size exists
  - Parallelize scan-time probes with bounded concurrency
  - Preserve worker-thread availability during remote slowness
  - _Depends on: 6_

- [ ] 15. Optimize URL joining
  - **Type:** perf-algorithmic
  - **Findings:** #11
  - **Files:** src/network_sources.cpp
  - Replace `joined.str().back()` inside loop with tracked last-appended character
  - Preserve path-segment encoding behavior
  - Add a source-contract or unit test for relative playlist URL joining
  - _Depends on: none_

- [ ] 16. Align HTTP worker limits
  - **Type:** perf-io
  - **Findings:** #14, #15
  - **Files:** src/httpserver.cpp, src/posix_httpserver.cpp
  - Raise Windows threadpool maximum or split stream workers from control workers
  - Align Windows and POSIX connection capacity policy
  - Add source-contract test for configured limits
  - _Depends on: none_

- [ ] 17. Split header and stream send timeouts
  - **Type:** perf-io
  - **Findings:** #16
  - **Files:** src/httpserver.cpp, src/posix_httpserver.cpp
  - Keep short timeout for request/header phase
  - Use longer body-stream timeout for media transfer loops
  - Add test coverage for separate timeout constants
  - _Depends on: none_

- [ ] 18. Reduce album-art duplicate lookups
  - **Type:** perf-io
  - **Findings:** #22
  - **Files:** src/dlna_utils.cpp, src/media_sources.cpp, src/posix_media_sources.cpp
  - Skip case-variant duplicate candidates on case-insensitive filesystems
  - Preserve POSIX case-sensitive lookup behavior
  - Add source-contract test for Windows candidate reduction
  - _Depends on: none_
