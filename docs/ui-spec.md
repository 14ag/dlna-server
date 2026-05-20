# Desktop UI Spec

Scope: Windows Win32 UI and Linux FLTK UI. Keep both quiet, compact, and utility-first.

## Shell

- Window title: `DLNA Server`
- Minimum content size: 640 x 460
- Default size: 800 x 600
- Toolbar height: 48 px
- Status strip height: 24 px
- Source list starts below status strip and fills remaining space
- Main background: RGB 30, 30, 30
- Toolbar background: RGB 45, 45, 48
- Primary text: RGB 220, 220, 220
- Secondary text: RGB 150, 150, 150
- Selection color target: muted blue RGB 70, 90, 120

## Controls

- Toolbar buttons are 30 x 30 px, right-aligned, 10 px from top
- Toolbar button order: Add, Start/Stop, Settings
- Add button tooltip: `Add media folder`
- Start button tooltip: `Start server`
- Stop button tooltip: `Stop server`
- Settings button tooltip: `Settings`
- Toolbar title is left-aligned and bold
- Source list paths use single-line rows with clipping or horizontal scroll where platform supports it
- Empty-state text appears only when no sources exist

## Settings Dialog

- Controls follow Windows order exactly: identity, ports, whitelist, toggles, thumbnail placeholders, actions
- Thumbnail controls remain visible but disabled until implemented
- `Restart`, `View log`, `Cancel`, and `OK` stay pinned to dialog bottom
- Numeric fields accept only integer ports
- `OK` saves, `Cancel` discards, `View log` opens current log

## Log Dialog

- Log text is read-only, multi-line, monospace where available
- Dialog opens scrolled to bottom
- `Close` is the only action

## Layout Rules

- No clipped labels at 640 x 460
- Long endpoint/status text clips instead of overlapping buttons
- Toolbar button positions remain stable during resize
- Dialog tab order follows visual reading order
- Disabled controls have native disabled styling and useful tooltips
