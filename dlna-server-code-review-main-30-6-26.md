# Code Review: dlna-server

Repository: https://github.com/14ag/dlna-server (full clone, default branch)
Scope: complete source tree under `src/`, weighted toward source-serving
paths (`httpserver.cpp`, `posix_httpserver.cpp`, `network_sources.cpp`,
`media_sources.cpp`, `posix_media_sources.cpp`, `contentdirectory.cpp`,
`dlna_utils.cpp`, `server.cpp`, `source_watcher.cpp`) since this is what a
newly added FTP-hosted M3U/HLS source exercises end to end.

---

**[src/dlna_utils.cpp:45-85]** The `kFormats` extension table has no entry for `.m3u8` (or `.m3u`), even though `IsRemoteMediaUrl`/`ResolvePlaylistEntry` happily resolve remote `.m3u8` URLs as playlist entries. Any nested HLS manifest referenced from a playlist is unrepresentable by this table.
`Fix:` add a recognized entry (or dedicated code path) for `.m3u8` in the format table.

**[src/media_sources.cpp:283-293]** `AddMediaFile` silently `return`s when `IsAllowedExtension` fails for a non-empty extension, with no log line identifying the rejected path or extension. This is the exact code path that drops the FTP-hosted playlist's HLS entry, and it gives no diagnostic trail.
`Fix:` log the rejected path and extension before returning.

**[src/posix_media_sources.cpp:283-289]** Same silent, unlogged drop as the Windows build, duplicated independently in the POSIX `AddMediaFile`.
`Fix:` apply the same logging fix to the POSIX implementation.

**[src/media_sources.cpp:359-362]** `ScanPlaylist` never checks `IsPlaylistSourcePath(entry.location)` before calling `AddMediaFile`, unlike `ScanNetworkFolder` and `ScanFolder`, which both explicitly expand nested playlist files into sub-containers. A playlist entry that itself points to another playlist/manifest is only ever attempted as a flat media file.
`Fix:` route `IsPlaylistSourcePath` matches from `ScanPlaylist` into a recursive `ScanPlaylist` call, matching the sibling scan functions.

**[src/posix_media_sources.cpp:347-351]** Same missing nested-playlist expansion as the Windows `ScanPlaylist`.
`Fix:` apply the same recursion fix to the POSIX implementation.

**[src/media_sources.cpp:359]** `ScanPlaylist` has no recursion-depth guard, unlike `ScanNetworkFolder` (`depth > 8`) and `ScanFolder` (`depth > 64`).
`Fix:` add a depth parameter and limit to `ScanPlaylist` for consistency with the other scan functions.

**[src/network_sources.cpp:21,178-211]** `ProbeRemoteContentLength` performs a synchronous libcurl HEAD request with up to a 30 second timeout (`kCurlCaptureTimeoutSeconds`) directly inside both the scan path and the live request-handling path (`httpserver.cpp:411`), with no concurrency.
`Fix:` move remote content-length probing off the request-handling thread and parallelize it during scan.

**[src/network_sources.cpp:213-240]** `CurlStream` sets `CURLOPT_TIMEOUT` to 0 (no total timeout) via `CreateCurlHandle(url, errorBuffer, 0L)`, and only `CURLOPT_CONNECTTIMEOUT` (15s) is configured; there is no `CURLOPT_LOW_SPEED_LIMIT`/`CURLOPT_LOW_SPEED_TIME`. A connection that stalls after connecting can hang indefinitely and permanently occupy a worker thread/slot.
`Fix:` set a low-speed-limit/time pair on the streaming curl handle.

**[src/network_sources.cpp:162-176]** `CreateCurlHandle` sets no `CURLOPT_USERAGENT` on any request, including the FTP/HLS/CDN fetches that are the focus of this review; many CDN and hardened FTP origins vary or reject behavior based on user agent.
`Fix:` set an explicit `CURLOPT_USERAGENT`.

