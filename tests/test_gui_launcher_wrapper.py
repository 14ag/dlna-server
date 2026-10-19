import subprocess
from pathlib import Path

import pytest

pytestmark = pytest.mark.posix_only

ROOT = Path(__file__).resolve().parents[1]
WRAPPER = ROOT / "packaging" / "linux" / "dlna-server-gui"
DESKTOP = ROOT / "packaging" / "linux" / "install_desktop.cmake.in"


def test_wrapper_is_valid_shell():
    assert subprocess.run(["sh", "-n", str(WRAPPER)]).returncode == 0


def test_wrapper_has_no_early_exec_or_private_bus():
    text = WRAPPER.read_text()
    assert "DLNA_SERVER_GTK_LAUNCHED" not in text
    assert "dbus-run-session" not in text


def test_wrapper_logs_gui_stderr():
    assert '2>>"$log_file"' in WRAPPER.read_text()


def test_desktop_entry_has_no_launch_env():
    assert "DLNA_SERVER_GTK_LAUNCHED" not in DESKTOP.read_text()


def test_wrapper_propagates_display_env_to_dbus():
    text = WRAPPER.read_text()
    # propagation is the fix for cold WSL Start Menu boot window not appearing
    assert "dbus-update-activation-environment" in text
    assert "WAYLAND_DISPLAY" in text
    assert "XDG_RUNTIME_DIR" in text
    assert "DISPLAY" in text


def test_wrapper_aligns_xdg_runtime_dir_with_wslg():
    text = WRAPPER.read_text()
    # when WAYLAND_DISPLAY is already set the wrapper must still align
    # XDG_RUNTIME_DIR to the wslg path so GTK and the compositor agree
    assert "elif" in text
    assert "/mnt/wslg/runtime-dir" in text
