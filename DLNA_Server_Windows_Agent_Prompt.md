# Coding Agent Prompt: Native Windows DLNA Media Server

## Project Overview

Build a native Windows desktop application — a DLNA/UPnP MediaServer:1 compliant media server. The reference is an Android app (com.al.dlnaserver). The desktop version must replicate its functional scope exactly, adapted for Windows conventions, using only native Windows UI libraries.

**No Electron. No Java. No .NET/C#. No Qt.**
Target stack: **C++ (C++17 or later), Win32 API for UI, WinSock2 for networking.**
Build system: **CMake + MSVC (Visual Studio 2022 toolchain).**

---

## Architecture Overview

The application has two distinct layers that run concurrently:

1. **Server Engine** — background threads; handles all DLNA/UPnP protocol work.
2. **GUI Layer** — Win32 window/dialog; allows user to manage media sources and settings. No MFC. Use raw Win32 dialog APIs.

Communication between layers: use a shared state struct protected by a `CRITICAL_SECTION` or `std::mutex`, plus `PostMessage` to marshal state changes to the UI thread.

---

## Module Breakdown

### 1. SSDP Module (`ssdp.cpp / ssdp.h`)

SSDP (Simple Service Discovery Protocol) runs on UDP multicast `239.255.255.250:1900`.

**Responsibilities:**
- Join multicast group on all active non-loopback IPv4 network interfaces.
- Listen for `M-SEARCH` requests. When a request targets `ssdp:all`, `upnp:rootdevice`, or `urn:schemas-upnp-org:device:MediaServer:1`, respond with unicast UDP replies per UPnP 1.0 spec.
- Periodically send `ssdp:alive` NOTIFY messages (every 900 seconds, `CACHE-CONTROL: max-age=1800`).
- Send `ssdp:byebye` NOTIFY on server shutdown.
- Each NOTIFY/response must include: `LOCATION` (HTTP URL to device description XML), `USN` (Unique Service Name derived from a persistent UUID), `NT`/`ST` headers.

**Implementation notes:**
- Use `WSASocket` + `IP_ADD_MEMBERSHIP` for multicast join.
- Run on a dedicated thread. Use `select()` with a timeout so the thread can check a stop flag.
- Generate a UUID once at first run, persist it in config (see Persistence section). Use this UUID consistently as the device UUID.

### 2. HTTP Server Module (`httpserver.cpp / httpserver.h`)

A minimal HTTP/1.1 server for serving:
- Device description XML (`/description.xml`)
- Service description XMLs (`/ContentDirectory.xml`, `/ConnectionManager.xml`)
- SOAP action endpoint (`/upnp/control/content_directory`)
- Media file streaming (`/media/<encoded-object-id>`)

**Implementation notes:**
- Use `WSASocket` (TCP, `SO_REUSEADDR`). Bind to `0.0.0.0` on the configured port (default: `8200`, user-configurable).
- Accept loop on a dedicated thread. Dispatch each connection to a worker thread (use a thread pool with `CreateThreadpoolWork` or a fixed pool of ~8 threads).
- Support HTTP range requests (`Range: bytes=X-Y`) for media streaming — this is mandatory for DLNA clients to seek.
- Parse request line and headers only. No body parsing except for SOAP POST.
- For media files: open with `CreateFile` using `FILE_FLAG_SEQUENTIAL_SCAN`, stream in chunks of 64KB using `ReadFile`. Set `Content-Type` from MIME table. Set `transferMode.dlna.org: Streaming` header.

**Required HTTP response headers for media:**
```
Content-Type: <mime-type>
Content-Length: <size>
Accept-Ranges: bytes
transferMode.dlna.org: Streaming
contentFeatures.dlna.org: DLNA.ORG_OP=01;DLNA.ORG_FLAGS=01700000000000000000000000000000
```

### 3. UPnP / SOAP / ContentDirectory Module (`contentdirectory.cpp / contentdirectory.h`)

This is the core of DLNA compliance. It handles `Browse` and `GetSystemUpdateID` SOAP actions.

