# Media Scanning

Implemented in `src/media_sources.cpp` (Windows) and `src/posix_media_sources.cpp` (POSIX), sharing dedup/ID logic from `src/media_scan_common.cpp` and playlist/remote-fetch logic from `src/network_sources.cpp`.

## Scan lifecycle

`MediaSources::Scan()`:

1. Loads `MediaDatabase` from `media-cache.tsv` (stable IDs and prior scan-error state).
2. Builds a fresh `MediaIndexState` — a `Root` item (id 0), then one top-level container per enabled `MediaSource`, plus one for the default playlist if enabled.
3. Dispatches each source by type: `ScanPlaylist` for `.m3u`/`.m3u8`/`.pls` paths, `ScanNetworkFolder` for FTP/FTPS directory URLs, `ScanFolder` for local directories.
4. Calls `BuildIndexes()` to construct the `idToIndex` / `childrenByParent` maps, then swaps the whole state into the live `MediaSources` instance under `m_mutex` — readers never observe a half-built tree.
5. Increments `SystemUpdateID` and calls `UpnpEventManager::NotifySystemUpdateId()`, which fires GENA notifications to `ContentDirectory` subscribers.
6. Saves `MediaDatabase` back to `media-cache.tsv`.

Disabled sources, and (Windows/POSIX both) `smb://`/`smbs://` sources, are skipped with a log line rather than causing scan failure — SMB support was removed because libcurl's `smb://` backend is SMB1/CIFS-only, which most current servers refuse, and it has no directory-listing capability for any dialect.

## Extension table (`kFormats`, `dlna_utils.cpp`)

A single static table maps extension → MIME type, UPnP class, DLNA profile, and byte-seek capability, for every locally-scanned file. **`.m3u8` is deliberately absent** from this table — playlist files are routed through the playlist path before extension lookup ever runs, and adding `.m3u8` to `kFormats` would make `AddMediaFile` treat every nested HLS reference as a generic unplayable file, silently dropping it. `BuildSourceProtocolInfoList()` (used by `ConnectionManager::GetProtocolInfo`) appends the HLS protocol-info entry manually, separately from the table, for the same reason.

## Playlist fetch-once contract

`FetchPlaylistOnce(path)` (`network_sources.h`/`.cpp`) is the only sanctioned way to read a playlist or nested-playlist URL. It returns a `FetchedPlaylist{ fetchOk, isHls, text }` from a single network or filesystem read. Callers must not separately call `ReadSourceText` and then re-classify — `IsHlsManifestText()` and playlist parsing both need the exact same fetched bytes, and a second fetch against a live HTTP endpoint can return different content (especially for live HLS manifests, where segment lists roll forward).

`IsHlsManifestText(text)` scans line-by-line for a literal `#EXT-X-` tag, per RFC 8216 — every valid HLS Master or Media Playlist contains at least one, and plain M3U/IPTV compilation playlists never do (they use only `#EXTM3U` and `#EXTINF`). One match is sufficient signal.

## Nested playlist behavior

`ScanPlaylist` / `ScanPlaylistEntry` (recursion capped at depth 8, logged as `[media:scan-depth]` past that):

- If a fetch fails outright (network error, missing file), the entry is recorded as a scan error (`MediaDatabase::RecordScanError`) and skipped — it is **not** rendered as an empty folder, since an empty folder looks the same to a renderer as "correctly scanned, genuinely has no children," which hides the actual failure from anyone browsing the tree. (Top-level unreachable sources still surface as empty folders — see `docs/KNOWN-ISSUES.md`.)
- If the fetched text is itself an HLS manifest, it's registered directly under the *current* parent container as a single stream item — it is never wrapped in an extra folder, since a folder whose only possible child is one manifest with no size or duration adds a browse hop for no benefit.
- If parsing yields zero usable entries, the nested playlist is skipped entirely rather than creating an empty container.
- Otherwise, a container is created and each entry is added, recursing into further nested playlists as needed.

## Playlist formats

- **M3U/M3U8** (`ParseM3u`): `#EXTINF:<duration>,<title>` sets the title for the next URI. `#DLNA-SUBTITLE:` and `#EXTVLCOPT:sub-file=` attach a subtitle path to the next URI, resolved relative to the playlist's own location (`ResolvePlaylistSidecar`). A trailing `#EXTINF`/subtitle directive with no following URI applies to the last entry instead of being dropped.
- **PLS** (`ParsePls`): `File<n>=`/`Title<n>=` key-value pairs, indexed 1-based, capped at `kMaxPlsIndex` (10,000) to bound memory from a malicious or corrupt file.

Relative entries resolve against the playlist's own location: local paths join via `JoinLocalPath`, remote URLs join via `JoinUrl` with percent-encoding applied per path segment (`UrlEncodePathSegment`), preserving any already-percent-encoded sequences.

## Local folder scanning

`ScanFolder` recurses with a depth cap of 64. Per entry:

- Hidden, system, and reparse-point entries are skipped on Windows (`FILE_ATTRIBUTE_HIDDEN | SYSTEM | REPARSE_POINT`); dotfiles and symlinks are skipped on POSIX.
- `flatFolderStyle` (config option) either creates a container per subfolder or merges subfolder contents into the parent container directly.
- A playlist file found during a folder scan is peeked at before deciding whether to wrap it in a container — an HLS manifest never gets a container, matching the nested-playlist rule above.

## Deduplication and stable IDs (`media_scan_common.cpp`)

- `BuildDuplicateMediaKey(parentId, path, canonicalize)` — used to reject a second `AddMediaFile`/`AddHlsStreamItem` call for the same (parent, canonical-path) pair within one scan. Canonicalization resolves to an absolute, lowercased path on Windows (`GetFullPathNameW`) or a weakly-canonical path on POSIX (`fs::weakly_canonical`); remote URLs are just lowercased.
- `BuildStableMediaKey`/`BuildStableContainerKey` feed `MediaDatabase::GetOrCreateStableId`/`GetOrCreateStableContainerId`, so the same logical object gets the same numeric ID across scans, as long as its (parent, canonical-path) pair doesn't change.
- Artist/Album mirroring (`AddArtistAlbumMirrorIfPresent`, gated by the `AddArtistAlbumFolders` config option): for audio tracks and videos, derives artist/album container names from the parent/grandparent directory names and creates a second reference to the same file under `Artist/Album` containers, deduplicated the same way as any other item.

## Album art and subtitle discovery

- Album art: per-file-stem candidates (`<stem>.jpg/.jpeg/.png`) are checked first, then folder-level candidates (`folder.jpg`, `cover.jpg`, `album.jpg`, `thumb.jpg`/`.jpeg`, with case variants on POSIX). Results are cached per directory and per stem within a scan (`perStemAlbumArt`/`folderAlbumArt` maps) so a directory with many files doesn't repeat the same filesystem probes.
- Subtitles: for local video items without an explicit playlist-provided subtitle, sidecar files matching the video's stem are checked in order: `.srt`, `.vtt`, `.sub`, `.ass`, `.ssa`, `.smi`, `.txt`.
