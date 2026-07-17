# HTTP Server

Implemented in `src/httpserver.cpp` (Windows) and `src/posix_httpserver.cpp` (POSIX). Both expose the same route table; keep them in sync when adding an endpoint.

## Listener setup

- Two listen sockets, one per address family (`AF_INET`, `AF_INET6`), both bound to the configured port. If one family fails to bind, the server still starts on the other and logs a warning.
- Windows: `SO_EXCLUSIVEADDRUSE` is set on the listen socket instead of `SO_REUSEADDR`. On Windows, `SO_REUSEADDR` allows a second process to silently bind over a port that's already in use, with no defined winner for incoming connections. `SO_EXCLUSIVEADDRUSE` makes a second bind attempt fail loudly instead.
- POSIX: `SO_REUSEADDR` (and `SO_REUSEPORT` where available) — POSIX reuse semantics don't have the same silent-hijack behavior, so this is safe there and helps with fast restarts.
- IPv6 sockets set `IPV6_V6ONLY` so the two listeners don't compete for the same traffic.

## Connection handling

- Windows: `CreateThreadpoolWork` per accepted connection, pool sized 4–64 threads (`SetThreadpoolThreadMinimum`/`Maximum`).
- POSIX: one `std::thread` per connection, capped at `kMaxClientThreads` (64). Past the cap, new connections get `503 Service Unavailable` and are closed immediately. Finished threads are reaped opportunistically in the accept loop (`ReapFinishedClientThreads`).
- Both sides check `IPWhitelist::Get().IsAllowed(clientIp)` right after `accept()`, before any request is read, and respond `403 Forbidden` if the address isn't allowed.
- HTTP/1.1 keep-alive is honored based on the `Connection` header and, absent one, the request's HTTP version (`ShouldKeepAlive`).

## Host header resolution

The server resolves the effective host URL per request for generating `<res>` URLs in DIDL-Lite responses and for `URLBase`:

1. Reads the `Host` header from the request.
2. If `Host` is a loopback address (`localhost`, `127.0.0.1`, `[::1]`, `[0:0:0:0:0:0:0:1]`), it is replaced with `GetRoutableHostUrl()` — which picks the best non-loopback endpoint for the configured port and interface allowlist. This ensures that a DLNA renderer on another machine receives a reachable `<res>` URL even if the original Browse request came through a local proxy.
3. If `Host` was empty or loopback-replaced, falls back to `getsockname()` on the client socket, then to `clientIP:port`.
4. The resolved `hostUrl` is passed to `ContentDirectory` for DIDL-Lite generation and used as the `Host` value in upstream proxy requests.

All media responses (`/media/`, `/subtitle/`, `/albumart/`) include a `transferMode.dlna.org: Streaming` header, per the DLNA guideline for HTTP streaming.

## Second-instance source bypass (Windows)

When a second `DLNA Server.exe` instance is launched with `--source` and an existing main window is already running (`FindWindowW("dlna-server_Main")`), the second instance sends the source list to the first instance via `WM_COPYDATA` (`kCopyDataSourceReplace = 1`) and exits immediately. The first instance receives the message in `MainWindow::HandleMessage`, sets the runtime source override, and restarts the server with the new sources. This avoids having to stop the GUI and relaunch.

## Routes

