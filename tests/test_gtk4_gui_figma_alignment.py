"""Blackbox Figma-alignment tests for the GTK4 GUI.

Runs the gtk4 binary under xvfb with the hidden --print-*/--dump-* hooks and
asserts the behaviors added by the POSIX GTK4 Figma-alignment workflow:
delete-focus gating, main-toolbar widget geometry, and the default-playlist
Add button sensitivity wiring.
"""

import os
import subprocess

import pytest

pytestmark = pytest.mark.posix_only

REPO_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
GUI_BINARY = os.environ.get(
    "DLNA_GUI_BINARY",
    os.path.join(REPO_ROOT, "output", "linux", "dlna-server-gui-bin"),
)


def _run_dump(flag, extra_args=None, timeout=45):
    args = ["dbus-run-session", "--", "xvfb-run", "-a", GUI_BINARY, flag]
    if extra_args:
        args.extend(extra_args)
    env = dict(os.environ, GDK_BACKEND="x11")
    result = subprocess.run(args, capture_output=True, text=True, env=env, timeout=timeout)
    return result


@pytest.mark.needs_xvfb
def test_delete_focus_gating_sequence():
    if not os.path.exists(GUI_BINARY):
        pytest.fail("GTK4 GUI binary not built at %s" % GUI_BINARY)
    result = _run_dump("--print-delete-focus-gating")
    assert result.returncode == 0
    lines = [line for line in result.stdout.splitlines() if line.strip()]
    assert "after-select=true" in lines
    assert "after-focus-to-delete=true" in lines
    assert "after-focus-elsewhere=false" in lines


@pytest.mark.needs_xvfb
def test_widget_geometry_matches_figma():
    if not os.path.exists(GUI_BINARY):
        pytest.fail("GTK4 GUI binary not built at %s" % GUI_BINARY)
    result = _run_dump("--dump-widget-geometry")
    assert result.returncode == 0
    output = result.stdout

    def find_class(tag, klass):
        for line in output.splitlines():
            if f"[gtk4-{tag}-geometry]" in line and f"class={klass}" in line:
                return line
        return None

    main_toolbar = find_class("main-window", "GtkBox")
    assert main_toolbar is not None

    # spot-check the four main toolbar button size requests against Phase 1 of
    # dlna-server-posix-gui-figma-alignment-workflow-20-08-26.md
    # The rendered w= in the dump is size request plus the GTK4 border so it is
    # 1px larger (57/72/72/83 under the default theme); the sr= line carries the
    # exact size request applied by the code, which is the Figma contract.
    for expected_sr in ("sr=55x31", "sr=71x31", "sr=82x31"):
        assert expected_sr in output, f"expected a button size request of {expected_sr} in geometry dump"


@pytest.mark.needs_xvfb
def test_playlist_add_disabled_until_input():
    if not os.path.exists(GUI_BINARY):
        pytest.fail("GTK4 GUI binary not built at %s" % GUI_BINARY)
    result = _run_dump("--print-playlist-add-sensitivity")
    assert result.returncode == 0
    assert "initial=false" in result.stdout
    assert "after-movie-text=true" in result.stdout