import os
import shutil
import subprocess
import time
from pathlib import Path

import pytest

pytestmark = pytest.mark.posix_only

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
