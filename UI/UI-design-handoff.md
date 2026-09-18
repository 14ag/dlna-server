# GTK4 UI Design Handoff

Use live Penpot MCP data. Do not guess sizes, colors, fonts, or state colors.
Boards on Page 1 are:

- `dlna-main-active`: 426 x 593
- `dlna-settings`: 700 x 797
- `dlna-logs`: 772 x 712
- `dlna-help`: 530 x 400
- `dlna-add-media-source`: 538 x 209
- `dlna-default-playlist-entry`: 538 x 189
- `dlna-warning-dialog`: 245 x 150

All sizes above include the 30 px title bar. GTK default window size must use
the body height, not the board height. Keep both values in
`src/ui_tokens_posix.h`. For example, a 538 x 209 board needs a 538 x 179
GTK child plus the 30 px custom title bar. Using 209 as the GTK child height
creates a 538 x 239 window.

## Title bar

Every board uses the same 30 px custom title bar.

- Use `gtk_window_set_titlebar()` with `CreateWin10Titlebar()`.
- Do not use `GtkHeaderBar` or `GtkWindowControls`.
- Do not call `gtk_window_set_decorated(window, FALSE)`. GTK then gave the
  custom title bar zero size, so it disappeared.
- Keep `GtkWindowHandle`, a left box, a growing spacer, and custom `GtkButton`
  controls.
- Main board controls are 46 x 30 at x=288, 334, and 380. Maximize is disabled
  by design. Dialog boards only have the 46 x 30 close control at the right.
- Main board has a 16 x 16 custom app icon at x=10 and title at x=35. Dialog
  boards use only title text at x=10.
- Control graphics come from `penpot-minimize.svg`, `penpot-maximize.svg`, and
  `penpot-close.svg` through CSS `background-image`. Do not put `GtkImage`
  inside title-bar buttons.

## Fonts and colors

- Penpot uses Inter for normal text. It overrides the old Segoe UI request for
  this strict Penpot implementation.
- Log and Help content use Cousine, 12 px, monospace.
- Title bar: `#191919`; inactive title bar: `#202020`.
- Main toolbar: `#252525`; main list area: `#1f1f1f`.
- Main toolbar buttons: `#333333`, 0.5 px `#555555` border, no radius.
- Dialog body: `#f0f0f0`; normal field: white with `#cccccc` border.
- Dialog buttons: `#e1e1e1`, `#adadad` border, no radius. Source dialog uses
  `#a0a0a0` borders. Focus/default border is `#0078d7`.

## Geometry and GTK CSS traps

- GTK CSS borders reduce a widget's visible allocation. Validate the visible
  geometry, not only `gtk_widget_set_size_request()`.
- Generated `resources/gtk/style.css` gives old minimum button sizes. The
  Penpot overlay must reset `min-width` and `min-height` for main toolbar
  buttons, or their Penpot sizes change.
- Keep Penpot-only values in `src/ui_tokens_posix.h`. Do not edit shared
  `src/ui_tokens.h`, because Windows uses it.
- Custom SVGs install from `CMakeLists.txt`. A new asset must be added there,
  or installed builds will silently use no image.
- B00merang title control PNGs were removed. Do not restore them. They are not
  the Penpot assets.

## Windows profile overrides

The user profile is a source for Windows-like colors where Penpot does not
show a state. Keep a 1 px `#606060` CSD frame on every window. The shadow is
black at 38% opacity, `0 6px 22px 9px`. It needs 8–10 px of outside space.

- Main title bar stays dark Penpot `#191919`.
- Other active and inactive title bars use profile `#202020`.
- Out-of-focus Minimize and Close icons use 50% opacity. Active icons use
  white at full opacity. The disabled Maximize button remains disabled.
- `AppWorkspace` is `#191919`; it is the 3 px dark area around Log's white
  text box.
- Main list stroke is 0.5 px `#434343`.
- Group box labels need their `#f0f0f0` background and 4 px side padding, so
  the text sits over the group-box border rather than inside its face.

## Warning dialog

The warning board uses a custom `penpot-warning.svg`: 32 x 38, yellow triangle.
It is placed in a white 245 x 76 message area. Footer is `#f0f0f0`, 44 px high,
with an OK button 73 x 22. Do not use GTK's `dialog-warning-symbolic` asset.

## Required validation after UI changes

1. Run the focused Python source-contract tests:
   `python3 -m pytest -q --tb=no tests/test_gtk4_window_presentation.py`
2. Build/install only through the Linux build script required by `AGENTS.md`.
3. Run GTK geometry dump under Xvfb. Check every top-level size, each custom
   title bar at 30 px, and title controls at 46 x 30.
4. Export each changed Penpot board and compare title bar, body color, button
   size, field size, and border color against the GTK screenshot.
5. Inspect installed `output/linux/share/dlna-server/gtk/` for all Penpot SVGs.
