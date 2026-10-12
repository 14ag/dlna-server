# Using the App

This page describes the window, the tray icon, the dialogs and the keyboard keys. Both the Windows and the Linux window follow the same rules unless a note says otherwise. See [Platform Differences](Platform-Differences.md).

## Main window

The window has a fixed size. It has four buttons and a list of sources.

| Button | When the server is stopped | When the server is running |
|---|---|---|
| **Add** / **Scan** | Opens the *Add media source* dialog. | Becomes **Scan**. Scans all sources again. |
| **Delete** | Removes the selected source. | Same. |
| **Start** / **Stop** | Starts the server. | Becomes **Stop**. Stops the server. |
| **Settings** | Opens the settings dialog. | Same. |

**Add**/**Scan** and **Delete** are disabled while the server starts or stops, and while a scan runs.

The line under the toolbar shows the state:

| Text | Meaning |
|---|---|
| (empty) | The server is stopped. |
| `starting server...` / `stopping server...` | A start or stop is in progress. |
| `scanning...` | A scan is running. |
| `Server running` | The server is running and no scan is active. |
| `temporary source` | The server runs with sources from the command line, not from `config.ini`. |

An empty list shows: *Please add shared folders or files with Add.*

### Source list

- Long paths end with `...`. Move the pointer over a shortened path to see the full path.
- **Delete** is enabled only when you have selected a source. If you click anywhere else, the selection clears and **Delete** turns off. Clicking **Delete** itself keeps the selection.
- Right-click a selected source, or press the Menu key, to get *Remove selected source*.
- Drag folders, media files or playlists from your file manager onto the list to add them. Dropping works only while the server is stopped. Files of unsupported types are ignored. Folders are always accepted.
- Adding or removing a source starts a scan, even when the server is stopped. A stopped scan updates the ID cache (`media-cache.tsv`).
- While the list shows temporary sources from the command line, you cannot add or remove sources.

## Add media source dialog

Type or paste a local path or a network URL. The **Add** button turns on when the text box has content.

| Button | What it does |
|---|---|
| **Folder...** | Picks a folder. |
| **File...** | Picks a media file or a playlist (`.m3u`, `.m3u8`, `.pls`). |
| **Add** | Adds the source. A source that is already in the list is not added twice. |
| **Cancel** | Closes the dialog. |

Supported URL schemes are `ftp`, `ftps`, `http` and `https`. A source URL must be an FTP or FTPS folder, or an HTTP or HTTPS playlist (`.m3u`, `.m3u8`, `.pls`). A direct HTTP link to one media file is skipped as a missing source. Put it in a playlist instead. SMB (`smb://`) is not supported. SMB entries from an older `config.ini` are skipped with a log line.

## Settings dialog

The dialog has a **Logs** and a **Help** entry at the top.

| Group | Option | Effect |
|---|---|---|
| Server | Server name | Name shown on players. |
| Server | HTTP port | TCP port of the HTTP server. Default `8200`. |
| Server | IP whitelist | Comma-separated IP addresses or CIDR ranges that may connect. Empty means everyone. |
| General | Run on Windows startup | Windows only. Starts the app with `--headless` at sign-in. |
| General | Debug log (write to file) | Writes `debug.log` next to `config.ini`. |
| Playlist | Default playlist | Shares `default.m3u` as a source named *Default playlist*. |
| Playlist | **Add...** | Appends a movie (and optional subtitle) to `default.m3u`. |
| Media browsing | Add artist/album folders to audio | Also lists media under artist and album folders. |
| Media browsing | Do not show 'All Media' folders | Saved in `config.ini`. The code reviewed for these docs does not read it when it builds the catalog. |
| Media browsing | Sort by title instead of file name | Sorts items by title. Numbers sort in natural order (`2` before `10`). |
| Media browsing | Flat folders style | Puts the files of all subfolders directly in the source folder. |
| Media browsing | Show file names instead of titles | Uses the file name as the title. |
| Media browsing | Proxy streams | Sends remote media through this app instead of giving the player the original URL. |
| Media browsing | Background scan | Scans again when it sees changes in local folders. |

Press **OK** to save. If the server is running and you changed an option that needs a restart, a question asks: *Restart server?* The default answer is **No**. Options that ask for a restart: HTTP port, Server name, IP whitelist, Debug log, Add artist/album folders, Do not show 'All Media' folders, Sort by title, Flat folders style, Show file names, Proxy streams, Background scan, and Default playlist.

Press **Cancel** to close without saving.

## Log dialog

Shows the last 1,000 log lines held in memory. **Refresh** adds new lines. It works even when **Debug log** is off. **Debug log** only controls writing to the `debug.log` file and adds detail lines.

## Help dialog

Lists the command-line options and the settings. The option table is the same one that `--help` prints.

## Tray icon

The app puts an icon in the notification area.

- Click it to show the window.
- Windows menu: *Show Window*, *Start Server* or *Stop Server*, *Exit*.
- Linux menu: *Show Window*, *Start/Stop Server*, *Exit*. The icon needs a desktop that provides a StatusNotifier host. Some desktops, such as WSLg, do not. Then the app logs a note and has no tray icon.

## Closing the window

| Platform | While the server is stopped | While the server is running |
|---|---|---|
| Windows | Closes the app. | Hides the window. The server keeps running. Use the tray icon to show it or to exit. |
| Linux | Closes the app. | Stops the server, then closes the app. |

If you close the window while the server is starting or stopping:

- Linux: the app finishes that step, stops the server, and closes.
- Windows: while stopping, the app closes after the stop. While starting, the window hides and the server keeps running.

## Dialog rules

- A dialog blocks the main window. If you click the main window, the dialog comes to the front and flashes.
- No window can be resized.
- Press **Esc** to close a dialog.
- Press **Backspace** to close a dialog, unless a text box has focus. Then it deletes text.
- Dialogs can open dialogs, for example *Settings* then *Logs*. A message box opens in front of the topmost dialog.

## Keyboard

| Key | Action |
|---|---|
| **F1** | Opens Help. |
| **F5** | Scans again when the server runs. Reloads the source list when it is stopped. Does nothing while busy or scanning. |
| **Delete** | Removes the selected source. |
| **D** | Removes the selected source (list must have a selection). |
| **Tab** | Moves between the button row and the source list. |
| **Left** / **Right** | Moves between the toolbar buttons. |
| **Up** / **Down** | Nothing on toolbar buttons. Moves the selection in the list. |
| **Esc** / **Backspace** | Closes the open dialog. |

### Access keys

Each button label has one underlined letter. The underlines appear after you press a key on the keyboard and disappear after you click with the mouse.

- Windows: press the letter alone. It works when the focus is on a toolbar button or the source list.
- Linux: press **Alt** and the letter. In the *Settings* dialog, **Logs** and **Help** also work with the letter alone.