**Device Description XML** (served at `/description.xml`):
```xml
<?xml version="1.0"?>
<root xmlns="urn:schemas-upnp-org:device-1-0"
      xmlns:dlna="urn:schemas-dlna-org:device-1-0">
  <specVersion><major>1</major><minor>0</minor></specVersion>
  <device>
    <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>
    <friendlyName>{SERVER_NAME}</friendlyName>
    <manufacturer>CustomDLNA</manufacturer>
    <modelName>WinDLNAServer</modelName>
    <UDN>uuid:{UUID}</UDN>
    <dlna:X_DLNADOC xmlns:dlna="urn:schemas-dlna-org:device-1-0">DMS-1.50</dlna:X_DLNADOC>
    <serviceList>
      <service>
        <serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType>
        <serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId>
        <SCPDURL>/ContentDirectory.xml</SCPDURL>
        <controlURL>/upnp/control/content_directory</controlURL>
        <eventSubURL>/upnp/event/content_directory</eventSubURL>
      </service>
      <service>
        <serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType>
        <serviceId>urn:upnp-org:serviceId:ConnectionManager</serviceId>
        <SCPDURL>/ConnectionManager.xml</SCPDURL>
        <controlURL>/upnp/control/connection_manager</controlURL>
        <eventSubURL>/upnp/event/connection_manager</eventSubURL>
      </service>
    </serviceList>
  </device>
</root>
```

**SOAP Browse Action:**

Parse the incoming POST body (XML). Extract: `ObjectID`, `BrowseFlag` (`BrowseMetadata` or `BrowseDirectChildren`), `StartingIndex`, `RequestedCount`.

The content tree is built dynamically from the configured media source paths:
- `ObjectID=0` → root container, lists all configured media source root folders as child containers.
- `ObjectID` for a folder → lists its direct children (files and subfolders).
- `ObjectID` for a file → the media item.

**Object ID scheme:** Use a flat integer-to-path map maintained in memory. Assign ID 0 to root. Assign sequential integer IDs to folders and files as they are first encountered during Browse traversal. This map lives in RAM; it is rebuilt if the server restarts.

**DIDL-Lite response for a folder:**
```xml
<container id="{ID}" parentID="{PARENT_ID}" childCount="{N}" restricted="1">
  <dc:title>{folder name}</dc:title>
  <upnp:class>object.container.storageFolder</upnp:class>
</container>
```

**DIDL-Lite response for a media file:**
```xml
<item id="{ID}" parentID="{PARENT_ID}" restricted="1">
  <dc:title>{display title}</dc:title>
  <upnp:class>{upnp class}</upnp:class>
  <res protocolInfo="http-get:*:{mime-type}:DLNA.ORG_OP=01;DLNA.ORG_FLAGS=01700000000000000000000000000000"
       size="{file size in bytes}">
    http://{server_ip}:{port}/media/{object_id}
  </res>
</item>
```

**UPnP class and MIME type mapping (minimum required set):**

| Extension | MIME Type | UPnP Class |
|-----------|-----------|------------|
| .mp4, .m4v | video/mp4 | object.item.videoItem |
| .mkv | video/x-matroska | object.item.videoItem |
| .avi | video/x-msvideo | object.item.videoItem |
| .mov | video/quicktime | object.item.videoItem |
| .wmv | video/x-ms-wmv | object.item.videoItem |
| .ts, .m2ts | video/MP2T | object.item.videoItem |
| .mpg, .mpeg | video/mpeg | object.item.videoItem |
| .mp3 | audio/mpeg | object.item.audioItem.musicTrack |
| .flac | audio/flac | object.item.audioItem.musicTrack |
| .aac | audio/aac | object.item.audioItem.musicTrack |
| .ogg | audio/ogg | object.item.audioItem.musicTrack |
| .wav | audio/wav | object.item.audioItem.musicTrack |
| .wma | audio/x-ms-wma | object.item.audioItem.musicTrack |
| .m4a | audio/mp4 | object.item.audioItem.musicTrack |
| .jpg, .jpeg | image/jpeg | object.item.imageItem.photo |
| .png | image/png | object.item.imageItem.photo |
| .gif | image/gif | object.item.imageItem.photo |
| .bmp | image/bmp | object.item.imageItem.photo |

Determine `display title` by stripping the file extension from the filename, or using the full filename verbatim if the "Show file names instead of titles" setting is on.

