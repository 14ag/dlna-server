"""Windows-side contract for WSLg shortcut launch and icon integration."""

import os
from pathlib import Path

import pytest


pytestmark = pytest.mark.windows_only
ROOT = Path(__file__).resolve().parents[1]


def test_wslg_shortcut_target_uses_desktop_metadata_for_launch_and_icon():
    """Raw WSLg shortcut target must route through the desktop entry.

    WSLg enumerates desktop entries for Windows Start/taskbar integration.
    This contract catches regressions without depending on an interactive
    Windows desktop during CI.
    """
    wrapper = (ROOT / "packaging" / "linux" / "dlna-server-gui").read_text()
    desktop = (ROOT / "packaging" / "linux" / "install_desktop.cmake.in").read_text()
    assert "exec gtk-launch dlna-server \"$@\"" in wrapper
    assert 'set(exec_path "${CMAKE_INSTALL_PREFIX}/@CMAKE_INSTALL_BINDIR@/dlna-server-gui")' in desktop
    assert "Icon=dlna-server" in desktop
    assert "StartupWMClass=com.github.dlna-server-14ag" in desktop
    assert 'gtk_application_new("com.github.dlna-server-14ag"' in (
        ROOT / "src" / "gtk4_gui_main.cpp"
    ).read_text()
    source = (ROOT / "src" / "gtk4_gui_main.cpp").read_text()
    assert "g_mainWindowWasUnminimized" in source
    assert "else if (g_mainWindowWasUnminimized)" in source
    assert "export GDK_BACKEND=x11" not in wrapper


def test_wslg_shortcut_command_is_available_on_windows():
    """Windows test runs only when WSL is installed and target distro exists."""
    if os.environ.get("DLNA_WSLG_RUNTIME_TEST") != "1":
        return
    import subprocess

    result = subprocess.run(
        ["wsl.exe", "-d", "Ubuntu", "--", "/usr/local/bin/dlna-server-gui", "--help"],
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert result.returncode == 0, result.stderr