| Method | Path | Purpose |
|---|---|---|
| GET/HEAD | `/description.xml` | UPnP device description; `<URLBase>` is built from the request's resolved `Host` |
| GET/HEAD | `/ContentDirectory.xml` | ContentDirectory:1 SCPD |
| GET/HEAD | `/ConnectionManager.xml` | ConnectionManager:1 SCPD |
| GET/HEAD | `/icons/server_icon_{48,120,256}.png` | Device icons (Win32 resource on Windows, files under `DLNA_RESOURCE_DIR` on POSIX) |
| GET/HEAD | `/media/{id}` | Streams a `MediaItem`'s content — local file, or proxied remote URL |
| GET/HEAD | `/subtitle/{id}` | Streams the item's companion subtitle file, local or remote |
| GET/HEAD | `/albumart/{id}` | Streams discovered album art for the item |
| POST | `/upnp/control/content_directory` | SOAP control — `Browse`, `Search`, `GetSystemUpdateID`, `GetSearchCapabilities`, `GetSortCapabilities` |
| POST | `/upnp/control/connection_manager` | SOAP control — `GetProtocolInfo`, `GetCurrentConnectionIDs`, `GetCurrentConnectionInfo` |
| SUBSCRIBE/UNSUBSCRIBE | `/upnp/event/content_directory`, `/upnp/event/connection_manager` | GENA eventing, delegated to `UpnpEventManager` |

Anything else returns `404 Not Found`. Malformed requests (bad method, bad `Content-Length`, oversized SOAP body) return `400 Bad Request`.

## `/media/{id}` streaming behavior

1. Look up the `MediaItem` by ID (`MediaSources::GetItem`). Unknown ID → `404`.
2. If `item.mimeType == "video/mpegurl"` (an HLS manifest), byte-range and `Content-Length` handling is skipped — the manifest has no meaningful fixed length, and advertising one breaks adaptive playback. `Accept-Ranges: none` is sent.
3. Otherwise, for local files: `Content-Length` is exact from the filesystem, `Accept-Ranges: bytes`, and `Range` headers are honored (`ParseHttpRangeHeader`), including `416 Range Not Satisfiable` for out-of-bounds requests.
4. For remote (proxied) files: size comes from a cached probe (`ProbeRemoteContentLength`) or the item's stored `sizeBytes`. If size is unknown and the `User-Agent` is empty or starts with `SEC_HHP_` (Samsung TVs), a placeholder `Content-Length: 1073741824` (1 GiB) is sent instead of omitting the header — see `docs/KNOWN-ISSUES.md` for why this is intentional, not a bug.
5. `contentFeatures.dlna.org` is computed per-extension via `BuildContentFeaturesForExtension`, except for HLS items, which get the fixed HLS feature string (`DLNA.ORG_OP=01;DLNA.ORG_CI=0;...`).
6. `Icy-MetaData: 1` is forwarded to the upstream request if the client sent it, and any `Icy-*` response headers from the upstream are relayed back — this keeps internet-radio metadata working through the proxy.

## SOAP handling (`contentdirectory.cpp`)

- The SOAP action name is extracted by walking the XML manually (`ExtractSoapActionName`), skipping comments and CDATA, rather than using a full XML parser. Malformed XML returns SOAP fault 400.
- `Browse` and `Search` return SOAP fault 710 ("Initial scan in progress") until `Server::IsInitialScanComplete()` is true, so a renderer that connects during startup gets a well-formed retryable error instead of an empty tree.
- Filter strings (`dc:title`, `upnp:class`, `upnp:albumArtURI`, `res`, `dc:date`) control which DIDL-Lite fields get emitted — see `BuildDIDL` in `contentdirectory.cpp`.
- Sort criteria support `dc:title` and `upnp:class`, ascending or descending (`-` prefix); anything else falls back to the configured default (`SortByTitle`).

## GENA eventing (`upnp_eventing.cpp`)

- Subscriptions are capped at `kMaxUpnpSubscriptions` (64); past that, the nearest-to-expire subscription is evicted to make room, and this is logged.
- `NotifySystemUpdateId` fires after every scan; only subscribers to `/upnp/event/content_directory` get a notification (ConnectionManager subscriptions are accepted but never fire spontaneous events).
- Notify jobs are deduplicated per subscription ID before delivery — if a second scan finishes before the first notify goes out, only the newer job is sent.
- Delivery happens on a single worker thread with a bounded queue (`kMaxQueuedNotifyJobs = 256`); a failed or non-2xx notify delivery expires the subscription immediately rather than retrying.
