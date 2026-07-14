# Architecture

## Platform split

The codebase maintains two parallel implementations behind the same headers:

| Concern | Windows | POSIX |
|---|---|---|
| Entry point | `src/main.cpp` (`wWinMain`, GUI) | `src/posix_main.cpp` (CLI), `src/fltk_gui_main.cpp` (optional GUI) |
| Server lifecycle | `src/server.cpp` | `src/posix_server.cpp` |
| SSDP | `src/ssdp.cpp` | `src/posix_ssdp.cpp` |
| HTTP server | `src/httpserver.cpp` | `src/posix_httpserver.cpp` |
| Network enumeration | `src/netutils.cpp` | `src/posix_netutils.cpp` |
| Media scanning | `src/media_sources.cpp` | `src/posix_media_sources.cpp` |
| Config storage | `src/config.cpp` | `src/posix_config.cpp` |
| Logging | `src/log.cpp` | `src/posix_log.cpp` |

Both sides share `src/ssdp.h`, `src/httpserver.h`, `src/server.h`, `src/media_sources.h`, `src/config.h`, and `src/netutils.h`. A change to shared behavior belongs in the shared header and both `.cpp` files — see `docs/KNOWN-ISSUES.md` for cases where the two sides have drifted.

Platform-independent logic lives in translation units with no `#ifdef _WIN32` branches: `src/dlna_utils.cpp`, `src/network_sources.cpp`, `src/media_database.cpp`, `src/media_scan_common.cpp`, `src/source_watcher.cpp`, `src/contentdirectory.cpp`, `src/upnp_eventing.cpp`, `src/ipwhitelist.cpp`. These build unchanged into both `dlna_core` static libraries defined in `CMakeLists.txt`.

## Runtime components

```
Server (server.h)
  ├─ HttpServer     — TCP accept loop, per-connection request handling
  ├─ SSDP           — UDP multicast advertise + M-SEARCH responder
  ├─ MediaSources    — in-memory content tree, background scan thread
  ├─ ContentDirectory — SOAP Browse/Search, DIDL-Lite generation
  ├─ UpnpEventManager — GENA SUBSCRIBE/UNSUBSCRIBE, change notifications
  ├─ IPWhitelist     — per-request source-address filtering
  └─ Config          — config.ini load/save, thread-safe snapshot
```

`Server::Start()` in `server.cpp` / `posix_server.cpp` is the composition root. Startup order matters:

1. Load and validate config (`Config::Snapshot`), load the IP whitelist.
2. On Windows, request firewall access if not already granted (`EnsureFirewallAccess`).
3. Enumerate network endpoints (`EnumerateNetworkEndpoints`) to build the SSDP/HTTP `LOCATION` URLs.
4. Start `HttpServer` — if this fails, discovery never starts.
5. Start `SSDP` — on failure, the HTTP server is stopped again so the process doesn't advertise nothing and listen for nothing.
6. Run the initial media scan synchronously (`StartBackgroundScan()` + `JoinBackgroundScan()`) before marking `m_initialScanComplete`. SOAP `Browse`/`Search` reject requests with UPnP error 710 until this flag is set.
7. Start the watch loop, which polls source state every 5 seconds and triggers a rescan on change (`source_watcher.cpp`).

## Object model

`MediaSources` (`media_sources.h`) holds a flat `std::vector<MediaItem>` plus two index maps (`id -> index`, `parentId -> child indices`). Object IDs come from `MediaDatabase`, which maps a canonical key (parent ID + normalized path) to a stable integer starting at `kPersistentMediaIdBase` (1,000,000) — so the same file gets the same DIDL-Lite object ID across rescans, which matters for renderers that cache browse results by ID.

A scan builds a fresh `MediaIndexState` off to one side, then swaps it into the live `m_items`/`m_idToIndex`/`m_childrenByParent` under `m_mutex` (`SwapScannedState`). Readers never see a partially built tree.

## HLS handling

HLS manifests are never expanded into per-segment DIDL items. `IsHlsManifestText()` (`network_sources.cpp`) scans fetched playlist text for a literal `#EXT-X-` tag per RFC 8216 — any Master or Media Playlist has at least one. If found, the whole manifest is registered as a single `MediaItem` with `mimeType = video/mpegurl` (`AddHlsStreamItem`), matching the DLNA renderer contract: one `<res>` element, `DLNA.ORG_OP=01` (time-seek available), no byte-range advertisement. `FetchPlaylistOnce()` guarantees the manifest is fetched exactly once and classified from that single fetch — see `docs/MEDIA-SCANNING.md`.

## Build targets (`CMakeLists.txt`)

- `dlna_core` — static library, all shared + platform logic
- `dlna-server` — the primary executable (Win32 GUI on Windows, headless CLI on POSIX)
- `dlna-server-gui-native` (POSIX only, `DLNA_ENABLE_FLTK_GUI=ON` by default) — FLTK-based native GUI, built from its own source list rather than linking `dlna_core`, so it does not share build flags with the CLI executable
- macOS: an `.app` bundle assembling both POSIX binaries plus icons via a custom target
- Linux (non-Apple): CPack `DEB` generator, desktop file, AppStream metadata, icon installation
