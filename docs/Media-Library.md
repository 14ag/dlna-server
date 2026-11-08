# Media Library Deep Dive

This page explains how the server turns sources into a catalog: how a scan works, how object IDs stay stable, how playlists, HLS and remote sources are handled, and how a rescan avoids breaking players that are still reading the old catalog.

Files: `media_sources_common.cpp`, `media_scan_common.cpp`, `media_database.cpp`, `network_sources.cpp`, `playlist_scan_concurrency.cpp`, `fs_backend*.cpp`, `source_watcher.cpp`, `dirwatch_*.cpp`.

## The catalog

`MediaSources` holds three structures behind one `shared_mutex`:

| Structure | Purpose |
|---|---|
| `m_items` (vector of `MediaItem`) | All items and containers. |
| `m_idToIndex` | Object ID to position in `m_items`. |
| `m_childrenByParent` | Parent ID to a list of positions. |

The root container has ID `0`, title `Root`, class `object.container.storageFolder`. Source containers are its children. `GetItem` and `TryGetChildren` return copies, so a reader never holds a reference into a vector that a scan may grow.

A `MediaItem` carries: ID, parent ID, path or URL, title, MIME type, UPnP class, size, subtitle path, album art path and type, `dcDate`/`rawDateMs` (HLS only), and the scan generation.

## A scan, step by step

`MediaSources::Scan()`:

1. Take a `ConfigSnapshot`. Open `MediaDatabase`, load `media-cache.tsv`, call `BeginScanPass()`.
2. Build one job per entry in `effectiveMediaSources`:
   - `smb://` and `smbs://`: skip with `[media:smb-removed]`.
   - Anything that is not a playlist and not an `ftp`/`ftps` URL must exist on disk (`FsExists`). Otherwise record a scan error `Source unavailable` and skip. **This means a direct `http` or `https` media URL is skipped as missing.** HTTP and HTTPS work only through playlists.
   - Publish a container for the source under the root.
3. Add the *Default playlist* job when it is on, the file exists, and no command-line sources are active.
4. Raise `m_publishBatchDepth`. Submit each job to `SourceScanPool`. Wait on a `TaskGroup`. Lower the depth.
5. Raise `SystemUpdateID` and send one event.
6. If the scan was cancelled: log, leave `media-cache.tsv` unchanged, return. The catalog is not swept.
7. `SweepStaleEntries()`. `PruneUntouched()` on the database. `Save()`.

### How a job scans its source

| Source | Method |
|---|---|
| `.m3u`, `.m3u8`, `.pls` (local or remote) | `ScanPlaylistTree` |
| `ftp://` or `ftps://` | `ScanNetworkFolder` (depth 0) |
| A single local file | `AddMediaFile` directly |
| A local folder | `ScanFolder` (depth 0) |

`ScanFolder` lists one directory through `FsListDirectory`. For each entry:

- Directory: with **Flat folders style**, recurse into the same parent. Otherwise add a container (`FindOrAddContainer`) and recurse into it.
- Playlist file: `ScanPlaylistTree`.
- Other file: `AddMediaFile`.

`FsListDirectory` skips hidden, system and reparse-point entries on Windows. On Linux it skips names that start with `.`, symbolic links, and entries with no read bit. It returns `false` if the directory cannot be opened. The scan then records `Folder unavailable` and moves on.

## Supported file types

From `kFormats` in `dlna_utils.cpp`:

| Kind | Extensions |
|---|---|
| Video | `mp4 m4v mkv webm avi divx mov mpg mpeg mpe vob ts m2ts mts wmv flv 3gp 3g2` |
| Audio | `mp3 flac m4a aac wav wma ogg oga opus aiff aif ac3 dts` |
| Image | `jpg jpeg png gif bmp tif tiff webp` |
| Playlist | `m3u m3u8 pls` |

The file pickers list video, audio and playlist extensions only (`media_source_file_types.h`). Images in a scanned folder are served. A remote URL with no extension is treated as an MP3 stream (`audio/mpeg`, music track). A remote URL with an unsupported extension is skipped with `[media:reject-extension]`.

### Per-format data

Each format has a MIME type, UPnP class, optional DLNA profile, and a byte-seek flag. Examples: `.mp4` is `video/mp4` with profile `AVC_MP4_MP_SD_AAC_MULT5`; `.mp3` is `audio/mpeg` with `MP3`; `.mkv` is `video/x-matroska` with no profile. Images have byte-seek off. A hash map built on first use turns the extension lookup into one hash probe instead of a scan of 35 rows.

## Items

`AddMediaFile` steps:

1. Extension to format. Unknown: return.
2. Build the stable key and get an ID from the database (or a scratch ID).
3. Get the size: local `FsFileSize`, remote `ProbeRemoteContentLength`. A local file whose size cannot be read is dropped.
4. Choose the title.
5. Find a subtitle.
6. Find album art.
7. Insert the duplicate key. If it already exists, return.
8. Publish the item and mark the key as scanned.
9. If **Add artist/album folders** is on, publish a mirror item.

### Titles

`BuildDisplayTitleForMediaFile` checks in this order:

