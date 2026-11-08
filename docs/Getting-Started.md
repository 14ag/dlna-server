# Getting Started

This page shows how to install the app, share a folder, and check that a player can see it.

## Before you start

- Your computer and your player must be on the same local network and subnet.
- Windows 10 or later, or a Linux system.
- On Windows, the app needs one firewall change. You can accept it when the app asks.

## Install

Download a file from the [releases page](https://github.com/14ag/dlna-server/releases).

### Windows

1. Unzip the file. It contains `dlna-server.exe`.
2. Run `dlna-server.exe`.

The zip name ends with `windows-x64` or `windows-Win32`. Use `x64` on a 64-bit system.

### Debian or Ubuntu

```bash
sudo apt install ./<file>.deb
```

The package installs three commands:

| Command | What it does |
|---|---|
| `dlna-server` | The command-line server. No window. |
| `dlna-server-gui` | Starts the window. Use this one from the menu or terminal. |
| `dlna-server-gui-bin` | The window program itself. The `dlna-server-gui` script starts it. |

### AppImage and Flatpak

```bash
chmod +x <file>.AppImage
./<file>.AppImage
```

```bash
flatpak install --user <file>.flatpak
```

The Flatpak build can read your home folder. Media outside your home folder is not visible to it.

## Share your first folder

1. Start the app.
2. Click **Add**.
3. Choose **Folder...** or **File...**. You can also type a path or an FTP folder URL such as `ftp://user:pass@server:21/media`.
4. Click **Add** in the dialog.
5. Click **Start**.

The status line shows `starting server...`, then `scanning...`, then `Server running`. Players can find the server as soon as it starts, even while the scan runs. Items appear as the scan finds them.

### Windows firewall prompt

The first time you press **Start**, the app checks the Windows Firewall. If the rules are missing, it asks:

> DLNA Server needs Windows Firewall access for this app ... Allow access now?

Choose **Yes**, then accept the Windows permission (UAC) prompt. The app adds two inbound rules for `dlna-server.exe`: one for TCP (any port, for this program only) and one for UDP port 1900. Both allow the local subnet only, on the Domain, Private and Public profiles.

If you choose **No**, the server does not start.

## Check from a player

1. Open the media or "network" app on your TV or player.
2. Look for a server with the name of your computer. Change this name in **Settings** under **Server name**.
3. Open the server. You see one folder for each source.

If the player does not find the server, see [Troubleshooting](Troubleshooting.md).

## Use the command line

You can start a server without opening the window. Paths on the command line are for this run only. They are not saved to `config.ini`.

### Linux

```bash
dlna-server ~/Videos ~/Music
```

The command starts the server in the background and prints `server is up`. To stop it:

```bash
dlna-server --kill-server
```

Add `--debug` to stay in the foreground and print log lines to the terminal.

### Windows

```bat
dlna-server.exe --headless D:\Media E:\Video
dlna-server.exe --kill-server
```

If you give a path on Windows, the app starts without a window, even without `--headless`.

Running the command a second time does not start a second server. On Windows it shows the existing window or replaces its sources. On Linux the `dlna-server` command stops the first instance and takes over.

## Next steps

- [Using the App](Using-the-App.md)
- [Configuration](Configuration.md)
