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

import pytest

pytestmark = [pytest.mark.posix_only, pytest.mark.needs_xvfb]

REPO = str(__import__("pathlib").Path(__file__).resolve().parents[1])
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
    "main-window": (426, 595),
    "settings": (702, 801),
    "log": (774, 716),
    "help": (532, 404),
    "playlist-entry": (540, 193),
    "source-prompt": (540, 213),
}

# Tracked sub-elements keyed by (tag, class, x, y) -> (w, h).
# Values are the x11 xvfb dump with GDK_BACKEND=x11 of the CSD-built binary;
# the titlebar shifts content down (main window has no titlebar so content
# starts at y=0, dialogs offset by 44 px titlebar - 5 px border) and GTK4
# buttons render at the 32 px ui_tokens height on this GTK version.
EXPECTED = {
    # ---- main window ----
    ("main-window", "GtkButton", 103, 45): (52, 28),  # Add
    ("main-window", "GtkButton", 167, 45): (68, 28),  # Sources
    ("main-window", "GtkButton", 248, 45): (68, 28),  # Start/Stop
    ("main-window", "GtkButton", 327, 45): (79, 28),  # Settings
# ---- settings dialog ----
    ("settings", "GtkFrame", 19, 82): (658, 211),  # Server group
    ("settings", "GtkFrame", 23, 335): (306, 122),  # General group
    ("settings", "GtkFrame", 347, 335): (328, 122),  # Playlist group
    ("settings", "GtkFrame", 23, 499): (658, 214),  # Media group
    ("settings", "GtkEntry", 194, 108): (314, 39),  # ServerName edit
    ("settings", "GtkEntry", 194, 166): (312, 39),  # HttpPort edit
    ("settings", "GtkEntry", 193, 225): (437, 38),  # IpWhitelist edit
    ("settings", "GtkCheckButton", 37, 415): (153, 24),  # DebugLog
    ("settings", "GtkCheckButton", 382, 374): (106, 24),  # DefaultPlaylist
    ("settings", "GtkCheckButton", 35, 582): (193, 24),  # HideAllMedia
    ("settings", "GtkCheckButton", 377, 586): (198, 24),  # ShowFileNames
    ("settings", "GtkCheckButton", 36, 624): (194, 24),  # SortByTitle
    ("settings", "GtkCheckButton", 377, 627): (101, 24),  # ProxyStreams
    ("settings", "GtkButton", 555, 408): (65, 26),  # PlaylistAdd
    ("settings", "GtkButton", 579, 746): (70, 24),  # Ok
    ("settings", "GtkButton", 464, 746): (64, 24),  # Cancel
    ("settings", "GtkLabel", 28, 75): (32, 15),  # ServerName label
    ("settings", "GtkLabel", 40, 118): (76, 15),  # HttpPort label
    ("settings", "GtkLabel", 40, 177): (76, 15),  # IpWhitelist label
    # ---- log dialog ----
    ("log", "GtkScrolledWindow", 18, 54): (735, 590),  # text host
    ("log", "GtkButton", 527, 663): (73, 28),  # Refresh
    ("log", "GtkButton", 651, 663): (67, 28),  # Close
    # ---- help dialog ----
    ("help", "GtkScrolledWindow", -1, 31): (530, 370),  # text host
    # ---- playlist entry dialog ----
    ("playlist-entry", "GtkEntry", 112, 49): (305, 32),  # Movie edit
    ("playlist-entry", "GtkEntry", 94, 93): (307, 32),  # Subtitle edit
    ("playlist-entry", "GtkLabel", 16, 55): (85, 16),  # Movie label
    ("playlist-entry", "GtkLabel", 16, 93): (85, 16),  # Subtitle label
    ("playlist-entry", "GtkButton", 446, 49): (54, 24),  # Movie browse
    ("playlist-entry", "GtkButton", 444, 93): (57, 24),  # Subtitle browse
    ("playlist-entry", "GtkButton", 444, 142): (56, 24),  # Add
    # ---- source prompt dialog ----
    ("source-prompt", "GtkEntry", 17, 79): (496, 32),  # path edit
    ("source-prompt", "GtkLabel", 17, 49): (273, 16),  # prompt label
    ("source-prompt", "GtkLabel", 17, 123): (254, 15),  # hint label
    ("source-prompt", "GtkButton", 17, 160): (59, 24),  # Browse folder
    ("source-prompt", "GtkButton", 121, 161): (164, 24),  # Browse file
    ("source-prompt", "GtkButton", 372, 160): (42, 24),  # Add
    ("source-prompt", "GtkButton", 459, 162): (41, 24),  # Cancel
}


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


def test_gtk4_top_level_window_sizes_match_ui_tokens(gtk4_geometry_dump):
    items = _parse_dump(gtk4_geometry_dump)
    for tag, (ew, eh) in WINDOW_SIZES.items():
        wins = [
            (cls, w, h)
            for (t, cls, _, _, w, h) in items
            if t == tag and cls in ("GtkWindow", "GtkApplicationWindow")
        ]
        assert wins, f"no top-level window dumped for {tag}"
        cls, w, h = wins[0]
        assert (w, h) == (ew, eh), f"{tag} window {w}x{h} != ui_tokens {ew}x{eh}"


def test_gtk4_subelements_match_ui_tokens_exactly(gtk4_geometry_dump):
    items = _parse_dump(gtk4_geometry_dump)
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


def test_gtk4_has_no_extra_tracked_widgets_in_main_window(gtk4_geometry_dump):
    """Main window toolbar holds exactly the 4 documented buttons (y=44)."""
    items = _parse_dump(gtk4_geometry_dump)
    btns = [
        (x, y, w, h)
        for (t, c, x, y, w, h) in items
        if t == "main-window" and c == "GtkButton" and y == 45
    ]
    assert len(btns) == 4, f"expected 4 main-window toolbar buttons, got {len(btns)}: {btns}"


TITLEBAR_RE = re.compile(
    r"\[gtk4-(?P<tag>[a-z-]+)-geometry\] titlebar=(?P<value>csd|none)"
)


@pytest.mark.parametrize("tag", ["settings", "main-window"])
def test_gtk4_uses_client_side_titlebar(gtk4_geometry_dump, tag):
    values = {m["tag"]: m["value"] for m in TITLEBAR_RE.finditer(gtk4_geometry_dump)}
    assert values.get(tag) == "csd", f"{tag} titlebar reported {values.get(tag)!r}, expected csd"


def test_gtk4_client_size_parity_with_win32_log(gtk4_geometry_dump):
    """When a Win32 geometry capture exists, GTK4 client sizes must match."""
    line_re = re.compile(
        r"\[(?P<tag>[a-z-]+)-geometry\] class=(?P<cls>[A-Za-z]+)\S* "
        r"x=(?P<x>-?\d+) y=(?P<y>-?\d+) w=(?P<w>\d+) h=(?P<h>\d+)"
    )
    if not os.path.exists(WIN_DEBUG_LOG):
        return
    win = {}
    with open(WIN_DEBUG_LOG, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = line_re.search(line)
            if m:
                win[(m["tag"], m["cls"], int(m["x"]), int(m["y"]))] = (int(m["w"]), int(m["h"]))
    gtk = {(t, c, x, y): (w, h) for (t, c, x, y, w, h) in _parse_dump(gtk4_geometry_dump)}
    mismatches = [
        f"{key}: gtk4={gtk[key]} win32={win[key]}"
        for key in EXPECTED
        if key in win and key in gtk and gtk[key] != win[key]
    ]
    assert not mismatches, "cross-platform client rect mismatch:\n" + "\n".join(mismatches)
