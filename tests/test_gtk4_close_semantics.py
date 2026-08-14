import pytest
import subprocess
import tempfile
import os
import time

pytestmark = pytest.mark.gui_only


def _isolated_env(tmp_path):
    env = dict(os.environ)
    (tmp_path / "config").mkdir(parents=True, exist_ok=True)
    # AF_UNIX sockets (dbus/at-spi) cannot bind on the drvfs tmp_path; use
    # a 0700 /tmp runtime dir instead.
    runtime = tempfile.mkdtemp(prefix="dlna-gui-si-", dir="/tmp")
    os.chmod(runtime, 0o700)
    env["HOME"] = str(tmp_path)
    env["XDG_CONFIG_HOME"] = str(tmp_path / "config")
    env["XDG_RUNTIME_DIR"] = runtime
    return env


def test_stopped_close_destroys_window(dlna_server_gui_binary, tmp_path):
    """Task 1: Stopped state Close must destroy window and exit process."""
    env = _isolated_env(tmp_path)
    env["GDK_BACKEND"] = "x11"
    # Use hidden flag to trigger RequestClose in Stopped state and assert process exits
    result = subprocess.run(
        ["dbus-run-session", "--", "xvfb-run", "-a", str(dlna_server_gui_binary),
         "--print-stopped-close-exit"],
        env=env, capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, (
        f"--print-stopped-close-exit failed with code {result.returncode}: "
        f"{result.stderr}"
    )
    # Process should exit cleanly (returncode 0), not hang
    # The test itself confirms this by the timeout mechanism


def test_log_dialog_reopens_after_close(dlna_server_gui_binary, tmp_path):
    """Task 6: after the Log dialog's Close button hides it, opening it again
    must re-show the existing window instead of hitting the stale non-null
    guard and doing nothing."""
    env = _isolated_env(tmp_path)
    env["GDK_BACKEND"] = "x11"
    result = subprocess.run(
        ["dbus-run-session", "--", "xvfb-run", "-a", str(dlna_server_gui_binary),
         "--dump-log-dialog-reopen"],
        env=env, capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, (
        f"--dump-log-dialog-reopen failed with code {result.returncode}: "
        f"{result.stderr}"
    )


def test_message_box_parents_to_active_dialog(dlna_server_gui_binary, tmp_path):
    """Task 16: an asynchronously-triggered failure message box must parent
    to whichever secondary dialog is currently visible (Settings here) instead
    of always to the main window. The hook prints the transient parent tag for
    both the nothing-open case (expect main) and the Settings-open case
    (expect settings), and the nested Settings-then-Log case (expect log),
    then exits 0."""
    env = _isolated_env(tmp_path)
    env["GDK_BACKEND"] = "x11"
    result = subprocess.run(
        ["dbus-run-session", "--", "xvfb-run", "-a", str(dlna_server_gui_binary),
         "--dump-msgbox-parent"],
        env=env, capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, (
        f"--dump-msgbox-parent failed with code {result.returncode}: "
        f"{result.stderr}"
    )
    lines = [ln for ln in result.stdout.splitlines() if "msgbox-parent" in ln]
    parents = {ln.split("parent=", 1)[1] for ln in lines}
    # Updated to expect three parents: main (nothing open), settings (Settings open), and log (Settings+Log nested)
    assert parents == {"main", "settings", "log"}, (
        f"expected transient parents {{main, settings, log}} got {parents!r}; "
        f"stdout={result.stdout!r}"
    )


def test_delete_disabled_after_focus_leaves_source_list(dlna_server_gui_binary, tmp_path):
    """Task 3: The Delete button must be disabled after focus leaves the source list,
    mirroring the Win32 NO-FOCUS rule where NO-FOCUS becomes true when the user selects
    items then clicks anywhere other than the Delete button."""
    env = _isolated_env(tmp_path)
    env["GDK_BACKEND"] = "x11"
    # Use hidden flag to test focus gating
    result = subprocess.run(
        ["dbus-run-session", "--", "xvfb-run", "-a", str(dlna_server_gui_binary),
         "--print-delete-focus-gating"],
        env=env, capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, (
        f"--print-delete-focus-gating failed with code {result.returncode}: "
        f"{result.stderr}"
    )
    # The test implementation is in the source code - this binary prints the sensitivity
    # values and exits with 0, which is checked by pytest
