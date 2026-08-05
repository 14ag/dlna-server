import os
import shutil
import subprocess
import time
from pathlib import Path

import pytest

pytestmark = [pytest.mark.posix_only, pytest.mark.gui_only]

REPO_ROOT = Path(__file__).resolve().parent.parent


def _find_gui_binary():
    env_path = os.environ.get("DLNA_GUI_BINARY")
    if env_path and Path(env_path).is_file():
        return Path(env_path)
    for candidate in (
        REPO_ROOT / "build" / "dlna-server-gui-bin",
        REPO_ROOT / "build-linux" / "dlna-server-gui-bin",
    ):
        if candidate.is_file():
            return candidate
    return None


GUI_BINARY = _find_gui_binary()
XVFB_RUN = shutil.which("xvfb-run")
XDOTOOL = shutil.which("xdotool")

_SKIP_REASON = (
    "dlna-server-gui-bin not found; set DLNA_GUI_BINARY to the built binary path"
)


def _isolated_env(tmp_path):
    env = dict(os.environ)
    (tmp_path / "config").mkdir(parents=True, exist_ok=True)
    (tmp_path / "runtime").mkdir(parents=True, exist_ok=True)
    env["HOME"] = str(tmp_path)
    env["XDG_CONFIG_HOME"] = str(tmp_path / "config")
    env["XDG_RUNTIME_DIR"] = str(tmp_path / "runtime")
    return env


def _socket_path(env):
    return Path(env["XDG_RUNTIME_DIR"]) / "dlna-server.sock"


def _wait_for(predicate, timeout_seconds):
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        if predicate():
            return True
        time.sleep(0.25)
    return False


def _window_exists(env):
    result = subprocess.run(
        [XDOTOOL, "search", "--name", "DLNA Server"],
        env=env,
        capture_output=True,
        text=True,
    )
    return result.returncode == 0 and result.stdout.strip() != ""