**SOAP response envelope structure:**
```xml
<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"
            s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:BrowseResponse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1">
      <Result>{XML-escaped DIDL-Lite string}</Result>
      <NumberReturned>{n}</NumberReturned>
      <TotalMatches>{total}</TotalMatches>
      <UpdateID>{system update id}</UpdateID>
    </u:BrowseResponse>
  </s:Body>
</s:Envelope>
```

The `Result` field contains the DIDL-Lite XML, XML-escaped (i.e., `<` → `&lt;` etc.).

**XML parsing:** Use the Windows built-in `IXMLDOMDocument` (MSXML) or hand-parse the SOAP body with `strstr`/string ops. MSXML is available on all Windows versions and requires no extra dependency. Include via `#import <msxml6.dll>` or use `CoCreateInstance(CLSID_DOMDocument60, ...)`.

**XML generation for responses:** Build strings with `std::string` or `std::ostringstream`. Do not use an XML library for output — string building is sufficient and avoids dependencies.

### 4. Media Source Manager (`media_sources.cpp / media_sources.h`)

Manages the list of configured media source paths.

**Data structure:**
```cpp
struct MediaSource {
    std::wstring path;       // absolute Windows path e.g. L"C:\\Users\\user\\Videos"
    bool enabled;
};
std::vector<MediaSource> g_sources;
```

**Path validation:** On add, verify the path exists with `GetFileAttributesW`. If it does not exist, show an error but still allow saving it (path may become valid later).

**Recursive file enumeration:** Use `FindFirstFileW` / `FindNextFileW`. Filter out hidden files (`FILE_ATTRIBUTE_HIDDEN`) and system files (`FILE_ATTRIBUTE_SYSTEM`). Recurse into subdirectories. Only yield files whose extensions appear in the MIME type table.

**No SMB/FTP path translation:** Users enter Windows paths directly. The UI uses a native folder picker (`SHBrowseForFolderW` or `IFileOpenDialog` with `FOS_PICKFOLDERS`). No path translation layer is needed — this is a pure Windows app.

### 5. Configuration / Persistence (`config.cpp / config.h`)

Persist settings to `%APPDATA%\WinDLNAServer\config.ini` using `WritePrivateProfileStringW` / `GetPrivateProfileStringW`.

**Settings stored:**

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `ServerName` | string | `WinDLNA Server` | Friendly name advertised via SSDP |
| `Port` | int | `8200` | HTTP server port |
| `FileServerPort` | int | `8201` | Secondary file server port (reserved for future use) |
| `FlatFolderStyle` | bool | `false` | If true, flatten all media into a single list |
| `ShowFileNamesInsteadOfTitles` | bool | `false` | Use raw filename as title |
| `ProxyStreams` | bool | `false` | Reserved |
| `SortByTitle` | bool | `false` | Sort items alphabetically by title instead of filename |
| `DoNotShowAllMediaFolders` | bool | `false` | Suppress the virtual "All Media" root folder |
| `AddArtistAlbumFolders` | bool | `false` | Group audio under Artist/Album virtual containers |
| `DebugLog` | bool | `false` | Write verbose log to `%APPDATA%\WinDLNAServer\debug.log` |
| `IPWhiteList` | string | `` | Comma-separated list of allowed client IPs; empty = allow all |
| `DeviceUUID` | string | (generated) | Persisted UUID for this server instance |
| `RunOnBoot` | bool | `false` | Whether to register with Windows startup |
| `MediaSources` | string | `` | Pipe-delimited list of paths e.g. `C:\Videos|C:\Music` |

**Run on Boot:** Use the Windows Registry key `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`. On enable: write `WinDLNAServer` = `"<exe path>" --minimized`. On disable: delete the value. Use `RegSetValueExW` / `RegDeleteValueW`.

### 6. IP White List (`ipwhitelist.cpp / ipwhitelist.h`)

- Parse the comma-separated `IPWhiteList` config string into a `std::vector<std::string>`.
- On each incoming HTTP connection, call `getpeername` to get the client IP.
- If the whitelist is non-empty and the client IP is not in it, close the connection immediately and return HTTP 403.
- If the whitelist is empty, allow all.

### 7. Log Module (`log.cpp / log.h`)

