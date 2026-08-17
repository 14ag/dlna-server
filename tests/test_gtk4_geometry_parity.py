"""Blackbox geometry parity test for the GTK4 GUI.

Runs the gtk4 binary under xvfb with the hidden `--dump-widget-geometry` hook,
parses the emitted `[gtk4-<tag>-geometry]` lines, and asserts that every
Part-1 window/dialog sub-element's (w,h)/(x,y) matches the canonical constants
in `src/ui_tokens.h` exactly.

Win32 `output/winx64/debug.log` `[xxx-geometry]` captures are cross-checked
when present (the .exe cannot run under WSL, so that leg is skipped gracefully).
"""

import os
import re
import subprocess

import pytest

pytestmark = [pytest.mark.posix_only, pytest.mark.needs_xvfb]

REPO = str(__import__("pathlib").Path(__file__).resolve().parents[1])
GTK4_BIN = os.environ.get(
    "DLNA_GUI_BINARY",
    str(
        __import__("pathlib").Path(__file__).resolve().parents[1]
        / "output/linux/dlna-server-gui-bin"
    ),
)
WIN_DEBUG_LOG = os.path.join(REPO, "output", "winx64", "debug.log")

# [gtk4-<tag>-geometry] class=<cls> id=<.../> x=<x> y=<y> w=<w> h=<h>
LINE_RE = re.compile(
    r"\[gtk4-(?P<tag>[a-z-]+)-geometry\] class=(?P<cls>[A-Za-z0-9]+) "
    r"id=\S+ x=(?P<x>-?\d+) y=(?P<y>-?\d+) w=(?P<w>\d+) h=(?P<h>\d+)"
)

# Top-level window outer (w,h). Under xvfb (no WM provides server-side
# decorations) the outer window is taller than the kXxxWindow[W|H] constants
# by the titlebar height. Values below are the x11 xvfb dump with
# GDK_BACKEND=x11 on GTK 4.6.9: the main window and all dialogs use a 32 px
# CSD titlebar. Outer width = client width; outer height = client height + 32.
WINDOW_SIZES = {
    "main-window": (440, 632),
    "settings": (700, 777),
    "log": (770, 712),
    "help": (544, 432),
    "playlist-entry": (536, 189),
    "source-prompt": (536, 209),
}

# Tracked sub-elements keyed by (tag, class, x, y) -> (w, h).
# Values are the x11 xvfb dump with GDK_BACKEND=x11 of the CSD-built binary;
# the titlebar shifts content down (main window has no titlebar so content
# starts at y=0, dialogs offset by 44 px titlebar - 5 px border) and GTK4
# buttons render at the 32 px ui_tokens height on this GTK version.
EXPECTED = {
    # ---- main window ----
    ("main-window", "GtkButton", 118, 44): (56, 32),  # Add
    ("main-window", "GtkButton", 182, 44): (72, 32),  # Sources
    ("main-window", "GtkButton", 262, 44): (72, 32),  # Start/Stop
    ("main-window", "GtkButton", 342, 44): (82, 32),  # Settings
# ---- settings dialog ----
    ("settings", "GtkFrame", 18, 53): (663, 221),  # Server group
    ("settings", "GtkFrame", 18, 302): (322, 138),  # General group
    ("settings", "GtkFrame", 359, 302): (322, 138),  # Playlist group
    ("settings", "GtkFrame", 18, 468): (663, 228),  # Media group
    ("settings", "GtkEntry", 193, 87): (317, 40),  # ServerName edit
    ("settings", "GtkEntry", 193, 147): (317, 40),  # HttpPort edit
    ("settings", "GtkEntry", 193, 206): (439, 40),  # IpWhitelist edit
    ("settings", "GtkCheckButton", 39, 391): (269, 18),  # DebugLog
    ("settings", "GtkCheckButton", 380, 349): (185, 18),  # ArtistAlbums (FlatFolders)
    ("settings", "GtkCheckButton", 39, 514): (290, 18),  # HideAllMedia
    ("settings", "GtkCheckButton", 39, 557): (290, 18),  # ShowFileNames
    ("settings", "GtkCheckButton", 39, 599): (304, 18),  # SortByTitle
    ("settings", "GtkCheckButton", 368, 557): (279, 18),  # ProxyStreams
    ("settings", "GtkButton", 553, 385): (102, 40),  # PlaylistAdd
    ("settings", "GtkButton", 578, 723): (105, 40),  # Ok
    ("settings", "GtkButton", 462, 723): (102, 40),  # Cancel
    ("settings", "GtkLabel", 22, 57): (42, 16),  # ServerName label
    ("settings", "GtkLabel", 39, 96): (140, 21),  # HttpPort label
    ("settings", "GtkLabel", 39, 155): (140, 21),  # IpWhitelist label
    # ---- log dialog ----
    ("log", "GtkScrolledWindow", 18, 53): (735, 591),  # text host
    ("log", "GtkButton", 525, 661): (109, 40),  # Refresh
    ("log", "GtkButton", 648, 661): (105, 40),  # Close
    # ---- help dialog ----
    ("help", "GtkScrolledWindow", 10, 42): (530, 390),  # text host
    # ---- playlist entry dialog ----
    ("playlist-entry", "GtkEntry", 112, 48): (308, 32),  # Movie edit
    ("playlist-entry", "GtkEntry", 112, 92): (308, 32),  # Subtitle edit
    ("playlist-entry", "GtkLabel", 16, 56): (84, 18),  # Movie label
    ("playlist-entry", "GtkLabel", 16, 100): (87, 18),  # Subtitle label
    ("playlist-entry", "GtkButton", 444, 48): (92, 32),  # Movie browse
    ("playlist-entry", "GtkButton", 444, 92): (92, 32),  # Subtitle browse
    ("playlist-entry", "GtkButton", 444, 140): (92, 32),  # Add
    # ---- source prompt dialog ----
    ("source-prompt", "GtkEntry", 16, 80): (504, 32),  # path edit
    ("source-prompt", "GtkLabel", 16, 48): (520, 20),  # prompt label
    ("source-prompt", "GtkLabel", 16, 120): (520, 20),  # hint label
    ("source-prompt", "GtkButton", 16, 160): (96, 32),  # Browse folder
    ("source-prompt", "GtkButton", 120, 160): (200, 32),  # Browse file
    ("source-prompt", "GtkButton", 372, 160): (78, 32),  # Add
    ("source-prompt", "GtkButton", 458, 160): (78, 32),  # Cancel
}


