# Desktop UI Spec

Scope: Windows Win32 UI and Linux FLTK UI. Keep both quiet, compact, and utility-first. Windows shell metrics should follow WinUI/Fluent guidance where practical in raw Win32.

## Shell

- Window title: `dlna-server`
- Minimum content size: 640 x 460
- Default size: 800 x 600
- Toolbar height: 56 px
- Status strip height: 40 px
- Source list starts below status strip and fills remaining space
- Theme adapts to Windows light/dark app mode where platform APIs allow it
- Use semantic neutral colors, with accent color only for focus, selection, and primary action states
- Do not hardcode a dark-only shell; all custom drawing must consume theme tokens
- Windows UI font: Segoe UI Variable

## Controls

- Toolbar buttons are 40 x 40 px, right-aligned, 8 px from top
- Toolbar button spacing is 8 px, with a 24 px right gutter
- Toolbar buttons use a 1 px border and 8 px corner radius
- Toolbar button order: Add, Start/Stop, Settings
- Add button tooltip: `Add media folder`
- Start button tooltip: `Start server`
- Stop button tooltip: `Stop server`
- Settings button tooltip: `Settings`
- Toolbar title is left-aligned and bold
- Source list paths use single-line rows with clipping or horizontal scroll where platform supports it
- Empty-state text appears only when no sources exist

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