- Thread-safe log function: `void Log(const wchar_t* fmt, ...)` using `CRITICAL_SECTION`.
- Always append to the in-memory log buffer (ring buffer, max 1000 lines).
- If `DebugLog` is true, also write to `%APPDATA%\WinDLNAServer\debug.log`.
- The "View log" settings action opens a modal dialog (`DialogBox`) showing the in-memory log content in a read-only `EDIT` control with `WS_VSCROLL | ES_MULTILINE | ES_READONLY`.

---

## GUI Specification (Win32, No MFC)

### Main Window

- Class name: `WinDLNAServerMain`
- Style: `WS_OVERLAPPEDWINDOW`, resizable.
- Title bar: `DLNA Server`
- Dark theme: set background brush to `RGB(30, 30, 30)`. Handle `WM_CTLCOLORSTATIC`, `WM_CTLCOLOREDIT`, `WM_CTLCOLORBTN` to return dark background and light text (`RGB(220,220,220)`).
- **Toolbar area (top, fixed height 48px):**
  - Left: static text `DLNA Server` (large, bold).
  - Right (3 buttons, icon-sized, flat style): **Add** (`+`), **Start/Stop** (▶/■), **Settings** (⚙).
  - Implement as `BUTTON` controls with `BS_FLAT` and owner-draw, or use Unicode glyphs as button labels with `Segoe UI Symbol` font.
- **Status bar (below toolbar, fixed height 24px):**
  - Shows either `DLNA Server is stopped` or `DLNA Server is running on {IP}:{port}`.
- **Info text (below status, if no sources added):**
  - Static text: `Please add shared folders or files (button "+")`.
- **Source list area (scrollable, fills remaining height):**
  - Each media source is a row containing:
    - A text field (`EDIT` control, read-only display) showing the Windows path.
    - Three small buttons: **Minimize/Collapse** (`—`, toggles the row to compact), **Remove** (`×`), **Browse** (folder icon or `[...]`) which opens a folder picker.
  - Implement as a custom control or a `LISTBOX` with owner-draw. Owner-draw is simpler: handle `WM_DRAWITEM`.
  - When a source row is clicked, allow editing the path inline in the `EDIT` control.

### Settings Dialog

Modal dialog (`DialogBox`). Scrollable content (use a child `SCROLLBAR` or embed controls in a scrollable panel).

**Controls (map exactly to the settings keys above):**

| Control | Type | Bound Setting |
|---------|------|---------------|
| Run on boot | `BS_AUTOCHECKBOX` | `RunOnBoot` |
| Show video thumbnails | `BS_AUTOCHECKBOX` | (disabled/greyed out — deferred) |
| Show audio album art | `BS_AUTOCHECKBOX` | (disabled/greyed out — deferred) |
| Show image thumbnails | `BS_AUTOCHECKBOX` | (disabled/greyed out — deferred) |
| Thumbnail quality | `COMBOBOX` | (disabled — deferred) |
| Add folders Artist/Album to audio/video | `BS_AUTOCHECKBOX` | `AddArtistAlbumFolders` |
| Do not show All Media folders | `BS_AUTOCHECKBOX` | `DoNotShowAllMediaFolders` |
| Flat folders style | `BS_AUTOCHECKBOX` | `FlatFolderStyle` |
| Show file names instead of titles | `BS_AUTOCHECKBOX` | `ShowFileNamesInsteadOfTitles` |
| Proxy streams | `BS_AUTOCHECKBOX` | `ProxyStreams` |
| Debug | `BS_AUTOCHECKBOX` | `DebugLog` |
| Sort by title instead of file name | `BS_AUTOCHECKBOX` | `SortByTitle` |
| Custom DLNA server name | `EDIT` | `ServerName` |
| Custom DLNA port | `EDIT` (numeric) | `Port` |
| Custom fileserver port | `EDIT` (numeric) | `FileServerPort` |
| IP white list | `EDIT` | `IPWhiteList` |
| View log | `BUTTON` | Opens log dialog |
| Restart | `BUTTON` | Stops then starts server engine |
| OK | `BUTTON` | Saves all settings and closes |

Settings marked with `*` in the original app (require restart) should show a note: `* Server restart needed` at the bottom of the dialog, as a greyed static text.

Thumbnail-related controls must be present but disabled (`EnableWindow(hCtl, FALSE)`) with a tooltip: `Thumbnail support coming in a future version`.