1. **Show file names instead of titles** is on: the full file name, with extension.
2. A non-empty title from the playlist (`#EXTINF` or `.pls` `Title`): that title, unchanged.
3. Otherwise: the file name without extension, followed by the extension. The extension is written in lower case, so `Song.MP3` shows as `Song.mp3`. The extension stays visible for players that guess the type from the title.

The server reads no tags from the files. Titles never come from ID3 or similar metadata. The **Sort by title** option sorts these titles.

### Subtitles and art

Both lookups read the folder **once** (`FsListDirectory`) and keep the lower-cased names in `MediaIndexState::folderFileNames`. After that, a lookup is a hash probe. Without it, each file would cost one `stat` per possible name.

| Kind | Candidates |
|---|---|
| Subtitle (video only, local only) | Same stem plus `.srt .vtt .sub .ass .ssa .smi .txt`, in this order |
| Art, per file | Same stem plus `.jpg`, `.jpeg`, `.png` |
| Art, per folder | `folder.jpg`, `cover.jpg`, `album.jpg`, `thumb.jpg`, `thumb.jpeg` |

A playlist can name its own subtitle (`#DLNA-SUBTITLE:` or `#EXTVLCOPT:sub-file=`). That wins over the folder search. Results go into small per-source caches (`perStemAlbumArt`, `folderAlbumArt`). The locks protect only the map access. A parallel worker may repeat the same disk read. The result is the same, so the write is harmless and the code accepts the duplicate cost over a long lock.

### Artist and album mirror

When **Add artist/album folders** is on, an audio or video file at `…/Artist/Album/file` also appears under `Source/Artist/Album`. The grandparent folder name is the artist and the parent folder name is the album. If either is empty, no mirror is made. Remote items are not mirrored. The mirror gets its own ID from its own key.

## Object IDs

### Keys

| Object | Key |
|---|---|
| Item | `media\n<parentId>\n<lower-cased path>` |
| Container | `container\n<parentId>\n<title>\n<lower-cased path>` |

The same file under two parents (for example in the mirror) has two keys and two IDs.

> **Known limit.** Paths are lower-cased on every platform. On Linux, two files that differ only by letter case in the same folder produce the same key. The second is dropped as a duplicate.

### The database

`media-cache.tsv` is a tab-separated text file.

```
# dlna-server media-cache.tsv v1
1000000	<escaped key>	<escaped error text>
```

- IDs start at `1000000` (`kPersistentMediaIdBase`). `\`, tab, CR and LF in fields are escaped.
- `GetOrCreateStableId` returns the existing ID or issues one. It marks the key as "touched in this pass".
- `PruneUntouched()` removes records the pass did not touch, and puts their IDs on a free list. New keys reuse freed IDs (last in, first out) before the counter grows. The free list is not saved. It bounds growth inside one run.
- If the counter reaches `INT_MAX`, the next ID is `INT_MAX` and the counter resets to the base. The log says so. Restart the app to reset the ID space.
- `RecordScanError` stores a short reason for a source or folder that failed. `MarkScanSuccess` clears it.
- `Save()` writes `<file>.tmp` and then renames it over the old file. On Windows the rename uses `MOVEFILE_WRITE_THROUGH`.
- Without a database, items get negative "scratch" IDs from one shared counter. They are valid only for the current run.

Stable IDs matter because players keep object IDs in bookmarks and resume lists.

## Rescans without breaking readers

A rescan never empties the catalog. The sequence:

1. `ResetForRescan()` raises `m_currentGeneration` and makes sure the root exists. Old items stay.
2. Each publish tags the item with the current generation. If an item with that ID already has the current generation, the publish does nothing.
3. After a complete, uncancelled scan, `SweepStaleEntries()` keeps only items of the current generation and rebuilds the index maps.

An HTTP request for an item that was not yet republished still finds it. An item that really disappeared leaves the catalog at the sweep. The `--print-media-item-stays-resolvable-during-rescan` hook tests this.

`SystemUpdateID` goes up on every publish and every reset. Events are sent when the batch depth is zero, and once when a scan ends.

## Concurrency in the scan

| Pool | Size | Runs |
|---|---|---|
| `SourceScanPool` | `min(2 × cores, 64)` | One job per source. Mostly waiting on disk or network. Twice the core count lets more sources overlap. |
| `PlaylistScanPool` | 20 workers, queue 200 | One task per playlist node. `Submit` blocks when the queue is full, so a huge playlist tree cannot fill memory. |

Each source has a `PlaylistScanContext`: the config snapshot, a `MediaIndexState`, an `AdaptiveConcurrencyLimiter`, a `TaskGroup`, and a queue of pending nodes. `RunPlaylistDispatcher` takes a node from the queue, waits for a limiter slot, and submits the node to the pool. It ends when the queue is empty and no task is pending.

The limiter starts at 1 and grows with the number of nested playlists found:

```
concurrency(n) = clamp(1 + floor(19 × ln(n) / ln(200)), 1, 20)   for n > 1; 1 for n ≤ 1
```

`n = 200` gives 20. A source with one playlist stays sequential. A source with many nested playlists (an IPTV list that points at other lists) runs up to 20 in parallel.

The task's leave guard runs before the dispatcher is notified. The notification reads the pending count. If it ran first, the dispatcher could see an old count and never wake.

## Playlists

### Parsing

| Format | Rules |
|---|---|
| M3U, M3U8 | Lines starting with `#EXTINF:` set the title (text after the first comma). `#DLNA-SUBTITLE:` and `#EXTVLCOPT:sub-file=` set a subtitle for the next entry. Other `#` lines are ignored. Every other line is an entry. A title or subtitle after the last entry applies to that entry. |
| PLS | `FileN`, `TitleN`. `N` must be 1 to 10,000. Keys are case-insensitive. Entries with no file are dropped. |

