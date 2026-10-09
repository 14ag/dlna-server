"""Regression contract for GTK4 desktop, WSLg icons, and launcher identity."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CMAKE = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
APP_ID = re.search(r'^set\(DLNA_APP_ID "([^"]+)"\)', CMAKE, re.MULTILINE).group(1)
APP_NAME = "DLNA Server"
APP_ICON = APP_ID
GUI_LAUNCHER = "dlna-server-gui"
GUI_BINARY = "dlna-server-gui-bin"
WSLG_APP_ID = APP_ID.rsplit(".", 1)[-1]
GTK_APP_ID = APP_ID.replace("-", "_")


def _read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def test_start_menu_icon_contract():
    desktop = _read("packaging/linux/install_desktop.cmake.in")

    assert f'file(WRITE "${{applications_dir}}/@DLNA_APP_ID@.desktop"' in desktop
    assert f"Name={APP_NAME}" in desktop
    assert "Icon=@DLNA_APP_ICON@" in desktop
    assert 'set(DLNA_APP_ICON "${DLNA_APP_ID}")' in CMAKE
    assert "DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps RENAME ${DLNA_APP_ICON}.png" in CMAKE
    assert "StartupWMClass=@DLNA_WSLG_APP_ID@" in desktop
    assert f"RENAME ${{DLNA_APP_ICON}}.png" in CMAKE
    assert f"RENAME ${{DLNA_APP_ICON}}.svg" in CMAKE
    assert APP_ICON == APP_ID
    assert "gtk-update-icon-cache" in desktop


def test_taskbar_icon_contract():
    source = _read("src/gtk4_gui_main.cpp")
    identity = _read("src/app_identity.h.in")
    desktop = _read("packaging/linux/install_desktop.cmake.in")

    assert '#define DLNA_APP_ID "@DLNA_APP_ID@"' in identity
    assert '#define DLNA_GTK_APP_ID "@DLNA_GTK_APP_ID@"' in identity
    assert '#define DLNA_WSLG_APP_ID "@DLNA_WSLG_APP_ID@"' in identity
    assert '#define DLNA_APP_ICON "@DLNA_APP_ICON@"' in identity
    assert "gtk_application_new(DLNA_GTK_APP_ID," in source
    assert "g_set_prgname(DLNA_WSLG_APP_ID);" in source
    assert "gdk_wayland_toplevel_set_application_id(toplevel, DLNA_WSLG_APP_ID);" in source
    assert "gtk_window_set_icon_name(window, DLNA_APP_ICON)" in source
    assert 'g_signal_connect_after(window, "map", G_CALLBACK(OnWindowMap)' in source
    assert "StartupWMClass=@DLNA_WSLG_APP_ID@" in desktop
    assert re.search(r'string\(REGEX REPLACE .* DLNA_WSLG_APP_ID "\$\{DLNA_APP_ID\}"\)', CMAKE)
    assert 'string(REPLACE "-" "_" DLNA_GTK_APP_ID "${DLNA_APP_ID}")' in CMAKE
    assert GTK_APP_ID == APP_ID


def test_start_menu_launch_contract():
    desktop = _read("packaging/linux/install_desktop.cmake.in")
    wrapper = _read("packaging/linux/dlna-server-gui")

    assert f"Exec=env DLNA_SERVER_GTK_LAUNCHED=1 /usr/bin/{GUI_LAUNCHER}" in desktop
    assert f"TryExec=/usr/bin/{GUI_LAUNCHER}" in desktop
    assert 'if [ "${DLNA_SERVER_GTK_LAUNCHED:-}" = "1" ]; then' in wrapper
    assert 'exec "$native_gui" "$@"' in wrapper
    assert 'exec dbus-run-session -- "$native_gui" "$@"' in wrapper
    assert "exec gtk-launch " not in wrapper
