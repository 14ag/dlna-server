import re
import struct
import subprocess
from pathlib import Path

import pytest

from tests.identity import APP_ID, GTK_APP_ID

ROOT = Path(__file__).resolve().parents[1]
DESKTOP_TEMPLATES = (
    "packaging/linux/install_desktop.cmake.in",
    "packaging/linux/portable.desktop.in",
)


def _read(relative_path):
    return (ROOT / relative_path).read_text(encoding="utf-8")


def _png_header(path):
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    width, height, depth, color, _comp, _filter, interlace = struct.unpack(">IIBBBBB", data[16:29])
    return width, height, depth, color, interlace


CMAKE = _read("CMakeLists.txt")
GTK_SOURCE = _read("src/gtk4_gui_main.cpp")


@pytest.mark.parametrize("template", DESKTOP_TEMPLATES)
def test_desktop_entry_uses_one_bare_identity(template):
    text = _read(template)
    assert "\nIcon=@DLNA_GTK_APP_ID@\n" in text
    assert "\nStartupWMClass=@DLNA_GTK_APP_ID@\n" in text
    assert "Icon=/" not in text
    assert "\nName=DLNA Server\n" in text


def test_deb_desktop_file_is_named_by_app_id():
    text = _read("packaging/linux/install_desktop.cmake.in")
    assert 'file(WRITE "${applications_dir}/@DLNA_GTK_APP_ID@.desktop"' in text
    assert "Exec=env DLNA_SERVER_GTK_LAUNCHED=1 /usr/bin/dlna-server-gui" in text
    assert "TryExec=/usr/bin/dlna-server-gui" in text
    assert "gtk-update-icon-cache" in text


def test_postinst_touches_the_app_id_desktop_file():
    assert "@DLNA_GTK_APP_ID@.desktop" in _read("packaging/linux/postinst.in")


def test_png_icons_installed_where_wslg_searches():
    assert re.search(r"/pixmaps\s+RENAME \$\{DLNA_GTK_APP_ID\}\.png", CMAKE)
    assert re.search(r"hicolor/48x48/apps\s+RENAME \$\{DLNA_GTK_APP_ID\}\.png", CMAKE)
    assert re.search(r"hicolor/scalable/apps\s+RENAME \$\{DLNA_GTK_APP_ID\}\.svg", CMAKE)
    assert "DLNA_APP_ICON" not in CMAKE
    assert "DLNA_WSLG_APP_ID" not in CMAKE


@pytest.mark.parametrize("name", ["16", "48", "120", "256"])
def test_icon_png_is_plain_square_8bit_noninterlaced(name):
    width, height, depth, color, interlace = _png_header(ROOT / "resources" / f"server_icon_{name}.png")
    assert width == height
    assert depth == 8
    assert color in (2, 6)
    assert interlace == 0


def test_gtk_source_sets_one_identity_before_gtk_starts():
    assert "g_set_prgname(DLNA_GTK_APP_ID);" in GTK_SOURCE
    assert "gdk_set_program_class" not in GTK_SOURCE
    assert GTK_SOURCE.index("g_set_prgname(DLNA_GTK_APP_ID);") < GTK_SOURCE.index("g_application_register(")
    assert "gtk_application_new(DLNA_GTK_APP_ID," in GTK_SOURCE
    assert "gtk_window_set_icon_name(window, DLNA_GTK_APP_ID)" in GTK_SOURCE
    assert "PosixTray::Initialize(connection, DLNA_GTK_APP_ID," in GTK_SOURCE


def test_gtk_source_sets_short_wayland_window_id():
    # WSLg cuts the app list key at the last dot
    # so the live Wayland window must carry the short id
    # to match the Start Menu entry and its taskbar icon
    for removed in (
        "DLNA_WSLG_APP_ID",
        "DLNA_APP_ICON",
    ):
        assert removed not in GTK_SOURCE
    assert "gdk_wayland_toplevel_set_application_id" in GTK_SOURCE
    assert "gdkwayland.h" in GTK_SOURCE
    call = GTK_SOURCE.split("gdk_wayland_toplevel_set_application_id(")[1].split(";")[0]
    assert "DLNA_APP_ID" in call
    assert "DLNA_GTK_APP_ID" not in call


def test_start_menu_launch_contract():
    desktop = _read("packaging/linux/install_desktop.cmake.in")
    wrapper = _read("packaging/linux/dlna-server-gui")
    assert 'if [ "${DLNA_SERVER_GTK_LAUNCHED:-}" = "1" ]; then' in wrapper
    assert 'exec "$native_gui" "$@"' in wrapper
    assert 'exec dbus-run-session -- "$native_gui" "$@"' in wrapper
    assert "exec gtk-launch " not in wrapper
    assert "Exec=env DLNA_SERVER_GTK_LAUNCHED=1 /usr/bin/dlna-server-gui" in desktop


@pytest.mark.posix_only
def test_gui_binary_reports_one_identity(dlna_server_gui_binary):
    result = subprocess.run(
        [dlna_server_gui_binary, "--print-gtk-identity"],
        capture_output=True, text=True, timeout=30)
    values = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
    assert values == {
        "PRGNAME": GTK_APP_ID,
        "PROGRAM_CLASS": APP_ID,
        "GTK_APP_ID": GTK_APP_ID,
    }

@pytest.mark.posix_only
def test_wslg_weston_icon_contract():
    weston_log = Path("/mnt/wslg/weston.log")
    if not weston_log.exists():
        return
    text = weston_log.read_text(encoding="utf-8", errors="ignore")
    # the associate notify always carries appIcon nil
    # the real signal is the loadIconEvent entry and image match
    matches = re.findall(r"loadIconEvent is signalled\. (\S+)[\s\S]{0,200}?entry (\S+), image (\S+)", text)
    dlna_icons = [(entry.strip(), image.strip()) for app, entry, image in matches if "dlna" in app.lower()]
    if dlna_icons:
        latest_entry, latest_image = dlna_icons[-1]
        assert latest_entry != "(nil)", f"WSLg weston.log shows entry is (nil): {latest_entry}"
        assert latest_image != "(nil)", f"WSLg weston.log shows image is (nil): {latest_image}"

