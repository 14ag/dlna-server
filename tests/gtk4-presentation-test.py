"""Collective GTK4 presentation contract for WSLg/Windows integration.

This validates the three user-visible requirements that matter for the app's
native Linux presentation:

1. the Windows Start Menu item has a stable icon
2. the running window has the correct taskbar icon in WSLg
3. the Windows Start Menu shortcut launches the app via the configured desktop entry

The checks intentionally follow the existing repo patterns: they validate the
static desktop metadata and GTK4 identity contract rather than relying on a
flaky end-to-end desktop UI.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def _read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def test_start_menu_icon_contract():
    """WSLg/Windows Start Menu icons must resolve through the desktop entry.

    This covers the start-menu icon requirement directly: the desktop entry must
    declare a concrete icon name, the matching StartupWMClass, and the install
    rules must write the icon into the directories WSLg checks for app icons.
    """
    desktop = _read("packaging/linux/install_desktop.cmake.in")
    cmake = _read("CMakeLists.txt")

    assert "Name=DLNA Server" in desktop
    assert "Icon=dlna-server" in desktop
    assert "StartupWMClass=com.github.dlna-server-14ag" in desktop
    assert "TryExec=/usr/bin/dlna-server-gui-bin" in desktop
    assert "Exec=env DLNA_SERVER_GTK_LAUNCHED=1 /usr/bin/dlna-server-gui" in desktop
    assert "gtk-update-icon-cache" in desktop
    assert "update-desktop-database" in desktop

    assert "DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/48x48/apps   RENAME dlna-server.png" in cmake
    assert "DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps RENAME dlna-server.png" in cmake
    assert "DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps RENAME dlna-server)" in cmake
    assert "DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps RENAME dlna-server.png" in cmake
    assert "DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps RENAME dlna-server)" in cmake


def test_taskbar_icon_contract():
    """The running GTK4 window must advertise the compositor app-id that WSLg expects.

    WSLg resolves the taskbar icon from the app's wayland app-id / desktop metadata.
    The GTK app-id must stay valid and reverse-DNS, while the compositor-facing id must
    match the desktop entry's basename-derived app key.
    """
    source = _read("src/gtk4_gui_main.cpp")
    desktop = _read("packaging/linux/install_desktop.cmake.in")

    assert 'gtk_application_new("com.github.dlna-server-14ag"' in source
    assert 'g_set_prgname("dlna-server-14ag")' in source
    assert 'gdk_wayland_toplevel_set_application_id(toplevel, "dlna-server-14ag")' in source
    assert 'gtk_window_set_icon_name(window, "dlna-server")' in source
    assert 'g_signal_connect_after(window, "map", G_CALLBACK(OnWindowMap)' in source
    assert "StartupWMClass=com.github.dlna-server-14ag" in desktop
    assert "Icon=dlna-server" in desktop


def test_windows_start_menu_shortcut_launch_contract():
    """The Windows shortcut must route through the installed desktop entry and launch the app."""
    wrapper = _read("packaging/linux/dlna-server-gui")
    desktop = _read("packaging/linux/install_desktop.cmake.in")
    cmake = _read("CMakeLists.txt")

    assert 'exec gtk-launch dlna-server "$@"' in wrapper
    assert "DLNA_SERVER_GTK_LAUNCHED=1" in desktop
    assert "Exec=env DLNA_SERVER_GTK_LAUNCHED=1 /usr/bin/dlna-server-gui" in desktop
    assert "TryExec=/usr/bin/dlna-server-gui-bin" in desktop
    assert "gtk-launch dlna-server" in wrapper
    assert "DLNA_SERVER_GTK_LAUNCHED=1" in wrapper
    assert 'set(CPACK_GENERATOR "DEB")' in cmake
    assert 'set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)' in cmake
