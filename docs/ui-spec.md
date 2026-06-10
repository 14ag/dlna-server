# Desktop UI Spec

## Scope

This contract covers Windows Win32 UI and Linux FLTK UI surfaces for the desktop app.

## Layout Rules

- Default main window size: 440 x 600
- Minimum content size: 440 x 460
- Toolbar height: 56 px
- Status strip height: 40 px
- Toolbar buttons are 32 px tall
- 8 px corner radius
- Spacing uses 4 px increments
- Parent surfaces own layout
- No clipped labels

## Windows Metrics

- Windows UI font: Segoe UI Variable with Segoe UI fallback
- Windows dialog templates use 10 pt Segoe UI
- Main shell uses WinUI and Fluent aligned spacing, button metrics, dark frame handling, and icon resources.

## Main Commands

- Add media folder
- Delete selected source
- Start server
- Stop server
- Settings

## Dialogs

- Default playlist is editable from Settings.
- Log text is read-only.
- Modal child windows restore focus to their owner only when the app still owns the foreground chain.

## Assets

- UPnP device icons are published for 48, 120, and 256 px sizes.
