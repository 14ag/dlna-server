# Desktop UI Spec

Scope: Windows Win32 UI and Linux FLTK UI. Keep both quiet, compact, and utility-first. Windows shell metrics should follow WinUI/Fluent guidance where practical in raw Win32.

## Shell

- Window title: `dlna-server`
- Minimum content size: 640 x 460
- Default size: 800 x 600
- Toolbar height: 56 px
- Status strip height: 40 px
- Source list starts below status strip and fills remaining space
- Shell is dark, and Windows enables the dark DWM title bar/frame
- Windows settings, log, add-source, and default-playlist dialogs use normal system dialog controls
- WinUI-style spacing uses 16 px outer margins, 12 px label-to-control gaps, and 8 px gaps between related controls
- Windows dialog templates use 10 pt Segoe UI; dynamic Win32 dialog controls use 14 px Segoe UI Variable Text with system fallback
- Parent surfaces own layout: shell/status/list areas stretch with the window; dialogs use fixed content surfaces sized to their controls
- Avoid fixed text-field widths unless the surface itself is fixed; long text clips or scrolls inside the text field instead of resizing neighboring controls
- Use semantic neutral colors, with accent color only for focus, selection, and primary action states
- Do not hardcode a dark-only shell; all custom drawing must consume theme tokens
- Windows UI font: Segoe UI Variable with Segoe UI fallback, with semibold weight for the shell title

## Controls

- Toolbar buttons are 32 px tall, text-labeled, right-aligned, and vertically centered
- Toolbar button spacing is 8 px, with a 16 px right gutter
- Toolbar buttons use a 1 px border and 8 px corner radius
- Toolbar button order: Add, Delete, Start/Stop, Settings
- Add button tooltip: `Add media folder`
- Delete button tooltip: `Delete selected source`
- Start button tooltip: `Start server`
- Stop button tooltip: `Stop server`
- Settings button tooltip: `Settings`
- Toolbar title is left-aligned and bold
- Source list paths use single-line rows with clipping or horizontal scroll where platform supports it
- Empty-state text appears only when no sources exist
- Delete removes the selected source entry only; it never deletes files, folders, or playlists from disk
- Delete is disabled while busy or when no source row is selected; keyboard `Delete` performs the same action

## Settings Dialog

- Controls follow Windows order exactly: identity, ports, whitelist, default playlist, toggles, actions
- Default playlist has a checkbox plus an `Add...` button that is enabled only while checked
- Default playlist entry form has `Movie path` and `Subtitle path` fields, each with a browse button, plus one `Add` button
- UPnP device icons are bundled PNG files at 48, 120, and 256 px and are advertised through `iconList`
- `Restart`, `View log`, `Cancel`, and `OK` stay pinned to dialog bottom
- Numeric fields accept only integer ports
- `OK` saves, `Cancel` discards, `View log` opens current log

## Log Dialog

- Log text is read-only and multi-line
- Dialog opens scrolled to bottom
- `Close` is the only action

## Layout Rules

- Fixed Windows dimensions, margins, and padding use 4 px increments
- No clipped labels at 640 x 460
- Long endpoint/status text clips instead of overlapping buttons
- Start/stop busy text is `starting server...` or `stopping server...`
- Toolbar button positions remain stable during resize
- Dialog tab order follows visual reading order
- Disabled controls have native disabled styling
- Windows modal sub-windows are owned by their parent, do not create separate taskbar entries, return focus to their owner on close, and preserve app quit messages if exit occurs while a sub-window is open
- Settings, log, add-source, and default-playlist windows opt into the same dark DWM frame as the main shell
