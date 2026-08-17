# GTK4 Windows 10 Explorer-Style UI Design Guide

## 1. Purpose

This document defines the visual contract for the POSIX GTK4 GUI of `dlna-server`.

The target is the Windows 10 Explorer.exe visual language shown by the supplied project screenshots, not a generic “Windows-like” GTK interface.

The supplied Windows screenshots are the primary visual reference for geometry, alignment, control placement, titlebar treatment, spacing, and window chrome. The Win32 implementation and `app.rc` are the secondary application-specific references. GTK4 documentation and established Windows 10 GTK themes are the implementation references.

The POSIX implementation must reproduce the observable layout rather than redesigning it for GTK.

## 2. Source-of-Truth Order

Use the following order when sources disagree:

1. Supplied Windows 10 screenshots.
2. Existing Win32 implementation and resource definitions.
3. Windows 10 Explorer.exe conventions.
4. GTK4 official documentation for how the required appearance must be implemented.
5. B00merang Windows 10 Dark GTK4 theme as a reference for GTK widget styling and control geometry.
6. B00merang Windows 10 GTK4 theme as a secondary reference for Windows 10 control relationships.

The B00merang Windows-10-Dark repository explicitly provides a dark Windows 10 GTK theme and contains GTK4 material. The Windows-10 repository also contains GTK4 material and documents its purpose as reproducing the appearance of Win32 applications on Windows 10. Use these repositories as implementation references, not as a complete stylesheet to copy into the application.

## 3. Window Geometry

The application uses client-side decorations in GTK4.

The visual result must have:

- zero application content padding at the outer window boundary;
- zero unintended outer margins;
- no rounded application-window corners introduced by GTK CSS;
- no decorative shadow inside the application content;
- no visible GTK theme border that is absent from the Windows reference;
- only the minimum invisible resize area required by GTK/window-manager operation;
- content aligned to the same coordinates and visual edges shown in the Windows screenshots.

Do not solve geometry differences by adding arbitrary margins to individual controls.

Establish the outer geometry first, then position child controls relative to that geometry.

The GTK window documentation distinguishes ordinary client-side decoration from solid CSD and notes that invisible CSD shadow/resize areas can exist outside the visible window. This means “thin invisible edges” must not be implemented as a visible border.

## 4. Titlebar

Every application-owned top-level window must use one reusable titlebar implementation.

The titlebar must be created by a single helper such as:

```cpp
GtkWidget* CreateWin10Titlebar(GtkWindow* window,
                               const char* title,
                               WindowChrome chrome);
```

The helper must create:

- `GtkHeaderBar`;
- the title widget;
- `GtkWindowControls`;
- active/inactive state tracking;
- the requested control layout.

GTK4 officially documents `GtkHeaderBar` as the natural custom titlebar widget and `gtk_window_set_titlebar()` as the API for assigning it. `GtkWindowControls` exposes the titlebar controls as CSS nodes including `button.minimize`, `button.maximize`, and `button.close`.

Do not implement separate titlebar construction for Settings, Log, Help, Playlist Entry, Add Media Source, and Main.

## 5. Titlebar Colors

Use these exact application values:

```text
Active:   #313131
Inactive: #323232
```

The active state is determined from `GtkWindow:is-active`.

Use two mutually exclusive classes:

```text
win10-active
win10-inactive
```

Expected CSS structure:

```css
.win10-titlebar {
    min-height: <measured Windows reference height>;
    padding: 0;
    margin: 0;
    border: none;
    border-radius: 0;
    box-shadow: none;
}

.win10-titlebar.win10-active {
    background-color: #313131;
}

.win10-titlebar.win10-inactive {
    background-color: #323232;
}
```

Do not use a gradient.

Do not rely on the desktop theme to select the required colors.

Do not use `.backdrop` as the application's source of truth for active/inactive state.

## 6. Titlebar Controls

The Windows 10 reference determines which controls exist.

Main window:

```text
Minimize | Close
```

Subwindows:

```text
Close
```

No application-owned dialog may expose minimize or maximize.

The main window remains resizable, but it must not expose a maximize control.

Use `GtkWindowControls` with explicit decoration layouts:

```cpp
":minimize,close"
```

for Main and:

```cpp
":close"
```

for dialogs.

Do not manually draw fake titlebar buttons when `GtkWindowControls` can provide the required controls.

The GTK4 documentation exposes the relevant CSS nodes:

```text
windowcontrols
├── image.icon
├── button.minimize
├── button.maximize
└── button.close
```

