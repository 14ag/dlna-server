import subprocess

import pytest

from tests.conftest import kill_process_group, spawn_wrapped_process

pytestmark = [pytest.mark.posix_only, pytest.mark.needs_xvfb]


def _run_gui_hook(gui_binary, xvfb, tmp_path, flag):
    home = tmp_path / "home"
    home.mkdir()
    runtime = tmp_path / "runtime"
    runtime.mkdir(mode=0o700)
    env = dict(
        xvfb,
        GDK_BACKEND="x11",
        HOME=str(home),
        XDG_CONFIG_HOME=str(home),
        XDG_RUNTIME_DIR=str(runtime),
    )
    proc = spawn_wrapped_process(
        ["dbus-run-session", "--", gui_binary, flag],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=env,
    )
    try:
        out, err = proc.communicate(timeout=20)
    except subprocess.TimeoutExpired:
        kill_process_group(proc)
        pytest.fail("gui hook %s did not exit within 20 seconds" % flag)
    return proc.returncode, out, err


def test_close_during_start_is_latched_then_process_exits(dlna_server_gui_binary, xvfb, tmp_path):
    code, out, err = _run_gui_hook(
        dlna_server_gui_binary, xvfb, tmp_path, "--print-close-while-starting")
    assert "pending-after-request=1" in out
    assert code == 0
    assert "terminate called" not in err
    assert "Aborted" not in err


def test_close_during_failed_start_does_not_wait_on_a_dialog(dlna_server_gui_binary, xvfb, tmp_path):
    code, out, err = _run_gui_hook(
        dlna_server_gui_binary, xvfb, tmp_path, "--print-close-while-starting-fails")
    assert "pending-after-request=1" in out
    assert code == 0
    assert "terminate called" not in err
