# UI Reference

Mockups for every application window. Source art lives in `docs/media/`.
Titles below are the window captions used in the codebase.

## DLNA Server

![DLNA Server](media/dlna-main-active.svg)

## DLNA Server Settings

![DLNA Server Settings](media/dlna-settings.svg)

## Add media source

![Add media source](media/dlna-add-media-source.svg)

## Default playlist entry

![Default playlist entry](media/dlna-default-playlist-entry.svg)

## DLNA Server Log

![DLNA Server Log](media/dlna-logs.svg)

## DLNA Server Help

![DLNA Server Help](media/dlna-help.svg)

## DLNA Server (server could not start warning)

![DLNA Server server could not start warning](media/dlna-warning-dialog.svg)

## WSLg icon and launch identity

WSLg shows the app in two places: the Start Menu and the taskbar.
They use two different channels. Both channels must agree on one short id.

### Start Menu icon channel

WSLg reads the desktop file at
`/usr/share/applications/com.app.dlna_server_14ag.desktop`.
It takes the file name, removes `.desktop`, and cuts everything up to
the last dot. The key becomes `dlna_server_14ag`.
It reads the `Icon` name and finds the PNG file.
Then it publishes an app list entry with the key and the icon.
This entry is what you see in the Start Menu.
You can check it in `/mnt/wslg/weston.log`:

- `Icon name:com.app.dlna_server_14ag`
- `Icon file:/usr/share/pixmaps/com.app.dlna_server_14ag.png`
- `app list entry updated: Key:dlna_server_14ag`

### Start Menu launch process

When you click the Start Menu entry, WSLg runs the `Exec` line:

- `Exec=env DLNA_SERVER_GTK_LAUNCHED=1 /usr/bin/dlna-server-gui`

`/usr/bin/dlna-server-gui` is a small wrapper script.
It starts the real binary `dlna-server-gui-bin`.
The binary takes the single-instance lock.
If a copy is already running, the new copy sends a show
request to it and then exits. So only one window exists.
The GUI window appears only when the main loop runs and
pumps the Wayland socket. A window with no live process
behind it is dead and cannot move or take clicks.

### Taskbar icon channel

When the live window opens, it sends its Wayland app id.
WSLg compares this id with the app list key.
The window must send the short id `dlna_server_14ag`.
Then WSLg finds the entry and the image:

- `loadIconEvent is signalled. dlna_server_14ag`
- `entry 0x..., image 0x...` (real pointers, not nil)

Note: the associate line in the log always shows `appIcon: (nil)`.
That is normal. The entry and image match is the real proof.

### Values that must be equal

| Place | Value | Must equal |
|---|---|---|
| Desktop file name | `com.app.dlna_server_14ag.desktop` | Key `dlna_server_14ag` after WSLg cuts at the last dot |
| Desktop `Icon` name | `com.app.dlna_server_14ag` | PNG file `/usr/share/pixmaps/com.app.dlna_server_14ag.png` |
| Live Wayland window id | `dlna_server_14ag` | App list key `dlna_server_14ag` |
| `PROGRAM_CLASS` from `--print-gtk-identity` | `dlna_server_14ag` | Live Wayland window id |
| Desktop `StartupWMClass` | `com.app.dlna_server_14ag` | X11 window class (X11 fallback only) |
| `PRGNAME` and `GTK_APP_ID` | `com.app.dlna_server_14ag` | GtkApplication id and D-Bus name |

If the window sends the full id with dots, WSLg finds no
entry and the taskbar shows a generic icon. The Start Menu
icon still works because it uses the desktop file channel.