**[src/network_sources.cpp:162-176]** `CreateCurlHandle` configures no FTP-specific options (passive-mode handling, FTP response timeout) despite `ftp`/`ftps` being advertised supported schemes (`IsSupportedScheme`, line 402-406); behavior is entirely whatever libcurl defaults to.
`Fix:` add explicit FTP transfer options to `CreateCurlHandle`.

**[src/network_sources.cpp:108-112]** `JoinUrl` calls `joined.str()` on every loop iteration solely to inspect the last character, copying the entire accumulated string on each path segment.
`Fix:` track the last-appended character in a local variable instead of calling `.str()` in the loop.

**[src/source_watcher.cpp:118-130]** `ComputeMediaSourceSignature` explicitly skips every remote (`IsRemoteMediaUrl`) media source and the remote default playlist when hashing for change detection.
`Fix:` include a content/timestamp signal for remote sources in the watch signature, or document that remote sources require a manual rescan.

**[src/source_watcher.cpp:132-137 + src/server.cpp:104-121]** Because the signature above never changes for remote sources, the 5-second `WatchLoop` never triggers an automatic rescan when an FTP-hosted playlist (or its target HLS manifest) is updated after the initial scan.
`Fix:` same as above; surface this limitation or add remote polling.

**[src/httpserver.cpp:197-198]** `SetThreadpoolThreadMaximum(m_threadPool, 8)` caps the entire Windows HTTP server — SOAP control requests and every concurrent media stream — at 8 worker threads. A handful of long-lived remote/HLS streams can starve Browse/Search control-plane requests.
`Fix:` raise the thread maximum or separate streaming workers from control-plane workers.

**[src/httpserver.cpp:197-198 vs src/posix_httpserver.cpp:39]** The Windows build caps concurrent connection handling at 8 threads (`SetThreadpoolThreadMaximum`) while the POSIX build caps at 64 (`kMaxClientThreads`), an 8x capacity disparity between platforms for identical workloads.
`Fix:` align the concurrency limit between the two builds.

**[src/httpserver.cpp:58-62 + src/posix_httpserver.cpp:90-94]** `SetSocketTimeouts` applies a fixed 10 second `SO_SNDTIMEO` for the lifetime of the connection, including the chunked streaming write loop. A renderer that pauses playback long enough for the OS send buffer to fill will have its connection silently torn down after 10s.
`Fix:` use a longer or per-phase send timeout for the body-streaming loop, distinct from the header-write timeout.

**[src/contentdirectory.cpp:286-287]** When `cfg.proxyStreams` is false (the default, `config.cpp:191`), the raw remote URL (`it.path`) is placed directly into the DIDL `<res>` element, while `protocolInfo` (`ItemProtocolInfo`) always declares `http-get` regardless of the URL's actual scheme. An `ftp://` or `smb://` resource is advertised under an `http-get` protocol token it cannot satisfy.
`Fix:` force proxying (or exclude the item) for any resource whose URL scheme is not http/https.

**[src/contentdirectory.cpp:286-287]** Because `proxyStreams` defaults to false, any FTP/SMB credentials embedded in a media source URL (the in-app hint at `mainwindow.cpp:193` literally suggests `ftp://user:pass@server:21/media`) are sent in plaintext inside the `<res>` element to every UPnP/DLNA client that browses the library.
`Fix:` strip or redact userinfo from URLs before placing them in DIDL output, or force proxying for credentialed URLs.

**[src/network_sources.cpp:204]** On a 401/403/login-denied response, the full URL — including any embedded `user:pass@` credentials — is written to the log via `LogPrint(L"...authentication failed for %ls", url.c_str())`.
`Fix:` redact userinfo from the URL before logging.

**[src/network_sources.cpp:196,367,509]** Several other `LogPrint` call sites (truncation warnings, directory-listing-unsupported warnings, recursion-depth warnings) also log full source URLs verbatim, repeating the same credential-leak risk.
`Fix:` route all URL logging through a single credential-redacting helper.

