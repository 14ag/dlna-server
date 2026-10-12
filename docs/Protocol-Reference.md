# Protocol Reference

This page lists everything a player can ask the server. The server speaks UPnP Device Architecture 1.0 and ContentDirectory:1 / ConnectionManager:1 over HTTP/1.1, and announces itself as DLNA `DMS-1.50`.

Discovery (SSDP) is in the [Discovery Deep Dive](Discovery.md).

## HTTP server

- Listens on TCP port `Port` (default 8200) on all addresses, IPv4 and IPv6, as separate sockets. If one family fails to bind, the other still runs. If both fail, the server does not start.
- HTTP/1.1 keep-alive is the default. `Connection: close` or an HTTP/1.0 request closes the connection after the reply.
- Limits: request headers 64 KiB; SOAP body 1 MiB; 64 client workers. When all 64 are busy, new connections wait in the operating system queue.
- Timeouts: 10 s for reads and writes; 5 s idle between keep-alive requests; 60 s for writes while streaming.
- A client that is not allowed by the IP whitelist gets `403 Forbidden` and is closed.
- The `Host` header must not contain spaces, `/` or `\`, or the reply is `400`. If the host is `localhost`, `127.0.0.1` or `::1`, the server replaces it with its first routable address, so URLs in replies work for other devices. If there is no `Host`, the server uses the local address of the connection.

### Routes

| Method | Path | Reply |
|---|---|---|
| GET, HEAD | `/description.xml` | Device description. |
| GET, HEAD | `/ContentDirectory.xml` | ContentDirectory service description (SCPD). |
| GET, HEAD | `/ConnectionManager.xml` | ConnectionManager service description (SCPD). |
| GET, HEAD | `/icons/server_icon_48.png`, `_120.png`, `_256.png` | Device icons. |
| GET, HEAD | `/media/<id>` or `/media/<id>.<ext>` | The media item. The extension is optional and ignored. It lets players see the file type in the URL. |
| GET, HEAD | `/subtitle/<id>` | The subtitle file of an item. |
| GET, HEAD | `/albumart/<id>` | The album art of an item. |
| POST | `/upnp/control/content_directory` | SOAP control for ContentDirectory. |
| POST | `/upnp/control/connection_manager` | SOAP control for ConnectionManager. |
| SUBSCRIBE, UNSUBSCRIBE | `/upnp/event/content_directory`, `/upnp/event/connection_manager` | Events. |

Query strings are ignored.

### Status codes

| Code | When |
|---|---|
| 200, 206 | Success. 206 for a byte range. |
| 400 | Bad `Host`; id is not a number or is negative; unknown route for POST or other methods; missing, bad or too large `Content-Length` on SOAP; short SOAP body. |
| 404 | Unknown GET path; no such item; file cannot be opened; no subtitle or art. |
| 416 | The `Range` cannot be satisfied. The reply has `Content-Range: bytes */<size>`. |
| 502 | An HLS manifest could not be fetched. |

### Media replies

Headers for `/media/<id>`:

| Header | Value |
|---|---|
| `Content-Type` | The item's MIME type. |
| `Content-Length` | Bytes in this reply. |
| `Accept-Ranges` | `bytes` when the size is known. Otherwise `none`. |
| `Content-Range` | On `206`. |
| `transferMode.dlna.org` | `Streaming` |
| `contentFeatures.dlna.org` | `DLNA.ORG_PN=<profile>;DLNA.ORG_OP=<01 or 00>;DLNA.ORG_FLAGS=01700000000000000000000000000000`. The profile is left out when the format has none. `OP=01` means byte seek is allowed. It is `00` for images and for items with an unknown size. |

Rules:

- One byte range is supported: `bytes=a-b`, `bytes=a-`, `bytes=-n`. A list of ranges, or a start beyond the end, gets `416`.
- Local files are sent with `TransmitFile` (Windows, in chunks of at most 2,147,483,646 bytes) or `sendfile` (Linux).
- **Remote items** (`ftp`, `ftps`, `http`, `https`) are streamed through libcurl. The server requests the same byte range from the remote host.
- If a remote item has no known size and the request has no `User-Agent`, or the agent contains `SEC_HHP_` (a Samsung player), the server sends `Content-Length: 1073741824` and `Accept-Ranges: none`. Those players refuse a reply without a length.
- If the request has `Icy-MetaData: 1`, the server forwards it to the remote host and passes `icy-*` response headers back.
- **HLS items** (`video/mpegurl`): the server fetches the manifest, rewrites every relative URI to an absolute URL, and sends it with `Accept-Ranges: none` and `DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=...`. A failed fetch returns `502`.
- `/subtitle/<id>` and `/albumart/<id>` reply with `Connection: close` and no range support. Subtitle MIME types: `.srt` `text/srt`, `.vtt` `text/vtt`, `.ass` and `.ssa` `text/x-ssa`, `.smi` `application/smil`, others `text/plain`. All are `charset=utf-8`. Album art is sent as `image/jpeg` or `image/png`.

## Device description

`/description.xml` has:

- `URLBase`: `http://<host>:<port>`
- Device type `urn:schemas-upnp-org:device:MediaServer:1`, `X_DLNADOC` `DMS-1.50`.
- `friendlyName`: the server name. `manufacturer`, `modelName` and `presentationURL`: from settings. `modelNumber`: the app version. `serialNumber`: fixed `12345`.
- `UDN`: `uuid:<DeviceUUID>`.
- Three icons: 48, 120 and 256 pixels.
- Services: ContentDirectory (`/ContentDirectory.xml`, control `/upnp/control/content_directory`, events `/upnp/event/content_directory`) and ConnectionManager (same pattern).

