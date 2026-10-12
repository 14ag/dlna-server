# Configuration

This page lists every setting, command-line option, environment variable and file the app uses.

## Where files live

| File | Windows | Linux |
|---|---|---|
| `config.ini` | Next to the program file. | `$XDG_CONFIG_HOME/dlna-server/config.ini` if that variable is set to an absolute path. Otherwise `~/.config/dlna-server/config.ini`. If no home folder is found, `/tmp` is used. |
| `debug.log` | Next to `config.ini`. | Next to `config.ini`. |
| `default.m3u` | Next to `config.ini`. | Next to `config.ini`. |
| `media-cache.tsv` | Next to `config.ini`. | Next to `config.ini`. |

On Linux, if no `config.ini` exists at the new place but one exists next to the program, the app copies it once.

`debug.log` is written only when **Debug log** is on. The app empties it the first time it opens it in each run.

## config.ini format

The file is UTF-8 with a byte order mark, with one `[Settings]` section. Lines outside `[Settings]` are ignored. Lines that start with `;` or `#` are comments. The Windows app also reads UTF-16 LE files.

The app writes the whole file when you save in the app. It rewrites comments and unknown keys away. Edit the file only while the app is closed.

```ini
[Settings]
ServerName=MY-PC
Port=8200
MediaSources="D:\Media","E:\Video"
```

### Keys