Use those nodes for styling rather than depending on theme-specific widget names.

## 7. Window Icons

The application already has an application icon in the Win32 resources:

```text
IDI_APP_ICON
```

and server icon resources at:

```text
48x48
120x120
256x256
```

The POSIX implementation must use the corresponding application icon asset wherever GTK supports a window/application icon.

Do not substitute an arbitrary symbolic icon.

Do not put a decorative icon into the titlebar merely because GTK permits one. The supplied Windows reference determines whether the icon is visually present in the target position.

Use the supplied Windows icon resources and existing application branding as the source material.

The B00merang Windows 10 icon project is a secondary reference for Windows 10 icon geometry and scaling. It contains Windows 10-derived icon sizes including 16, 22, 24, 32, 48, 128, 256 and scalable assets.

## 8. Title Text

The title must use the GTK `title` style class.

The title position must reproduce the supplied Windows screenshot.

Do not add a second visible application title inside the content toolbar.

For the main window, the visible title belongs to the titlebar.

For dialogs, the titlebar title is the only visible window title.

The title should not be vertically displaced by GTK's default headerbar padding.

## 9. Settings Window

The Settings screenshot is the direct layout reference.

The white content surface in the supplied Settings screenshot is not a design requirement for the POSIX build. It is a Windows limitation/reference-state artifact. Preserve the requested Windows 10 geometry and control alignment while applying the POSIX dark-mode surface.

The following must remain aligned as shown in the reference:

- Logs and Help controls;
- Server group;
- Server name label and entry;
- HTTP port label and entry;
- IP whitelist label and entry;
- General group;
- Playlist group;
- Media browsing group;
- all checkboxes;
- Add button;
- Cancel;
- OK.

Text must use the same left edges and column relationships as the reference.

Do not center labels merely because GTK defaults make that convenient.

Do not let widget natural sizes change the intended coordinates.

## 10. Settings Close Button Rule

Settings must have exactly one window close control.

The titlebar must provide the window close button.

Do not create a second close button solely to compensate for GTK titlebar behavior.

The existing content-level Cancel and OK buttons are functional dialog controls and are not replacements for the titlebar close control.

The visual defect to correct is specifically duplicate titlebar/window close chrome.

The coding workflow must inspect the widget tree and CSS nodes to establish why the duplicate close control exists before deleting anything.

## 11. Main Window Toolbar

The main screenshot contains:

- application title area;
- Add;
- Delete;
- Start/Stop;
- Settings;
- status line;
- source list.

The titlebar and application toolbar are separate surfaces.

Do not move Add, Delete, Start/Stop, or Settings into the titlebar.

The main application toolbar must preserve the horizontal alignment shown in the Windows screenshot.

The source list must begin below the toolbar/status region.

## 12. Main Source List

The source list must never overlap the toolbar/button section.

The list's top edge must be computed from:

```text
titlebar/content origin
+ application toolbar height
+ status area height
+ required gap
+ focus-ring allowance
```

The list bottom edge must remain inside the window content area.

Do not calculate list height from the raw window height without accounting for every region above and below it.

The supplied Windows screenshot is the geometry authority.

The POSIX implementation currently computes the source-list rectangle in `LayoutMainWindow()`. This calculation must be reviewed against the actual widget hierarchy and measured window geometry rather than adjusted with a random negative/positive offset.

The list must remain below the buttons even when the main window is resized.

## 13. List Box Appearance

The source list should resemble the Windows list area:

- flat rectangular surface;
- no unnecessary rounded card treatment;
- no large outer padding;
- row text aligned consistently to the Windows reference;
- focus indication limited to the intended focus state;
- no list content extending into the toolbar.

Do not use a generic GTK card/list design.

The list is a Windows Explorer-style content region.

## 14. Text Alignment

Treat the screenshots as coordinate references.

For every visible text element compare:

- left edge;
- baseline;
- vertical center;
- spacing from neighboring controls;
- relationship to group-box edges;
- relationship to window edges.

Do not judge alignment only by approximate visual centering.

The Win32 resource definitions provide explicit control rectangles and are useful for recovering the intended coordinate relationships.

For example, `app.rc` defines the Settings dialog with explicit positions for labels, edits, groups, checkboxes and buttons. Those relationships should be preserved when GTK4 widgets are used.

## 15. Fonts

The Win32 resource uses:

```text
Segoe UI
10 pt
normal weight
```

The GTK4 implementation should use the closest available Segoe UI-equivalent configuration on the target environment, while preserving the screenshot's measured size and baseline relationships.