---

## Server Start / Stop Logic

**Start sequence:**
1. Load config and source list.
2. Validate at least one source path exists. If none exist, show messagebox and abort.
3. Detect local IPv4 address: enumerate adapters with `GetAdaptersInfo` or `GetAdaptersAddresses`. Pick the first non-loopback, non-APIPA address. This is the IP advertised in SSDP `LOCATION` and in media URLs.
4. Start HTTP server thread (bind socket, begin accept loop).
5. Start SSDP thread (join multicast, begin listen loop; send initial `ssdp:alive` NOTIFY burst of 3).
6. Update UI: set status bar text to running state, change Start button to Stop (■).

**Stop sequence:**
1. Set a global `std::atomic<bool> g_running = false`.
2. Signal SSDP thread: send `ssdp:byebye` NOTIFY, then exit.
3. Signal HTTP thread: close listen socket to unblock `accept()`, then join thread.
4. Join worker threads.
5. Update UI: status bar shows stopped, button reverts to Start (▶).

**Server restart** (from settings dialog): stop then start.

---

## System Tray Integration

- Add a tray icon using `Shell_NotifyIcon` with `NIM_ADD` on startup.
- Icon: use a custom `.ico` embedded as a resource. Fallback: use `IDI_APPLICATION`.
- Tray right-click menu (`TrackPopupMenu`): items: `Show Window`, `Start Server`, `Stop Server`, `Exit`.
- On window `WM_CLOSE`: minimize to tray (hide window with `ShowWindow(hWnd, SW_HIDE)`) rather than destroying. Destroy only on Exit from tray menu.
- Double-clicking the tray icon restores the window.

---

## Startup Behavior

- If launched with `--minimized` argument (the case when started at boot via registry): call `ShowWindow(hWnd, SW_HIDE)` after creating the window, and auto-start the server engine.
- Otherwise: show window normally. Server starts only when user clicks Start (▶), or if it was running when the app last closed (persist last-run state in config as `AutoResume=1`).

---

## Functional Requirements Summary (Exact Feature Parity with Reference App)

| # | Requirement |
|---|-------------|
| FR-01 | User can add one or more Windows folder paths as media sources |
| FR-02 | Each source row has: path display/edit field, remove button (×), folder browser button |
| FR-03 | Folder browser uses native Windows `IFileOpenDialog` with `FOS_PICKFOLDERS` |
| FR-04 | Server can be started and stopped with a toolbar button |
| FR-05 | Server status (running/stopped + IP:port) is shown in the UI |
| FR-06 | DLNA clients on the LAN can discover the server via SSDP (M-SEARCH and NOTIFY) |
| FR-07 | DLNA clients can browse folders and files via UPnP ContentDirectory:1 Browse SOAP action |
| FR-08 | DLNA clients can stream video, audio, and image files over HTTP with range request support |
| FR-09 | Settings: configurable server friendly name |
| FR-10 | Settings: configurable HTTP port |
| FR-11 | Settings: configurable secondary file server port |
| FR-12 | Settings: IP whitelist (comma-separated; empty = allow all) |
| FR-13 | Settings: Run on Windows startup (via registry `HKCU\...\Run`) |
| FR-14 | Settings: Flat folder style (collapse hierarchy into flat file list) |
| FR-15 | Settings: Show file names instead of derived titles |
| FR-16 | Settings: Sort by title instead of file name |
| FR-17 | Settings: Do not show All Media folders virtual root |
| FR-18 | Settings: Add Artist/Album virtual folder grouping for audio |
| FR-19 | Settings: Proxy streams toggle (reserved, UI present but no-op) |
| FR-20 | Settings: Debug mode (verbose log to file) |
| FR-21 | Settings: View log (in-app log viewer dialog) |
| FR-22 | Settings: Restart server button |
| FR-23 | Application minimizes to system tray on window close |
| FR-24 | When launched with `--minimized`, starts hidden and auto-starts server |
| FR-25 | Thumbnail UI controls present but disabled (deferred feature) |
| FR-26 | Settings persist across sessions in `%APPDATA%\WinDLNAServer\config.ini` |
| FR-27 | A stable UUID is generated once and reused for SSDP device identity |

---

## Non-Functional Requirements