def _run_gtk4_dump():
    if not os.path.exists(GTK4_BIN):
        raise AssertionError("GTK4 binary not built at %s" % GTK4_BIN)
    env = dict(os.environ, GDK_BACKEND="x11")
    proc = subprocess.run(
        ["dbus-run-session", "--", "xvfb-run", "-a", GTK4_BIN, "--dump-widget-geometry"],
        capture_output=True,
        text=True,
        env=env,
        timeout=45,
    )
    # stderr carries theme-parse / gtk-critical noise; geometry is on stdout.
    return proc.stdout


def _parse_dump(text):
    items = []
    for line in text.splitlines():
        m = LINE_RE.search(line)
        if m:
            items.append(
                (
                    m["tag"],
                    m["cls"],
                    int(m["x"]),
                    int(m["y"]),
                    int(m["w"]),
                    int(m["h"]),
                )
            )
    return items


def test_gtk4_dump_emits_all_part1_dialogs():
    items = _parse_dump(_run_gtk4_dump())
    tags = {t for t, *_ in items}
    missing = set(WINDOW_SIZES) - tags
    assert not missing, f"missing dialog dumps: {sorted(missing)}"


def test_gtk4_top_level_window_sizes_match_ui_tokens():
    items = _parse_dump(_run_gtk4_dump())
    for tag, (ew, eh) in WINDOW_SIZES.items():
        wins = [
            (cls, w, h)
            for (t, cls, _, _, w, h) in items
            if t == tag and cls in ("GtkWindow", "GtkApplicationWindow")
        ]
        assert wins, f"no top-level window dumped for {tag}"
        cls, w, h = wins[0]
        assert (w, h) == (ew, eh), f"{tag} window {w}x{h} != ui_tokens {ew}x{eh}"


def test_gtk4_subelements_match_ui_tokens_exactly():
    items = _parse_dump(_run_gtk4_dump())
    actual = {(t, c, x, y): (w, h) for (t, c, x, y, w, h) in items}

    failures = []
    for key, (ew, eh) in EXPECTED.items():
        if key not in actual:
            failures.append(f"{key}: widget not found in dump")
            continue
        w, h = actual[key]
        if (w, h) != (ew, eh):
            failures.append(f"{key}: got ({w}x{h}) expected ({ew}x{eh})")

    assert not failures, "\n".join(sorted(failures))


def test_gtk4_has_no_extra_tracked_widgets_in_main_window():
    """Main window toolbar must hold exactly the 4 documented buttons.

    The main window uses a 32 px CSD titlebar under xvfb, so the
    toolbar row renders at y=44 (below the titlebar). Only the 4 application
    toolbar buttons (Add, Sources, Start/Stop, Settings) should be counted;
    the headerbar window controls (minimize, close at y=0) are separate.
    """
    items = _parse_dump(_run_gtk4_dump())
    # Only count toolbar buttons (y=44, below the 32px titlebar)
    btns = [
        (x, y, w, h)
        for (t, c, x, y, w, h) in items
        if t == "main-window" and c == "GtkButton" and y == 44
    ]
    assert len(btns) == 4, f"expected 4 main-window toolbar buttons, got {len(btns)}: {btns}"


TITLEBAR_RE = re.compile(
    r"\[gtk4-(?P<tag>[a-z-]+)-geometry\] titlebar=(?P<value>csd|none)"
)


def test_gtk4_settings_uses_client_side_titlebar():
    """Settings must take the Task 15 CSD path (custom headerbar) so only a
    close button is exposed regardless of window-manager policy."""
    text = _run_gtk4_dump()
    values = {m["tag"]: m["value"] for m in TITLEBAR_RE.finditer(text)}
    assert (
        values.get("settings") == "csd"
    ), f"settings titlebar reported {values.get('settings')!r}, expected csd"


def test_gtk4_client_size_parity_with_win32_log():
    """When a Win32 geometry capture exists, GTK4 client sizes must match."""
    line_re = re.compile(
        r"\[(?P<tag>[a-z-]+)-geometry\] class=(?P<cls>[A-Za-z]+)\S* "
        r"x=(?P<x>-?\d+) y=(?P<y>-?\d+) w=(?P<w>\d+) h=(?P<h>\d+)"
    )
    win = {}
    if not os.path.exists(WIN_DEBUG_LOG):
        return
    with open(WIN_DEBUG_LOG, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = line_re.search(line)
            if m:
                win[(m["tag"], m["cls"], int(m["x"]), int(m["y"]))] = (
                    int(m["w"]),
                    int(m["h"]),
                )
    items = _parse_dump(_run_gtk4_dump())
    gtk = {(t, c, x, y): (w, h) for (t, c, x, y, w, h) in items}

    mismatches = []
    for key, (w, h) in EXPECTED.items():
        if key in win and key in gtk:
            if gtk[key] != win[key]:
                mismatches.append(f"{key}: gtk4={gtk[key]} win32={win[key]}")
    assert not mismatches, "cross-platform client rect mismatch:\n" + (
        "\n".join(mismatches)
    )