Do not introduce a larger GTK default font into controls that are supposed to match the Windows reference.

Do not change font weight merely to improve perceived readability.

## 16. Buttons

Buttons should follow the Windows 10 reference rather than the default GTK theme.

Required characteristics:

- rectangular geometry;
- consistent height;
- no unnecessary rounded corners;
- compact horizontal padding;
- consistent gaps;
- predictable hover/active/focus states;
- text centered in the same visual position as the reference.

The external Windows 10 GTK themes should be inspected for control-state relationships before writing application CSS.

Do not copy unrelated theme-wide styling.

## 17. Group Boxes

Settings group boxes must preserve the Win32 layout relationships.

The Windows resource defines:

```text
Server
General
Playlist
Media browsing
```

with explicit rectangles.

GTK's `GtkFrame` may be used, but its default border, label gap, padding and corner treatment must be overridden if they differ from the reference.

The group label must occupy the same visual position relative to the border.

Do not replace the group-box layout with modern GTK cards.

## 18. CSS Architecture

Shared Windows 10 styling belongs in the application's main GTK stylesheet.

Use one definition for:

```text
.win10-titlebar
.win10-titlebar.win10-active
.win10-titlebar.win10-inactive
```

Do not maintain a second copy in an inline dialog CSS provider.

Dialog-specific CSS may remain local only when it is genuinely dialog-specific.

The same titlebar CSS must serve Main, Settings, Log, Help, Playlist Entry, Add Media Source, and application-owned message windows.

## 19. GTK Theme Isolation

The application must not assume that the host desktop GTK theme already looks like Windows 10.

Explicitly define the visual properties that matter to the target:

- titlebar colors;
- titlebar height;
- title text;
- control geometry;
- button states;
- window border;
- list surface;
- list rows;
- group boxes;
- entries;
- checkboxes;
- application toolbar;
- spacing.

Use GTK CSS only for properties GTK actually exposes.

GTK CSS operates on a widget CSS-node tree. The actual node hierarchy must be checked against the supported GTK version before writing selectors.

## 20. GTK Version Constraint

The source notes that the project targets GTK 4.6 compatibility.

Do not introduce APIs unavailable to the supported minimum GTK version merely because current GTK documentation contains them.

For example, `GtkHeaderBar:use-native-controls` is documented as available since GTK 4.18. It must not be used as a required mechanism for a GTK 4.6-compatible implementation.

Use APIs already available in the project's supported version unless the build target is deliberately changed.

## 21. Reference Theme Repositories

Use:

- B00merang Windows 10 Dark for dark-mode GTK styling.
- B00merang Windows 10 for Windows 10 GTK4 widget relationships.
- B00merang Windows 10 icon theme for icon geometry and available Windows-derived icon assets.

Do not import their complete theme into the application.

Extract only evidence relevant to:

- titlebar geometry;
- button geometry;
- checkbox appearance;
- entry geometry;
- list rows;
- borders;
- spacing;
- hover/pressed states;
- icon scale.

## 22. Explorer.exe Interpretation

“Windows 10 UI” means the visual system used by Windows 10 Explorer.exe, not Windows 11 Fluent UI and not generic GNOME/GTK design.

Therefore do not introduce:

- Windows 11 rounded corners;
- Fluent-style large spacing;
- pill controls;
- modern GTK cards;
- thick borders;
- excessive shadows;
- large title text;
- oversized toolbar controls.

The target is compact Windows 10 desktop geometry.

## 23. Validation

Every UI change must be validated against the supplied screenshots.

Compare:

1. outer window rectangle;
2. titlebar height;
3. title position;
4. close/minimize positions;
5. toolbar height;
6. toolbar button positions;
7. status position;
8. source-list top;
9. source-list bottom;
10. Settings group rectangles;
11. Settings text columns;
12. Settings buttons;
13. window border/edge visibility.

Use the existing geometry-dump infrastructure where possible.

Do not accept “looks close” as the validation criterion.

## 24. Required End State

The GTK4 GUI must have:

```text
Windows 10 Explorer-style geometry
Windows 10 dark-mode titlebar
Active titlebar: #313131
Inactive titlebar: #323232
Zero visible outer padding
Zero visible outer margin
Thin/invisible resize edges
Single reusable titlebar implementation
One close control per application window
Main: minimize + close
Dialogs: close only
Correct Windows-derived application icon
Correct text alignment
Correct Settings geometry
Source list completely below toolbar/buttons
No overlap during resize
No duplicate titlebar implementations
No duplicated titlebar CSS
```

