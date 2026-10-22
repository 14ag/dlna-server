"""Blackbox test for the GTK4 toolbar bare access key mapping.

Runs the gtk4 binary under xvfb with the hidden --print-toolbar-access-keys
hook and asserts the toolbar button index each access key resolves to.
"""

import os
import re
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
def test_toolbar_access_key_indexes():
    if not os.path.exists(GUI_BINARY):
        pytest.fail("GTK4 GUI binary not built at %s" % GUI_BINARY)
    result = _run_dump("--print-toolbar-access-keys")
    assert result.returncode == 0
    # gtk prints renderer and atspi noise on stdout so keep probe lines only
    probes = [line for line in result.stdout.splitlines() if re.fullmatch(r"[adsex]=-?\d+", line)]
    assert probes == ["a=0", "d=1", "s=2", "e=3", "x=-1"]