@pytest.mark.skipif(GUI_BINARY is None, reason=_SKIP_REASON)
@pytest.mark.skipif(XVFB_RUN is None, reason="xvfb-run not installed")
def test_log_dialog_reopens_after_close(tmp_path):
    """Task 6: after the Log dialog's Close button hides it, opening it again
    must re-show the existing window instead of hitting the stale non-null
    guard and doing nothing. The hook exits 0 only when the second
    ShowLogDialog() leaves g_logDialog visible."""
    env = _isolated_env(tmp_path)
    env["GDK_BACKEND"] = "x11"
    result = subprocess.run(
        [XVFB_RUN, "-a", str(GUI_BINARY), "--dump-log-dialog-reopen"],
        env=env, capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, (
        f"--dump-log-dialog-reopen failed with code {result.returncode}: "
        f"{result.stderr}"
    )


@pytest.mark.skipif(GUI_BINARY is None, reason=_SKIP_REASON)
@pytest.mark.skipif(XVFB_RUN is None, reason="xvfb-run not installed")
@pytest.mark.skipif(shutil.which("dbus-run-session") is None,
                    reason="dbus-run-session not installed")
def test_tray_registration_result_is_logged(tmp_path):
    """Task 7: the tray icon registration result must always be resolved and
    logged (registered or unavailable), never left silently unknown. Run
    under a session D-Bus with no StatusNotifierWatcher so the registration
    call deterministically fails and the unavailable path is exercised."""
    env = _isolated_env(tmp_path)
    config_dir = Path(env["XDG_CONFIG_HOME"]) / "dlna-server"
    config_dir.mkdir(parents=True, exist_ok=True)
    (config_dir / "config.ini").write_text(
        "[Settings]\nDebugLog=1\n", encoding="utf-8")

    proc = subprocess.Popen(
        ["dbus-run-session", "--", XVFB_RUN, "-a", str(GUI_BINARY)],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        deadline = time.time() + 15
        content = ""
        while time.time() < deadline:
            log = config_dir / "debug.log"
            if log.exists():
                content = log.read_text(encoding="utf-8", errors="replace")
                if ("Tray icon registered" in content or
                        "Tray icon unavailable" in content):
                    break
            time.sleep(0.25)
        assert ("Tray icon registered" in content or
                "Tray icon unavailable" in content), (
            f"debug.log never resolved tray registration; content={content!r}"
        )
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3)


@pytest.mark.skipif(GUI_BINARY is None, reason=_SKIP_REASON)
@pytest.mark.skipif(XVFB_RUN is None, reason="xvfb-run not installed")
@pytest.mark.skipif(shutil.which("dbus-run-session") is None,
                    reason="dbus-run-session not installed")
def test_no_tray_hint_is_logged_but_recovery_needs_no_tray(tmp_path):
    """Task 14: with no tray host present (StatusNotifierWatcher absent) the
    app logs a one-time, non-blocking hint that the window stays reachable by
    re-running the shortcut. This does not block startup and window recovery
    never waits on the tray (see OnSingleInstanceCommand)."""
    env = _isolated_env(tmp_path)
    config_dir = Path(env["XDG_CONFIG_HOME"]) / "dlna-server"
    config_dir.mkdir(parents=True, exist_ok=True)
    (config_dir / "config.ini").write_text(
        "[Settings]\nDebugLog=1\n", encoding="utf-8")

    proc = subprocess.Popen(
        ["dbus-run-session", "--", XVFB_RUN, "-a", str(GUI_BINARY)],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        deadline = time.time() + 15
        content = ""
        while time.time() < deadline:
            log = config_dir / "debug.log"
            if log.exists():
                content = log.read_text(encoding="utf-8", errors="replace")
                if "No system tray host detected" in content:
                    break
            time.sleep(0.25)
        assert "No system tray host detected" in content, (
            f"debug.log never logged the no-tray recovery hint; content={content!r}"
        )
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3)


@pytest.mark.skipif(GUI_BINARY is None, reason=_SKIP_REASON)
@pytest.mark.skipif(XVFB_RUN is None, reason="xvfb-run not installed")
@pytest.mark.skipif(XDOTOOL is None, reason="xdotool not installed")
def test_closing_window_before_start_does_not_abort(tmp_path):
    env = _isolated_env(tmp_path)
    sock_path = _socket_path(env)

    proc = subprocess.Popen(
        [XVFB_RUN, "-a", str(GUI_BINARY)],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        assert _wait_for(lambda: sock_path.exists(), 15), (
            "app did not finish startup ipc socket never appeared"
        )
        assert _wait_for(lambda: _window_exists(env), 15), (
            "main window never appeared"
        )

        subprocess.run(
            [XDOTOOL, "search", "--name", "DLNA Server", "windowclose"],
            env=env,
            capture_output=True,
            text=True,
        )

        stdout, stderr = proc.communicate(timeout=20)
    except subprocess.TimeoutExpired:
        proc.kill()
        stdout, stderr = proc.communicate()
        pytest.fail("gui process did not exit after a window close request")

    assert proc.returncode == 0, (
        f"gui process exited with code {proc.returncode} stderr was {stderr}"
    )
    assert "terminate called" not in stderr
    assert "Aborted" not in stderr
    assert not sock_path.exists(), (
        "single instance socket was not cleaned up release lock did not run"
    )


@pytest.mark.skipif(GUI_BINARY is None, reason=_SKIP_REASON)
@pytest.mark.skipif(XVFB_RUN is None, reason="xvfb-run not installed")
@pytest.mark.skipif(XDOTOOL is None, reason="xdotool not installed")
def test_second_launch_restores_minimized_window(tmp_path):
    env = _isolated_env(tmp_path)
    sock_path = _socket_path(env)

    first = subprocess.Popen([XVFB_RUN, "-a", str(GUI_BINARY)], env=env)
    try:
        assert _wait_for(lambda: sock_path.exists(), 15)
        assert _wait_for(lambda: _window_exists(env), 15)

        for attempt in range(5):
            subprocess.run(
                [XDOTOOL, "search", "--name", "DLNA Server", "windowminimize"],
                env=env,
                capture_output=True,
            )

            second = subprocess.run(
                [XVFB_RUN, "-a", str(GUI_BINARY)],
                env=env,
                timeout=15,
            )
            assert second.returncode == 0, f"second launch failed on attempt {attempt}"

            def is_restored():
                result = subprocess.run(
                    [XDOTOOL, "getactivewindow", "getwindowname"],
                    env=env,
                    capture_output=True,
                    text=True,
                )
                return "DLNA Server" in result.stdout

            assert _wait_for(is_restored, 10), (
                f"window was not restored on attempt {attempt}"
            )
    finally:
        first.terminate()
        try:
            first.wait(timeout=10)
        except subprocess.TimeoutExpired:
            first.kill()


@pytest.mark.skipif(GUI_BINARY is None, reason=_SKIP_REASON)
@pytest.mark.skipif(XVFB_RUN is None, reason="xvfb-run not installed")
def test_message_box_parents_to_active_dialog(tmp_path):
    """Task 16: an asynchronously-triggered failure message box must parent
    to whichever secondary dialog is currently visible (Settings here) instead
    of always to the main window. The hook prints the transient parent tag for
    both the nothing-open case (expect main) and the Settings-open case
    (expect settings), then exits 0."""
    env = _isolated_env(tmp_path)
    env["GDK_BACKEND"] = "x11"
    result = subprocess.run(
        [XVFB_RUN, "-a", str(GUI_BINARY), "--dump-msgbox-parent"],
        env=env, capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, (
        f"--dump-msgbox-parent failed with code {result.returncode}: "
        f"{result.stderr}"
    )
    lines = [ln for ln in result.stdout.splitlines() if "msgbox-parent" in ln]
    parents = {ln.split("parent=", 1)[1] for ln in lines}
    assert parents == {"main", "settings"}, (
        f"expected transient parents {{main, settings}} got {parents!r}; "
        f"stdout={result.stdout!r}"
    )