A UTF-8 byte order mark is removed. A file is recognized as a playlist only if it starts with `#EXTM3U` (M3U family) or `[playlist]` (`.pls`). This rejects error pages and redirect stubs that carry a `.m3u8` name.

### Resolving entries

- A remote URL or an absolute local path is used as it is.
- A relative entry in a remote playlist is resolved against the playlist URL. RFC 3986 rules: the query and fragment are split off, path segments are joined and `.` and `..` are removed, then the query and fragment are added back **unchanged**. Signed CDN URLs depend on those exact bytes.
- A relative entry in a local playlist is joined to the playlist's folder.
- An entry that is itself a playlist becomes a child node, up to depth 8. Its container uses the playlist's title.

### HLS

A fetched text that contains a line starting with `#EXT-X-` is an HLS manifest (RFC 8216). The server does not expand it. It adds **one** item with MIME `video/mpegurl` and the title from the playlist or the file stem. When a player opens it, the server fetches the manifest again and rewrites each URI to an absolute URL (`RewriteHlsManifestUrisToAbsolute`). A plain playlist of separate clips uses only `#EXTM3U` and `#EXTINF`, so the `#EXT-X-` test tells the two apart.

Each playlist is fetched once. `FetchPlaylistOnce` returns the text and whether it is HLS, so the scan never reads a remote playlist twice.

## Remote sources

### libcurl use

- One shared handle (`CURLSH`) shares DNS cache, TLS session cache and connections between threads. Each data type has its own mutex.
- Easy handles have: follow redirects, fail on HTTP error, no signals, user agent `DLNA-Server/<version>`, connect timeout 15 s, FTP response timeout 30 s, EPSV on, and a progress callback that aborts when the scan is cancelled.
- Text fetches (playlists, FTP listings, size probes): total timeout 30 s, a 4 MiB limit on the body. A longer body counts as a failed fetch and is logged (`[remote:parse]`).
- Streaming (playback): no total timeout, abort if the speed stays at or below 1 byte/s for 30 s.
- Log lines hide the user name and password in a URL (`RedactUrlForLog`).

### FTP listing

`ListRemoteDirectory` accepts `ftp` and `ftps` URLs only. It uses a name-only listing request and also understands detailed lines:

- Unix style: first character `d` is a directory, `-` is a file. The name starts after eight fields.
- DOS style: `MM-DD-YY HH:MM <DIR> name` or `MM-DD-YY HH:MM size name`.
- A trailing `/` also marks a directory.

Names are URL-encoded when they are joined to the base URL. HTTP directory pages are not parsed.

### Size probe

`ProbeRemoteContentLength(url)` makes a header-only request.

- Result cache: 2,000 entries, 5 minutes each. At the limit the 64 entries that expire soonest are removed. Removing 64 at once means the next 63 inserts need no removal.
- At most four probes run at once (`AdaptiveConcurrencyLimiter`).
- A result of 0 means "unknown". It is cached too.

## Keeping the catalog current

| Mechanism | Trigger | Condition |
|---|---|---|
| Manual | **Scan**, **F5**, or add and remove actions | Always |
| Folder watch | `inotify` (Linux) or `ReadDirectoryChangesW` (Windows). Debounced to one event per 2 s. | **Background scan** on |
| Signature poll | Every 60 s. Every 15 min when the folder watch works. | **Background scan** on |

The poll computes a 64-bit FNV-1a hash over every path, modification time and entry type below each local source, to depth 64. Symbolic links are hashed as "skipped", not followed. If the hash differs from the last one, the server scans again. Remote sources are not hashed or watched.

The Linux watcher adds a watch for every directory, including new ones, to depth 64. The Windows watcher asks the system to watch each source folder and everything below it. If the system limit on watches (`fs.inotify.max_user_watches`) is reached, the log says so and that path falls back to the poll.

A new scan from any trigger goes through `Server::Rescan` or `StartBackgroundScan`. A new background scan waits for the running one to finish. The Windows and GTK windows also ignore a scan request while one runs.

## Limits recap

| Limit | Value |
|---|---|
| Playlist nesting | 8 |
| Local folder depth | 64 |
| FTP folder depth | 8 |
| `.pls` entries | 10,000 per file |
| Playlist text | 4 MiB |
| Size cache | 2,000 entries, 5 min |
| Parallel size probes | 4 |
| Parallel playlist tasks | 20 |
