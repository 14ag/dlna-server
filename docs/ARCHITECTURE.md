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
| Filesystem access | `src/fs_backend_win32.cpp` | `src/fs_backend_posix.cpp` |
| Firewall access | `src/firewall_access_win.cpp` | — (no firewall on POSIX server) |
| GUI / dialogs | `src/mainwindow.cpp`, `src/settingsdlg.cpp`, `src/logdlg.cpp`, `src/help_dialog.cpp`, `src/modal_focus.cpp`, `src/source_drop_target.cpp` | `src/fltk_gui_main.cpp` (optional, via `DLNA_ENABLE_FLTK_GUI`) |
| Access key hooks | `src/access_key_hook.cpp` | — |
| Config storage | `src/config.cpp` | `src/posix_config.cpp` |
| Logging | `src/log.cpp` | `src/posix_log.cpp` |

Both sides share `src/ssdp.h`, `src/httpserver.h`, `src/server.h`, `src/media_sources.h`, `src/config.h`, `src/netutils.h`, `src/fs_backend.h`, `src/firewall_access.h`, `src/ssdp_common.h`, and `src/http_common.h`. A change to a platform-split concern needs updates in both the shared header and both platform `.cpp` files — see `docs/KNOWN-ISSUES.md` for cases where the two sides have drifted.

Platform-independent logic lives in translation units compiled on both platforms with no `#ifdef _WIN32` branches (unless the `#ifdef` is for a non-platform difference like struct layout): `src/access_keys.cpp`, `src/bounded_thread_pool.cpp`, `src/contentdirectory.cpp`, `src/dlna_utils.cpp`, `src/http_common.cpp`, `src/ipwhitelist.cpp`, `src/media_database.cpp`, `src/media_scan_common.cpp`, `src/media_sources_common.cpp`, `src/network_sources.cpp`, `src/playlist_scan_concurrency.cpp`, `src/scan_cancellation.cpp`, `src/settings_restart.cpp`, `src/source_watcher.cpp`, `src/ssdp_common.cpp`, `src/upnp_eventing.cpp`. Note that `media_sources_common.cpp` is the sole media-scanning implementation — there is no platform-split file for this concern. Header-only utilities (`src/access_keys.h`, `src/cli_flags.h`, `src/hover_focus_state.h`, `src/input_gate.h`, `src/netutils_internal.h`, `src/scan_cancellation.h`, `src/settings_help.h`, `src/source_list_focus.h`, `src/startup_mode.h`, `src/task_group.h`) follow the same pattern. These build unchanged into both `dlna_core` static libraries defined in `CMakeLists.txt`.

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
3. Clear the media tree (`MediaSources::ResetForRescan()`).
4. Set `m_initialScanComplete = true` — the server is ready to serve Browse/Search requests immediately. Clients see whatever media items have been published so far (incremental publish model — see below).
5. Enumerate network endpoints (`EnumerateNetworkEndpoints`) to build the SSDP/HTTP `LOCATION` URLs.
6. Start `HttpServer` — if this fails, discovery never starts.
7. Start `SSDP` — on failure, the HTTP server is stopped again so the process doesn't advertise nothing and listen for nothing.
8. Set `m_running = true` and `m_initialScanInProgress = true`.
9. Launch the background scan asynchronously (`StartBackgroundScan()` — spawns a scan thread and returns immediately). The scan completion thread (`JoinBackgroundScan`) waits for the scan, then starts the watch loop.
10. `Start()` returns immediately. The UI can show the server as "running, scanning..." and Browse/Search can serve whatever items have been published so far.

The scan completion thread spawned by `StartBackgroundScan()`:
- Calls `MediaSources::Scan()` (see below).
- Once scan finishes, clears `m_initialScanInProgress`.
- Starts the watch loop (`source_watcher.cpp`), which polls source state every 5 seconds and triggers a rescan on change.

## Object model

`MediaSources` (`media_sources.h`) holds a flat `std::vector<MediaItem>` plus two index maps (`id -> index`, `parentId -> child indices`), all guarded by a `std::shared_mutex`. Object IDs come from `MediaDatabase`, which maps a canonical key (parent ID + normalized path) to a stable integer starting at `kPersistentMediaIdBase` (1,000,000) — so the same file gets the same DIDL-Lite object ID across rescans, which matters for renderers that cache browse results by ID.

Scans publish items *incrementally* — `PublishItem()` and `PublishContainer()` each acquire the mutex in exclusive mode for the duration of a single insert/index-update, then release it. Readers (`GetChildren`, `TryGetChildren`, `GetDescendants`, `GetItem`, `GetChildCounts`) take a shared lock and copy data out by value. There is no atomic swap of a complete state — readers see items as they are published. This design lets Browse/Search return partial results while a large scan (e.g. a slow FTP source) is still in progress, rather than blocking the entire startup on scan completion.

## HLS handling

HLS manifests are never expanded into per-segment DIDL items. `IsHlsManifestText()` (`network_sources.cpp`) scans fetched playlist text for a literal `#EXT-X-` tag per RFC 8216 — any Master or Media Playlist has at least one. If found, the whole manifest is registered as a single `MediaItem` with `mimeType = video/mpegurl` (`AddHlsStreamItem`), matching the DLNA renderer contract: one `<res>` element, `DLNA.ORG_OP=01` (time-seek available), no byte-range advertisement. `FetchPlaylistOnce()` guarantees the manifest is fetched exactly once and classified from that single fetch — see `docs/MEDIA-SCANNING.md`.

## Build targets (`CMakeLists.txt`)

- `dlna_core` — static library, all shared + platform logic
- `dlna-server` — the primary executable (Win32 GUI on Windows, headless CLI on POSIX)
- `dlna-server-gui-native` (POSIX only, `DLNA_ENABLE_FLTK_GUI=ON` by default) — FLTK-based native GUI, built from its own source list rather than linking `dlna_core`, so it does not share build flags with the CLI executable
- macOS: an `.app` bundle assembling both POSIX binaries plus icons via a custom target
- Linux (non-Apple): CPack `DEB` generator, desktop file, AppStream metadata, icon installation