**[src/media_database.cpp:106-111]** `DefaultDatabasePath` derives the cache file's directory from `AppConfig.GetDefaultPlaylistPath()`; if that path is unset, `dir` is an empty string and `media-cache.tsv` is written relative to the process's current working directory instead of a fixed location.
`Fix:` fall back to a dedicated app-data directory when no default playlist path is configured.

**[src/dlna_utils.cpp:399-414]** `BuildAlbumArtCandidateNames` hardcodes separate case variants (`folder.jpg`/`folder.JPG`/`Folder.jpg`, etc.). On the Windows scan path (`GetFileAttributesW`, case-insensitive), these variants are functionally identical and only add redundant filesystem lookups per scanned file.
`Fix:` skip the case-variant duplicates when running on a case-insensitive filesystem.

**[src/dlna_utils.cpp:108-115]** `NarrowAscii` converts `wchar_t` to `char` via a raw `static_cast<char>` per character with no validation, silently truncating/corrupting any code point outside ASCII.
`Fix:` guard that the input is ASCII-only before truncating, or use a proper UTF-8 narrowing conversion.

**[src/dlna_utils.cpp:346-351]** `IsSubtitleExtension` is declared (`dlna_utils.h:43`) and defined but never called anywhere in the codebase.
`Fix:` remove the function or wire it into the subtitle-detection path it appears intended for.

**[src/contentdirectory.cpp:325-327]** `ContentDirectory::XMLEscape` duplicates `XMLEscapeUtf8(WideToUtf8(...))` (already inlined directly via the free function `EscapeWide` at line 173-175) and is never called anywhere.
`Fix:` remove the unused member function.

**[src/contentdirectory.cpp:141-143]** `ExtractTag` is a one-line pass-through wrapper around `ExtractTagValue` with an identical signature; every call site in the file uses `ExtractTag`, and `ExtractTagValue` is never called directly.
`Fix:` delete the wrapper and call `ExtractTagValue` directly.

**[src/upnp_libupnp_win.h:20]** `GetDeviceHandle` is an inline accessor with no callers anywhere in the codebase.
`Fix:` remove the unused accessor.

**[src/upnp_libupnp_win.cpp:117]** `LibUPnPWrapper::GetHttpAddr` is defined but never called.
`Fix:` remove the unused method.

**[src/upnp_libupnp_win.cpp:121]** `LibUPnPWrapper::NotifyUpdateId` is defined but never called; the actually-used system-update-ID notification path is the separate `UpnpEventManager::NotifySystemUpdateId` (`upnp_eventing.cpp:199`).
`Fix:` remove the unused, confusingly-named duplicate method.

**[src/media_database.cpp:200-206]** `MediaDatabase::CacheMetadata` is defined (`media_database.h:32`) but never called from anywhere in the codebase; the metadata columns it would populate (title/mime/upnpClass/codec) are loaded and persisted but never written by a live scan.
`Fix:` wire `CacheMetadata` into the scan path or remove it along with the now-pointless metadata columns.

**[src/mainwindow.h:29]** `RenderToolbar` is declared on the class but has no definition anywhere in `mainwindow.cpp`; it is currently unused, and any future caller would fail to link.
`Fix:` remove the dangling declaration.

**[src/contentdirectory.cpp:631-633]** `BrowseDirectChildren` calls `AppMedia.GetItem(objId)` purely to check existence, then immediately calls `AppMedia.GetChildren(objId)`, taking the `MediaSources` mutex twice for one logical operation, and never checks `isFolder` before treating the object as a container.
`Fix:` add a single locked accessor that validates and fetches children together, including an `isFolder` check.

**[src/server.cpp:211-231]** `HttpServer::Get().Start()` begins accepting connections and announcing via SSDP/UPnP before `StartBackgroundScan()`'s thread has populated the media index, so an early Browse request (including from a renderer that connects immediately on discovery) sees an empty library with no signal that a scan is still in progress.
`Fix:` hold off announcing readiness until the first scan completes, or surface scan-in-progress state to clients.
