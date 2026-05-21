# Linux GUI Parity Blueprint

Native Linux GUI target: FLTK C++ app matching current Windows Win32 behavior. Keep `dlna-server` as server binary and add a separate GUI launcher target that reads/writes the same `config.ini` schema through existing config code.

## Main Window Inventory

- Window title: `dlna-server`
- Initial size: 800 x 600
- Minimum supported size: 640 x 460
- Background: dark shell, toolbar band, status band, source list
- Toolbar height: 48 px
- Status strip: y 48-72 px
- Source list: y 72 to bottom, full width
- Title text: `dlna-server`, Segoe UI 24 px bold equivalent
- Add button: plus symbol, opens folder picker
- Start/Stop button: play symbol when stopped, stop symbol when running
- Settings button: gear symbol, opens settings dialog
- Empty-state text: `Please add shared folders or files (button "+")`
- Status stopped text: `dlna-server is stopped`
- Status running text: `dlna-server is running on {endpoint}`
- Source list contents: one row per configured media source path
- Close behavior: hide to tray/status notifier when available; graceful quit-visible fallback when not available
- Tray/status notifier menu: `Show Window`, `Start Server` or `Stop Server`, `Exit`
- Tray/status notifier activation: show/restore window

## Settings Dialog Inventory

- Dialog title: `dlna-server Settings`
- Windows resource size: 320 x 280 dialog units
- Bottom actions: `Restart`, `View log`, `Cancel`, `OK`
- Text field: `Server Name:`
- Numeric field: `HTTP Port:`
- Numeric field: `File Port:`
- Text field: `IP Whitelist:`
- Checkbox: `Run on Windows Startup`
- Checkbox: `Debug Log (Write to file)`
- Checkbox: `Add Artist/Album folders to audio`
- Checkbox: `Do not show 'All Media' folders`
- Checkbox: `Flat folders style`
- Checkbox: `Show file names instead of titles`
- Checkbox: `Sort by title instead of file name`
- Checkbox: `Proxy streams`
- Disabled checkbox: `Show video thumbnails`
- Disabled checkbox: `Show audio album art`
- Disabled checkbox: `Show image thumbnails`
- Disabled combo: `Thumbnail quality:`
- Footer text: `* Server restart needed`

Settings load from `AppConfig` on dialog open. `OK` writes every enabled setting and calls `AppConfig.Save()`. `Cancel` closes without saving. `View log` opens the log dialog. `Restart` remains wired as a visible control; implementation can restart server once FLTK server orchestration is connected.

## Log Dialog Inventory

- Dialog title: `dlna-server Log`
- Size target: 400 x 300 dialog units or equivalent
- Main control: read-only multi-line text view
- Text source: `GetSystemLog()`
- Open behavior: scroll to bottom
- Action: `Close`

## Behavior Checklist

- Adding a folder appends an enabled media source, saves config, refreshes list, and rescans media.
- Starting server calls existing server start path and updates status/control state on success.
- Stopping server calls existing server stop path and updates status/control state.
- Settings `OK` round-trips all current config fields.
- Log dialog displays current in-memory/file-backed log text and remains read-only.
- Close/hide behavior does not stop server unless user chooses `Exit`.
- AppImage entry starts GUI, bundles server binary, desktop file, icon, and needed shared libraries.
- Desktop metadata includes `Name`, `Exec`, and `Icon`.

## Visual/Usability Checklist

- No clipped text at 640 x 460 or larger.
- Toolbar buttons keep stable square dimensions and right alignment.
- Status text truncates gracefully if endpoint is long.
- Source list stays readable with long paths.
- Disabled thumbnail controls remain visible but unmistakably disabled.
- Keyboard tab order follows visible order.
- Tooltips exist for plus, start/stop, settings, restart, view log, and disabled thumbnail controls.