## SOAP

The server reads the action name from the first element inside `Body`. It does not use a full XML parser: tag prefixes are removed, and argument values are the text between the matching tags. Replies are SOAP envelopes. Errors are SOAP faults with a UPnP error code.

### Error codes

| Code | Text | Meaning |
|---|---|---|
| 400 | Bad Request | The XML is malformed. |
| 401 | Invalid Action, or Invalid XML | Unknown action; missing `ObjectID` or `BrowseFlag`. |
| 402 | Invalid Args | A required argument is missing, or an index or count is not a non-negative number. |
| 701 | No such object | Unknown `ObjectID` or `ContainerID`. |
| 706 | Not a container (Browse), No such connection (ConnectionManager) | |
| 710 | Content directory not yet initialized | The server has not built the root yet. |

### ContentDirectory actions

| Action | Notes |
|---|---|
| `GetSystemUpdateID` | Returns the catalog counter. |
| `GetSearchCapabilities` | `dc:title,upnp:class` |
| `GetSortCapabilities` | `dc:title,upnp:class` |
| `Browse` | `BrowseMetadata` (one object, `TotalMatches` 1) and `BrowseDirectChildren`. |
| `Search` | Searches all descendants of `ContainerID`. |

**Paging.** `StartingIndex` and `RequestedCount` work as in the standard. `RequestedCount=0` means "all", but a reply never has more than 16,000 items. A player that needs more must page. `NumberReturned` may be smaller than `TotalMatches`.

**Sorting.** `SortCriteria` that contains `dc:title` sorts by title with natural number order. One that contains `upnp:class` sorts by class, then title. A leading `-` means descending. When `SortCriteria` is empty and **Sort by title** is on, items sort by title. Otherwise the order is the scan order.

**Filter.** `*` or empty returns everything. Otherwise `dc:title`, `upnp:class`, `upnp:albumArtURI`, `res` and `dc:date` appear only if the filter text contains them.

**Search criteria.** Exactly one condition is understood:

| Form | Example |
|---|---|
| Title contains | `dc:title contains "holiday"` (case-insensitive) |
| Class derived from | `upnp:class derivedfrom "object.item.videoItem"` |
| Class equals | `upnp:class = "object.item.audioItem.musicTrack"` |
| Everything | empty, `*` or `true` |

Anything else, including conditions joined with `and` or `or`, matches nothing.

Search results are cached per container, with the criteria and sort order, and tagged with the `SystemUpdateID`. Any catalog change makes the entry invalid. A rescan empties the cache.

### DIDL-Lite

Containers: `<container id parentID childCount restricted="1">` with `dc:title` and `upnp:class` (`object.container.storageFolder`).

Items: `<item id parentID restricted="1">` with:

| Element | Content |
|---|---|
| `dc:title` | See [Media Library Deep Dive](Media-Library.md#titles). |
| `upnp:class` | `object.item.videoItem`, `object.item.audioItem.musicTrack` or `object.item.imageItem.photo`. |
| `dc:date`, `rawDate` | HLS items only. |
| `upnp:albumArtURI` (`dlna:profileID="JPEG_TN"`) | `http://<host>/albumart/<id>` when art exists. |
| `res` with `protocolInfo` and `size` | `http://<host>/media/<id><.ext>`. `size` is present when known. For a remote `http` or `https` item without a login in the URL, and with **Proxy streams** off, `res` holds the original URL instead. `ftp` and `ftps` items, and URLs with a user name, always go through the server. |
| `sec:CaptionInfoEx` (`sec:type`=extension) | `http://<host>/subtitle/<id>` when a subtitle exists. |

`protocolInfo` format: `http-get:*:<mime>:DLNA.ORG_PN=<profile>;DLNA.ORG_OP=<01|00>;DLNA.ORG_FLAGS=01700000000000000000000000000000`.

### ConnectionManager actions

| Action | Reply |
|---|---|
| `GetProtocolInfo` | `Source`: the list of `protocolInfo` values for every supported format, plus HLS. `Sink`: empty. |
| `GetCurrentConnectionIDs` | `0` |
| `GetCurrentConnectionInfo` | For `ConnectionID` 0: `RcsID` and `AVTransportID` -1, direction `Output`, status `OK`. Other IDs: fault 706. |

## Events (GENA)

| Item | Behavior |
|---|---|
| Subscribe | `SUBSCRIBE` with `CALLBACK: <http://...>` and `NT: upnp:event`. Only `http://` callbacks are accepted. Reply: `SID` and `TIMEOUT`. |
| Renew | `SUBSCRIBE` with a `SID` header. |
| Cancel | `UNSUBSCRIBE` with a `SID` header. |
| Timeout | `Second-N`. Default 1800. Largest 86400. `infinite` becomes 86400. |
| First event | Sent after you subscribe. Sequence 0. Body: the current `SystemUpdateID`. |
| Later events | Only `SystemUpdateID`, only for ContentDirectory. At most one dispatch per 500 ms: the first change goes at once, later changes in the window are merged into one trailing event with the newest value. |
| Scans | A scan sends one event when it ends. |
| ConnectionManager | Subscriptions are accepted. No events are sent. |
| Limits | 64 subscriptions. 8 sender workers. Failed delivery (timeout 10 s, connect 5 s, or HTTP status 400 or more) deletes the subscription. |
| Stop | Stopping the HTTP server deletes all subscriptions. |
| Errors | `412` for a bad request or unknown `SID`. `404` for an unknown path. |

## Server header

`SERVER` and `Server` values: `<OS> DLNADOC/1.50 UPnP/1.0 dlna-server/<version>`. `<OS>` is `Windows/10.0`, `Linux` or `macOS`.
