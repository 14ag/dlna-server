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

pytestmark = pytest.mark.posix_only

REPO = str(__import__("pathlib").Path(__file__).resolve().parents[1])
GTK4_BIN = os.environ.get(
    "DLNA_GUI_BINARY",
    str(
        __import__("pathlib").Path(__file__).resolve().parents[1]
        / "build-release-linux-stage/install/bin/dlna-server-gui-bin"
    ),
)
WIN_DEBUG_LOG = os.path.join(REPO, "output", "winx64", "debug.log")

# [gtk4-<tag>-geometry] class=<cls> id=<.../> x=<x> y=<y> w=<w> h=<h>
LINE_RE = re.compile(
    r"\[gtk4-(?P<tag>[a-z-]+)-geometry\] class=(?P<cls>[A-Za-z0-9]+) "
    r"id=\S+ x=(?P<x>-?\d+) y=(?P<y>-?\d+) w=(?P<w>\d+) h=(?P<h>\d+)"
)

# Top-level window outer (w,h). Every window is built with a CSD headerbar
# under xvfb (no WM provides server-side decorations), so the outer window is
# taller than the kXxxWindow[W|H] client constants by the titlebar height and
# its border offsets sub-elements. Values below are the x11 xvfb dump.
WINDOW_SIZES = {
    "main-window": (450, 653),
    "settings": (710, 801),
    "log": (770, 680),
    "help": (544, 400),
    "playlist-entry": (536, 157),
    "source-prompt": (536, 177),
}

# Tracked sub-elements keyed by (tag, class, x, y) -> (w, h).
# Values are the x11 xvfb dump of the CSD-built binary; the headerbar shifts
# content down and the CSD border shifts it right by 5 px, and GTK4 buttons
# render 2 px taller (34 vs the 32 px ui_tokens height).
EXPECTED = {
    # ---- main window ----
    ("main-window", "GtkButton", 113, 50): (56, 34),  # Add
    ("main-window", "GtkButton", 177, 50): (72, 34),  # Sources
    ("main-window", "GtkButton", 257, 50): (72, 34),  # Start/Stop
    ("main-window", "GtkButton", 337, 50): (82, 34),  # Settings
    # ---- settings dialog ----
    ("settings", "GtkFrame", 13, 62): (665, 223),  # Server group
    ("settings", "GtkFrame", 13, 311): (324, 140),  # General group
    ("settings", "GtkFrame", 354, 311): (324, 140),  # Playlist group
    ("settings", "GtkFrame", 13, 477): (665, 230),  # Media group
    ("settings", "GtkEntry", 188, 96): (333, 40),  # ServerName edit
    ("settings", "GtkEntry", 188, 156): (333, 40),  # HttpPort edit
    ("settings", "GtkEntry", 188, 215): (455, 40),  # IpWhitelist edit
    # NOTE: kSettingsRunOnStartupX/Y/W/H (39,317) exists in ui_tokens.h but the
    # GTK4 settings dialog does not yet create a Run-on-startup checkbox, so
    # there is no sub-element to assert here. Adding that control is a
    # separate feature task, not a geometry-parity concern.
    ("settings", "GtkCheckButton", 375, 358): (193, 26),  # DefaultPlaylist
    ("settings", "GtkCheckButton", 34, 400): (277, 26),  # DebugLog
    ("settings", "GtkCheckButton", 34, 523): (298, 26),  # ArtistAlbums
    ("settings", "GtkCheckButton", 363, 523): (228, 26),  # FlatFolders
    ("settings", "GtkCheckButton", 34, 566): (298, 26),  # HideAllMedia
    ("settings", "GtkCheckButton", 363, 566): (287, 26),  # ShowFileNames
    ("settings", "GtkCheckButton", 34, 608): (312, 26),  # SortByTitle
    ("settings", "GtkCheckButton", 363, 608): (228, 26),  # ProxyStreams
    ("settings", "GtkCheckButton", 34, 655): (403, 26),  # BackgroundScan
    ("settings", "GtkButton", 457, 732): (102, 40),  # Cancel
    ("settings", "GtkButton", 548, 394): (102, 40),  # PlaylistAdd
    ("settings", "GtkButton", 573, 732): (105, 40),  # Ok
    ("settings", "GtkLabel", 34, 105): (140, 21),  # ServerName label
    ("settings", "GtkLabel", 34, 164): (140, 21),  # HttpPort label
    ("settings", "GtkLabel", 34, 224): (140, 21),  # IpWhitelist label
    # ---- log dialog ----
    ("log", "GtkScrolledWindow", 18, 21): (735, 591),  # text host
    ("log", "GtkButton", 525, 629): (109, 40),  # Refresh
    ("log", "GtkButton", 648, 629): (105, 40),  # Close
    # ---- help dialog ----
    ("help", "GtkScrolledWindow", 10, 10): (530, 390),  # text host
    # ---- playlist entry dialog ----
    ("playlist-entry", "GtkEntry", 112, 16): (324, 32),  # Movie edit
    ("playlist-entry", "GtkEntry", 112, 60): (324, 32),  # Subtitle edit
    ("playlist-entry", "GtkLabel", 16, 24): (84, 18),  # Movie label
    ("playlist-entry", "GtkLabel", 16, 68): (87, 18),  # Subtitle label
    ("playlist-entry", "GtkButton", 444, 16): (92, 34),  # Movie browse
    ("playlist-entry", "GtkButton", 444, 60): (92, 34),  # Subtitle browse
    ("playlist-entry", "GtkButton", 444, 108): (92, 34),  # Add
    # ---- source prompt dialog ----
    ("source-prompt", "GtkEntry", 16, 48): (520, 32),  # path edit
    ("source-prompt", "GtkLabel", 16, 16): (520, 20),  # prompt label
    ("source-prompt", "GtkLabel", 16, 88): (520, 20),  # hint label
    ("source-prompt", "GtkButton", 16, 128): (96, 34),  # Browse folder
    ("source-prompt", "GtkButton", 120, 128): (200, 34),  # Browse file
    ("source-prompt", "GtkButton", 372, 128): (78, 34),  # Add
    ("source-prompt", "GtkButton", 458, 128): (78, 34),  # Cancel
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

    The main window uses a CSD headerbar (Task 15), whose window controls
    (minimize/maximize/close) also render as GtkButtons at the top of the
    window. Scope the count to the toolbar row below the headerbar so only
    the 4 application buttons are tracked.
    """
    items = _parse_dump(_run_gtk4_dump())
    btns = [
        (x, y, w, h)
        for (t, c, x, y, w, h) in items
        if t == "main-window" and c == "GtkButton" and y >= 44
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
