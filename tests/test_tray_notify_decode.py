import subprocess

import pytest

TRAY_ID = 1
WM_LBUTTONUP = 0x0202
WM_LBUTTONDBLCLK = 0x0203
WM_RBUTTONUP = 0x0205
WM_CONTEXTMENU = 0x007B
NIN_SELECT = 0x0400
WM_MOUSEMOVE = 0x0200


def _pack(icon_id, event):
    return (icon_id << 16) | event


@pytest.mark.parametrize("icon_id,event,expected", [
    (TRAY_ID, WM_LBUTTONUP, "activate"),
    (TRAY_ID, WM_LBUTTONDBLCLK, "activate"),
    (TRAY_ID, NIN_SELECT, "activate"),
    (TRAY_ID, WM_CONTEXTMENU, "showmenu"),
    (TRAY_ID, WM_RBUTTONUP, "showmenu"),
    (TRAY_ID, WM_MOUSEMOVE, "none"),
    (2, WM_LBUTTONUP, "none"),  # different icon id is ignored
])
def test_tray_notify_decode(dlna_binary, icon_id, event, expected):
    result = subprocess.run(
        [dlna_binary, "--print-tray-notify-decode", hex(_pack(icon_id, event)), str(TRAY_ID)],
        capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == expected
