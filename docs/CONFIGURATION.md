# Configuration Reference

Settings persist to `config.ini`, a single `[Settings]` section, key=value per line. Location:

- Windows: next to the executable (`GetModuleFileNameW` directory)
- POSIX: next to the running binary, resolved via `/proc/self/exe` (Linux) or `_NSGetExecutablePath` (macOS)

The file is written UTF-8 with a BOM (`Config::Save`), and read back tolerating both UTF-8-with-BOM and UTF-16LE-with-BOM input (`ReadConfigFile` in `config.cpp` handles the `0xFF 0xFE` marker explicitly — a config file hand-edited in a Windows text editor that defaults to UTF-16 will still load).

## Fields

| Key | Type | Default | Notes |
|---|---|---|---|
| `ServerName` | string | hostname | UPnP friendly name |
| `Port` | int | `8200` | HTTP port; validated 1–65535 (`TryParsePortStrict`) |
| `FileServerPort` | int | `8201` | Deprecated — if it differs from `Port`, a log line notes media is served on `Port` instead |
| `FlatFolderStyle` | bool (`0`/`1`) | `0` | Merge subfolder contents into the parent container instead of creating nested containers |
| `ShowFileNamesInsteadOfTitles` | bool | `0` | Use the raw filename as the DIDL title instead of the derived stem/playlist title |
| `ProxyStreams` | bool | `0` | Force all remote `<res>` URLs through this server's `/media/{id}` proxy instead of exposing the origin URL directly (see `ShouldProxyRemoteUrl` in `contentdirectory.cpp` — URLs with embedded credentials are always proxied regardless of this setting) |
| `SortByTitle` | bool | `0` | Sort children by natural-order title instead of scan order; used as the default `SortCriteria` when a `Browse`/`Search` request supplies none |
| `DoNotShowAllMediaFolders` | bool | `0` | Reserved for hiding aggregate "all media" containers |
| `AddArtistAlbumFolders` | bool | `0` | Mirror audio/video items under derived `Artist/Album` containers in addition to their source location |
| `DebugLog` | bool | `0` | Enables verbose SSDP discovery logging (`DiscoveryLog` in `ssdp.cpp`) and writes all log lines to `debug.log` next to `config.ini`, in addition to the in-memory ring buffer |
| `RunOnBoot` | bool | `0` | Windows: writes/removes a `HKCU\...\Run` registry value launching with `--headless`. POSIX: accepted but not yet wired to a service manager (`Config::SetRunOnBoot` is a no-op on POSIX) |
| `DefaultPlaylistEnabled` | bool | `0` | Scan `DefaultPlaylistPath` as an additional source named "Default playlist" |
| `DefaultPlaylistPath` | string | `<config-dir>/default.m3u` | Created empty if it doesn't exist when an entry is first appended via the GUI |
| `IPWhiteList` | string | empty | Comma-separated list of IPv4/IPv6 addresses and/or CIDR ranges (e.g. `192.168.1.50,10.0.0.0/24,fe80::1`). Empty means all addresses are allowed — see `IPWhitelist::IsAllowed` |
| `DeviceUUID` | string | generated on first run | UPnP device UUID; generated once and persisted, not regenerated on subsequent loads |
| `DeviceManufacturer` | string | `dlna-server contributors` | `<manufacturer>` in `description.xml` |
| `DeviceModelName` | string | `dlna-server` | `<modelName>` in `description.xml` |
| `PresentationURL` | string | `/` | `<presentationURL>` in `description.xml` |
| `BackgroundScanEnabled` | bool (`0`/`1`) | `0` | Watch media folders for filesystem changes and trigger rescans automatically. Poll-based, 5‑second interval — see `source_watcher.cpp` |
| `NetworkInterfaceAllowList` | string | empty | Comma-separated list of network adapter names to use for SSDP and HTTP. Empty means all adapters are eligible. Matching is case-insensitive substring match against the adapter's friendly name (Windows) or interface name (POSIX) — filtering is applied in `EnumerateNetworkEndpoints()` each time endpoints are refreshed |
| `MediaSources` | string (quoted-comma list) | empty | Comma-separated list of source paths/URLs; each entry is double-quoted, embedded quotes are doubled (`""`). Written by `BuildQuotedCommaList`, parsed by `ParseQuotedCommaList`. Config files predating the comma format (legacy pipe-delimited, `|` separator with `\`-escaping) are still read via `DecodeLegacyPipeDelimitedSources` and silently upgraded on first save |

## Media source types

Each `MediaSources` entry is classified at scan time, not at config-parse time:

- **Local folder** — any path that isn't a playlist extension or FTP/FTPS URL
- **Playlist file** — path ends in `.m3u`, `.m3u8`, or `.pls` (`IsPlaylistSourcePath`)
- **Network folder** — `ftp://` or `ftps://` URL (`IsNetworkShareUrl`); directory listing is attempted, individual files and nested playlists inside are scanned
- **Removed (SMB)** — `smb://`/`smbs://` entries are recognized and logged as unsupported rather than silently ignored, so an old config doesn't look like it lost a source for no reason

`http://`/`https://` single-file sources are supported as playlist entries and as direct `--source` values, but are not walked as directories (no HTTP directory-listing support — see `ListRemoteDirectory` in `network_sources.cpp`).

## Environment variable

- `DLNA_SERVER_SKIP_FIREWALL` (Windows) — if set (to any value, including empty), `Server::Start()` skips the interactive firewall-access check. Useful in CI or when firewall rules are provisioned out-of-band.

## Runtime overrides

Command-line flags (`--port`, `--name`, `--uuid`, `--source`, `--debug`) apply on top of the loaded config for that process instance and are not written back to `config.ini` unless the GUI's Settings dialog is used, which calls `Config::Save()` explicitly. These flags work the same way on Windows (headless mode or regular window) and on POSIX (both the desktop GUI and the command-line binary).
