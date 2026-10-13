# Platform Differences

Both builds share the server code. The differences are in the user interface, in operating system services, and in a few defaults.

## Summary

| Area | Windows | Linux |
|---|---|---|
| Programs | `dlna-server.exe` (window; `--headless` for no window) | `dlna-server` (no window), `dlna-server-gui` and `dlna-server-gui-bin` (window) |
| User interface | Win32 | GTK4 |
| Minimum system | Windows 10 | Not fixed in the code. The window code is written for GTK 4.6 or newer. The build needs libcurl. |
| Config location | Next to the program | `~/.config/dlna-server/` |
| Run at sign-in | `RunOnBoot` setting | Not supported. The setting is ignored. |
| Firewall | The app checks and sets Windows Firewall rules | The app does not touch the firewall |
| Close button while running | Hides to the tray | Stops the server and quits |
| Single instance | Named mutex and window class | File lock and Unix socket in `/tmp/<app-id>-<uid>/` |
| Second launch with sources | Sends sources to the running window | Window app: sends sources. `dlna-server`: stops the first instance and takes over. |
| Second launch without sources | Shows the existing window | Window app: shows it. `dlna-server`: replaces the first instance. |
| Background mode | `--headless`. Prints `server is up`, then detaches from the console | `dlna-server` forks to the background and prints `server is up`. With `--debug` it stays in front. |
| `--headless`, `-h` | Starts without a window | Accepted and ignored |
| Command-line values `--port`, `--name`, `--uuid`, `--debug` | Run only, until you save from the app | Saved to `config.ini` |
| Console output | The program uses the GUI subsystem. It attaches to the parent console only in headless mode | Normal terminal output |
| Folder watching | `ReadDirectoryChangesW` | `inotify` |
| Network change watching | IP Helper notifications | Netlink |
| Sleep and wake | Restarts discovery when the system resumes | No special handling |
| File sending | `TransmitFile`, in chunks up to 2,147,483,646 bytes | `sendfile` |
| HTTP port reuse | Exclusive bind. A second program cannot share the port | `SO_REUSEADDR` and `SO_REUSEPORT` are set |
| Virtual adapter filter | Adapter name list, plus default gateway check | Adapter name prefixes, plus default-route check |
| Fonts | Segoe UI | Selawik (installed with the package), then Segoe UI, Cantarell, Noto Sans |

## Notes

### Close behavior

The rule is `ShouldCloseNow(isRunning, isBusy)`: the app may close only when the server is stopped and idle. Windows answers "not now" by hiding the window. Linux answers by stopping the server first and then closing. See [Using the App](Using-the-App.md#closing-the-window).

### Adapter selection

Both builds exclude adapters that look virtual, unless the adapter has a default gateway. Only the way to detect "default gateway" differs:

- Windows asks the adapter list for gateway addresses of each adapter.
- Linux opens a UDP socket toward `8.8.8.8` (and `2001:4860:4860::8888` for IPv6). It sends no packet. The address the kernel picks as source marks the default-route adapter.

Names treated as virtual:

- Windows (part of the name, any case): `virtualbox`, `vmware`, `hyper-v`, `virtual switch`, `wsl`, `docker`, `tap-windows`, `tailscale`, `zerotier`, `loopback`, `npcap loopback`, `vethernet`.
- Linux (start of the name): `docker`, `veth`, `vboxnet`, `vmnet`, `tailscale`, `zt`, `wg`, `tun`, `tap`, `br-`.

### Linux packages

| Format | Notes |
|---|---|
| `.deb` | Depends on `libcurl4` and the libraries found at build time. Installs launchers, icons, fonts, desktop entry. |
| AppImage | x86_64. Built with `linuxdeploy`. |
| Flatpak | GNOME 47 runtime. Network access, X11 and Wayland sockets, home folder access. |

### macOS

`CMakeLists.txt` has a macOS bundle branch. The directory watcher and the network change watcher are written for Linux and Windows only. macOS is not covered by the release scripts.

### WSLg

The `dlna-server-gui` launcher sets `WAYLAND_DISPLAY`, `XDG_RUNTIME_DIR` and `DISPLAY` when they are missing, waits up to 60 seconds for the Wayland socket, and starts a D-Bus session if none exists. The tray icon is not available on WSLg.