- **No third-party libraries** except optionally `tinyxml2` (header-only, MIT) if MSXML is inconvenient. All networking: WinSock2. All UI: Win32. All filesystem: Win32 API (`FindFirstFileW` etc.).
- **Unicode throughout:** use `wchar_t` and wide API (`W` suffix functions) for all file paths and registry access.
- **Minimum Windows target:** Windows 10 (version 1903+). Use `#define WINVER 0x0A00`.
- **Single executable output.** Link CRT statically (`/MT`). No installer required for basic use.
- **Thread safety:** all accesses to `g_sources` and the object-ID map must be protected by a mutex since the HTTP server threads read them concurrently.
- **Graceful shutdown:** on `WM_DESTROY` or tray Exit, always send `ssdp:byebye` and close all sockets before process exit.

---

## File / Project Structure

```
WinDLNAServer/
├── CMakeLists.txt
├── resources/
│   ├── resource.h
│   ├── app.rc
│   └── app.ico
├── src/
│   ├── main.cpp          # WinMain, message loop, tray
│   ├── mainwindow.cpp    # Main window proc, source list UI
│   ├── mainwindow.h
│   ├── settingsdlg.cpp   # Settings dialog proc
│   ├── settingsdlg.h
│   ├── logdlg.cpp        # Log viewer dialog
│   ├── logdlg.h
│   ├── server.cpp        # Start/stop orchestration
│   ├── server.h
│   ├── ssdp.cpp
│   ├── ssdp.h
│   ├── httpserver.cpp
│   ├── httpserver.h
│   ├── contentdirectory.cpp
│   ├── contentdirectory.h
│   ├── media_sources.cpp
│   ├── media_sources.h
│   ├── config.cpp
│   ├── config.h
│   ├── ipwhitelist.cpp
│   ├── ipwhitelist.h
│   ├── log.cpp
│   └── log.h
```

---

## CMakeLists.txt Requirements

```cmake
cmake_minimum_required(VERSION 3.20)
project(WinDLNAServer)
set(CMAKE_CXX_STANDARD 17)
add_executable(WinDLNAServer WIN32 ...)  # WIN32 = no console window
target_link_libraries(WinDLNAServer ws2_32 shell32 ole32 oleaut32 uuid comctl32 shlwapi)
# Static CRT:
set_property(TARGET WinDLNAServer PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
```

---

## Known Protocol Pitfalls (from open-source reference implementations)

These are failure modes documented in SimpleDLNA, DMS, and UMS that the agent must handle:

1. **HTTP header field names are case-insensitive.** Always compare header names with `_stricmp`.
2. **SOAP action name case:** Some clients send `SOAPACTION` as `urn:schemas-upnp-org:service:ContentDirectory:1#browse` (lowercase `b`). Handle case-insensitively.
3. **Range request `Range: bytes=0-`** (open-ended range): serve from offset 0 to end of file. This is the most common seek pattern. Return HTTP 206 for any range request, HTTP 200 for full-file requests.
4. **SSDP M-SEARCH `MX` header:** respect the `MX` delay value (wait a random time between 0 and MX seconds before responding). Prevents thundering herd on initial network scan.
5. **Multiple network interfaces:** join the SSDP multicast group on each interface separately. Advertise a single `LOCATION` IP (the one most likely on the same subnet as clients — pick the one with the longest common prefix with the first responding client, or just pick the first non-loopback adapter).
6. **DIDL-Lite XML escaping:** the `Result` field in BrowseResponse is XML text containing embedded XML. The inner XML must be properly escaped in the outer XML. Build it as a string, then XML-escape it before embedding.
7. **`BrowseMetadata` vs `BrowseDirectChildren`:** `BrowseMetadata` returns info about the object itself (as if it were a child of its parent). `BrowseDirectChildren` returns its children. Both must be implemented.
8. **`childCount` attribute:** for folders, `childCount` should reflect the actual number of direct children (filtered by media type). Compute this by scanning the directory.
9. **`UpdateID` / `SystemUpdateID`:** maintain a global counter, incremented when media sources change. Return it in Browse responses. Clients use this to detect stale caches.
10. **Content-Length vs chunked:** do not use chunked transfer for media files. Always send `Content-Length`. Some TV DLNA clients do not support chunked encoding.