| Key | Type | Default | Needs restart | Meaning |
|---|---|---|---|---|
| `ServerName` | text | Computer name | Yes | Name players show. An empty value uses the default. |
| `Port` | 1-65535 | `8200` | Yes | HTTP port. An invalid value uses `8200`. |
| `IPWhiteList` | text | empty | Yes | Comma-separated IP addresses or CIDR ranges. See [IP whitelist](#ip-whitelist). |
| `MediaSources` | list | empty | No | Your sources. See [MediaSources format](#mediasources-format). |
| `FlatFolderStyle` | 0/1 | `0` | Yes | Merge subfolders into the source folder. |
| `ShowFileNamesInsteadOfTitles` | 0/1 | `0` | Yes | Use the file name as the title. |
| `ProxyStreams` | 0/1 | `0` | Yes | Always proxy remote media. |
| `SortByTitle` | 0/1 | `0` | Yes | Sort items by title. |
| `DoNotShowAllMediaFolders` | 0/1 | `0` | Yes | Stored. Not read when the catalog is built. |
| `AddArtistAlbumFolders` | 0/1 | `0` | Yes | Add artist and album folders for audio and video. |
| `BackgroundScanEnabled` | 0/1 | `0` | Yes | Scan again when local folders change. |
| `DefaultPlaylistEnabled` | 0/1 | `0` | Yes | Share the default playlist. |
| `DefaultPlaylistPath` | path | `default.m3u` next to `config.ini` | No | File used as the default playlist. No Settings field. |
| `DebugLog` | 0/1 | `0` | Yes | Write `debug.log`. |
| `RunOnBoot` | 0/1 | `0` | No | Windows only. Adds the app to the current user's Run list with `--headless`. Saved but ignored on Linux. |
| `DeviceUUID` | UUID | Generated | No | Device identity. Created on first run and saved. Keep it stable. |
| `DeviceManufacturer` | text | `dlna-server contributors` | No | Shown in the device description. |
| `DeviceModelName` | text | `dlna-server` | No | Shown in the device description. |
| `PresentationURL` | text | `/` | No | Presentation URL in the device description. |
| `NetworkInterfaceAllowList` | list | empty | No | Network adapters to use. No Settings field. See [Network adapters](#network-adapters). |
| `FileServerPort` | number | none | n/a | Old key. Read and ignored. It is not written again. |

"Needs restart" means the server reads the value once when it starts. After you change it in Settings, the app asks to restart a running server. Keys marked "No" are not part of that check; edit them with the server stopped.

### MediaSources format

Each path is in double quotes. Paths are separated by commas. A double quote inside a path is written twice.

```ini
MediaSources="D:\Media","ftp://user:pass@nas/media","D:\Playlists\road trip.m3u8"
```

A single unquoted path without a comma is also accepted. An old file that uses `|` between paths is converted on the next load and saved in the new format.

### IP whitelist

A comma-separated list. Each item is one IP address (IPv4 or IPv6) or a CIDR range such as `192.168.1.0/24` or `fd00::/8`.

- An empty list allows every address.
- The check runs when a client connects to the HTTP port. Blocked clients get `403 Forbidden`.
- IPv6 zone IDs (`%eth0`) are ignored.
- The list does not filter SSDP discovery messages.

### Network adapters

By default the server advertises on every active adapter, except adapters that look virtual and have no default gateway. Set `NetworkInterfaceAllowList` to choose adapters yourself.

```ini
NetworkInterfaceAllowList=Ethernet,Wi-Fi
```

- Items are separated by commas. An item matches when it appears anywhere in the adapter name.
- Windows ignores letter case. Linux does not.
- When the list is not empty, only matching adapters are used. The virtual-adapter rule is not applied.

See [Discovery Deep Dive](Discovery.md) for how adapters are chosen.

## Command-line options

| Option | Meaning |
|---|---|
| `--headless`, `-h` | Windows: start without a window (tray icon only). Linux: accepted and ignored. |
| `--port N` | HTTP port, 1-65535. |
| `--name NAME` | Server name. |
| `--uuid UUID` | Device UUID. |
| `--source "A","B"` | Use these sources for this run. Replaces the sources in `config.ini` for this run. Not saved. |
| `PATH...` | Same as `--source` for each path. |
| `--kill-server`, `-k` | Stop the running instance and close it. |
| `--debug` | Turn on the debug log. |
| `--no-debug` | Turn off the debug log for this run. |
| `--configure-firewall` | Windows only. Adds the firewall rules and exits. Needs administrator rights. Use with `--port N`. |
| `--help` | Show the option list. |

An unknown option that starts with `-` prints the option list and exits with code 2.

Options starting with `--print-` or `--dump-` are hidden test hooks. They are not part of the product.

### Which options are saved

| Option | Windows | Linux |
|---|---|---|
| `--port`, `--name`, `--uuid`, `--debug` | Used for this run. A later save from the app writes them to `config.ini`. | Saved to `config.ini` at start. |
| `--no-debug` | This run only. | This run only. |
| `--source`, `PATH` | This run only. | This run only. |

### How sources from the command line behave

- While they are active, the app ignores the sources in `config.ini` and the default playlist.
- When the server stops, they are cleared.
- If the app already runs, the new instance sends its sources to the running one. The running instance restarts its server with those sources.
- On Windows, giving any source also implies `--headless`.
- Linux `dlna-server` command: a second launch stops the first instance and takes over.

## Environment variables

| Variable | Used by | Effect |
|---|---|---|
| `DLNA_SERVER_SKIP_FIREWALL` | Windows app | Any value, even empty, skips the firewall check when the server starts. |
| `XDG_CONFIG_HOME` | Linux app | Base folder for `config.ini` if it is an absolute path. |
| `HOME` | Linux app | Used when `XDG_CONFIG_HOME` is not usable. |
| `DLNA_SERVER_BIN`, `DLNA_SERVER_GUI_BIN` | Linux launcher | Overrides the path of the `dlna-server` and `dlna-server-gui-bin` programs. |

Build-time variables are in the [Development Guide](Development-Guide.md).

## Files the app writes

- `config.ini`: settings and sources.
- `default.m3u`: written by **Add...** in Settings. Standard M3U with `#EXTINF`, `#DLNA-SUBTITLE:` and `#EXTVLCOPT:sub-file=` lines.
- `media-cache.tsv`: object IDs. See [Media Library Deep Dive](Media-Library.md). It is safe to delete. IDs change after that.
- `debug.log`: log lines when **Debug log** is on.
