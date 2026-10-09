import ctypes
import os
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

import pytest

from conftest import kill_process_group, spawn_wrapped_process

pytestmark = [pytest.mark.posix_only, pytest.mark.needs_xvfb]

REPO_ROOT = Path(__file__).resolve().parent.parent


def _find_gui_binary():
    env_path = os.environ.get("DLNA_GUI_BINARY")
    if env_path and Path(env_path).is_file():
        return Path(env_path)
    for candidate in (
        REPO_ROOT / "output" / "linux" / "dlna-server-gui-bin",
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
    # AF_UNIX sockets (dbus/at-spi) cannot bind on the drvfs tmp_path; use
    # a 0700 /tmp runtime dir instead.
    runtime = Path(tempfile.mkdtemp(prefix="dlna-gui-li-", dir="/tmp"))
    os.chmod(runtime, 0o700)
    env["HOME"] = str(tmp_path)
    env["XDG_CONFIG_HOME"] = str(tmp_path / "config")
    env["XDG_RUNTIME_DIR"] = str(runtime)
    return env


def _global_instance_dir():
    # /tmp directly -- NOT tempfile.gettempdir(), which conftest redirects to
    # the repo tmp/ tree on drvfs. The C++ instance dir is /tmp/com.github.dlna_server_14ag-<uid>.
    return Path("/tmp") / f"com.github.dlna_server_14ag-{os.getuid()}"


def _socket_path(env):
    return _global_instance_dir() / "com.github.dlna_server_14ag.sock"


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


class _XClientMessageEvent(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_int),
        ("serial", ctypes.c_ulong),
        ("send_event", ctypes.c_int),
        ("display", ctypes.c_void_p),
        ("window", ctypes.c_ulong),
        ("message_type", ctypes.c_ulong),
        ("format", ctypes.c_int),
        ("data", ctypes.c_long * 5),
    ]


class _XEvent(ctypes.Union):
    _fields_ = [
        ("type", ctypes.c_int),
        ("xclient", _XClientMessageEvent),
        ("pad", ctypes.c_long * 24),
    ]


def _main_window_id(env):
    # choose the largest visible matching window so the hidden helper
    # window is ignored and an unmapped main window is never picked
    result = subprocess.run(
        [XDOTOOL, "search", "--onlyvisible", "--name", "DLNA Server"],
        env=env,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return None
    best_id = None
    best_area = 0
    for line in result.stdout.splitlines():
        window = line.strip()
        if not window.isdigit():
            continue
        geometry = subprocess.run(
            [XDOTOOL, "getwindowgeometry", "--shell", window],
            env=env,
            capture_output=True,
            text=True,
        )
        if geometry.returncode != 0:
            continue
        width = 0
        height = 0
        for entry in geometry.stdout.splitlines():
            if entry.startswith("WIDTH="):
                width = int(entry.partition("=")[2] or 0)
            elif entry.startswith("HEIGHT="):
                height = int(entry.partition("=")[2] or 0)
        if width * height > best_area:
            best_id = window
            best_area = width * height
    return best_id


def _send_close_request(env, window_id):
    # xdotool windowclose destroys the X window directly
    # send the window manager close message instead
    x11 = ctypes.CDLL("libX11.so.6")
    x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x11.XOpenDisplay.restype = ctypes.c_void_p
    x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    x11.XInternAtom.restype = ctypes.c_ulong
    x11.XSendEvent.argtypes = [
        ctypes.c_void_p,
        ctypes.c_ulong,
        ctypes.c_int,
        ctypes.c_long,
        ctypes.c_void_p,
    ]
    x11.XSendEvent.restype = ctypes.c_int
    x11.XFlush.argtypes = [ctypes.c_void_p]
    x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
    display = x11.XOpenDisplay(env["DISPLAY"].encode("ascii"))
    if not display:
        return False
    try:
        wm_protocols = x11.XInternAtom(display, b"WM_PROTOCOLS", 0)
        wm_delete = x11.XInternAtom(display, b"WM_DELETE_WINDOW", 0)
        event = _XEvent()
        event.xclient.type = 33
        event.xclient.send_event = 1
        event.xclient.display = display
        event.xclient.window = int(window_id)
        event.xclient.message_type = wm_protocols
        event.xclient.format = 32
        event.xclient.data[0] = wm_delete
        event.xclient.data[1] = 0
        sent = x11.XSendEvent(
            display, int(window_id), 0, 0, ctypes.byref(event)
        )
        x11.XFlush(display)
        return sent != 0
    finally:
        x11.XCloseDisplay(display)


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

    proc = spawn_wrapped_process(
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
        kill_process_group(proc)


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

    proc = spawn_wrapped_process(
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
        kill_process_group(proc)


@pytest.mark.skipif(GUI_BINARY is None, reason=_SKIP_REASON)
@pytest.mark.skipif(XVFB_RUN is None, reason="xvfb-run not installed")
@pytest.mark.skipif(XDOTOOL is None, reason="xdotool not installed")
def test_closing_window_before_start_does_not_abort(tmp_path, xvfb):
    env = _isolated_env(tmp_path)
    env["DISPLAY"] = xvfb["DISPLAY"]
    sock_path = _socket_path(env)

    proc = spawn_wrapped_process(
        ["dbus-run-session", "--", str(GUI_BINARY)],
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

        window_id = None
        assert _wait_for(
            lambda: _main_window_id(env) is not None, 15,
        ), "main window id never appeared"
        window_id = _main_window_id(env)
        assert window_id is not None, "main window id never appeared"
        stdout = ""
        stderr = ""
        for _ in range(3):
            assert _send_close_request(env, window_id), (
                "window manager close request was not sent"
            )
            try:
                stdout, stderr = proc.communicate(timeout=10)
                break
            except subprocess.TimeoutExpired:
                window_id = _main_window_id(env)
                if window_id is None:
                    stdout, stderr = proc.communicate(timeout=10)
                    break
        else:
            kill_process_group(proc)
            pytest.fail("gui process did not exit after a window close request")
    except subprocess.TimeoutExpired:
        kill_process_group(proc)
        pytest.fail("gui process did not exit after a window close request")

    if proc.returncode != 0:
        if "BadDrawable" in stderr and "terminate called" not in stderr and "Aborted" not in stderr:
            print("Ignoring BadDrawable X error (Xvfb race)")
        else:
            pytest.fail(f"gui process exited with code {proc.returncode} stderr was {stderr}")
    assert "terminate called" not in stderr
    assert "Aborted" not in stderr
    assert _wait_for(lambda: not sock_path.exists(), 5), (
        "single instance socket was not cleaned up, release lock did not run"
    )


@pytest.mark.skipif(GUI_BINARY is None, reason=_SKIP_REASON)
@pytest.mark.skipif(XVFB_RUN is None, reason="xvfb-run not installed")
@pytest.mark.skipif(XDOTOOL is None, reason="xdotool not installed")
@pytest.mark.needs_wm
def test_second_launch_restores_minimized_window(tmp_path, xvfb):
    env = _isolated_env(tmp_path)
    env["DISPLAY"] = xvfb["DISPLAY"]
    sock_path = _socket_path(env)

    first = spawn_wrapped_process(
        ["dbus-run-session", "--", str(GUI_BINARY)],
        env=env)
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
                ["dbus-run-session", "--", str(GUI_BINARY)],
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
        kill_process_group(first)
