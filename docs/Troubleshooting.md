# Troubleshooting

Each entry lists the symptom, the likely cause, and what to do. Turn on **Debug log** in Settings and restart the server for more detail. Read lines in the **Logs** dialog or in `debug.log`.

## The player does not find the server

| Check | Why it matters | What to do |
|---|---|---|
| Is the status `Server running`? | The server advertises only while it runs. | Press **Start**. |
| Same network and subnet? | Discovery uses multicast on the local network. | Connect both devices to the same router or access point. Turn off client isolation on the router. |
| Windows Firewall | Without the two inbound rules, the player cannot reach the server. | Press **Start** and accept the prompt, or run `dlna-server.exe --configure-firewall --port 8200` as administrator. |
| Wrong adapter | The app skips adapters that look virtual (VPN, Docker, Hyper-V, WSL, VirtualBox, VMware) unless they have a default gateway. | Set `NetworkInterfaceAllowList` in `config.ini`. See [Configuration](Configuration.md#network-adapters). |
| IP whitelist | A whitelist blocks all other addresses with `403`. | Clear **IP whitelist** or add the player's address. Log line: `Blocked connection from <address>`. |
| Port in use | Another program holds the HTTP port. | Pick another port in Settings. Log line: `Failed to start HTTP server on port N`. |
| Player sleeps or joined late | Players that missed the start messages wait for the next one. | Wait up to 6 minutes, or press **Stop** then **Start**. The server repeats its announcement every 4 to 6 minutes. |

### Log lines that help

| Log line | Meaning |
|---|---|
| `SSDP IPv4 multicast join failed` / `SSDP multicast join failed on all endpoints` | The operating system refused multicast on that adapter. Discovery will not work there. |
| `Failed to find any active network endpoint for discovery.` | No usable adapter or address was found. |
| `Discovery endpoint selected: ...` | Debug only. Shows every address that is advertised. |
| `SSDP search ignored: ...` | Debug only. A search arrived but did not match, so no answer was sent. |
| `Network topology change detected; refreshing endpoints` | The network changed. The server re-announces if the address list changed. |

## The server does not start

| Message | Cause | Action |
|---|---|---|
| `No media sources configured` | No source, and the default playlist is off. | Add a source. |
| `Invalid port` | `Port` is outside 1-65535. | Fix the port. |
| `Firewall access was denied` (Windows) | You chose **No** or cancelled the permission prompt. | Press **Start** again and accept. |
| `No active network endpoints found` | No adapter is up with a usable address. | Connect to a network. Check `NetworkInterfaceAllowList`. |
| `Failed to start SSDP discovery` | Port 1900 or multicast is not available. | Close other DLNA or UPnP programs. Check the firewall. |
| `Server is already starting on another thread` | A start is already in progress. | Wait. |

On Linux, `dlna-server` prints `no sources found, please add a source or pass one with the --source flag` and exits with code 1 if it has nothing to share.

## A folder or file does not appear

- **Unsupported type.** See the type list in [Media Library Deep Dive](Media-Library.md#supported-file-types).
- **Hidden entries.** The scan skips hidden and system files on Windows. It skips names that start with `.` on Linux.
- **Links.** The scan skips symbolic links on Linux and reparse points on Windows.
- **No permission.** The app skips folders it cannot open. Log line: `Skipping unreadable folder`. A missing source gives `Skipping missing source`.
- **Too deep.** Local folders deeper than 64 levels, network folders deeper than 8, and playlists nested more than 8 deep are skipped. Log tag: `[media:scan-depth]`.
- **Changes not picked up.** Turn on **Background scan**, or press **Scan** or **F5**.
- **Playlist problems.** Log tags `[media:fetch-failed]` (could not be read) and `[media:fetch-invalid]` (does not start with `#EXTM3U`, or `[playlist]` for `.pls`).
- **SMB source.** Log tag `[media:smb-removed]`. SMB is not supported.
- **Flatpak.** The sandbox can read only your home folder.

## A file does not play

| Symptom | Cause | Action |
|---|---|---|
| Player lists the file but refuses it | The player does not support the format. | Convert the file. The server does not transcode. |
| No seeking in a remote stream | Seeking needs a known size. The server asks the remote host for the size. If the host does not report one, the server advertises no seeking. | Use a source that reports its size. |
| Odd seeking in an HLS item | The server serves the HLS manifest as one item. The player handles the segments. | Use the player's own HLS support. |
| Images do not seek | The server advertises images as not seekable. | Normal. |
| No subtitles | The subtitle file name must match the video name (`movie.mkv` with `movie.srt`). The player must support Samsung-style `sec:CaptionInfoEx`. | Rename the file, or add the subtitle with **Add...** in the Playlist settings. |
| No album art | The server looks for `folder.jpg`, `cover.jpg`, `album.jpg`, `thumb.jpg`, `thumb.jpeg`, or an image with the same name as the file. | Rename the image. |
| Remote file does not play | A login or network error. | Search the log for `[remote:auth]` and `[remote:network]`. Passwords in URLs are hidden in logs. |

## Searching on the player returns nothing

The server supports only a single search condition: title contains text, class derived from a class, or class equals a class. A search with `and` or `or` returns no results. Use the browse view.

## Settings changes do not apply

Most options are read when the server starts. When the server runs, Settings asks *Restart server?* after you press **OK**. Choose **Yes**, or press **Stop** and **Start**.

## The server stopped by itself

The app checks that the HTTP and discovery workers are alive. After four checks in a row that fail (about one second), it stops the server and shows: *The server stopped unexpectedly and has been shut down*. Press **Start**. If it happens again, send `debug.log` with a bug report. Search the log for `[pool-guard]` and `[fatal-guard]`. These lines show an error that the server caught.

## Linux window problems

- **No tray icon.** Your desktop has no StatusNotifier host. Closing the window stops the server and quits, so nothing is lost.
- **WSLg.** The launcher waits up to one minute for the display, then starts. If the window never opens, check `~/.cache/dlna-server/gui-launch.log`.
- **Second launch does nothing.** Another instance owns the lock in `/tmp/<app-id>-<uid>`. The new launch asks it to show its window. If the first instance is stuck, run `dlna-server-gui --kill-server`.
