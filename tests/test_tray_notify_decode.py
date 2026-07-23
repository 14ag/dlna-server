import os
import subprocess
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent


def _find_cli_binary():
    env_path = os.environ.get("DLNA_CLI_BINARY")
    if env_path and Path(env_path).is_file():
        return Path(env_path)
    for candidate in (
        REPO_ROOT / "build" / "dlna-server",
        REPO_ROOT / "build-linux" / "dlna-server",
    ):
        if candidate.is_file():
            return candidate
    return None


CLI_BINARY = _find_cli_binary()
_SKIP_REASON = "dlna-server cli binary not found; set DLNA_CLI_BINARY"

TRAY_ID = 1
WM_LBUTTONUP = 0x0202
WM_LBUTTONDBLCLK = 0x0203
WM_RBUTTONUP = 0x0205
WM_CONTEXTMENU = 0x007B
NIN_SELECT = 0x0400
WM_MOUSEMOVE = 0x0200


def _pack(icon_id, event):
    return (icon_id << 16) | event


def _decode(raw_lparam, expected_icon_id):
    result = subprocess.run(
        [
            str(CLI_BINARY),
            "--print-tray-notify-decode",
            hex(raw_lparam),
            str(expected_icon_id),
        ],
        capture_output=True,
        text=True,
        timeout=10,
    )
    assert result.returncode == 0, result.stderr
    return result.stdout.strip()


@pytest.mark.skipif(CLI_BINARY is None, reason=_SKIP_REASON)
@pytest.mark.parametrize(
    "event,expected",
    [
        (WM_LBUTTONUP, "activate"),
        (WM_LBUTTONDBLCLK, "activate"),
        (NIN_SELECT, "activate"),
        (WM_CONTEXTMENU, "showmenu"),
        (WM_RBUTTONUP, "showmenu"),
        (WM_MOUSEMOVE, "none"),
    ],
)
def test_decode_recognizes_expected_events(event, expected):
    raw_lparam = _pack(TRAY_ID, event)
    assert _decode(raw_lparam, TRAY_ID) == expected


@pytest.mark.skipif(CLI_BINARY is None, reason=_SKIP_REASON)
def test_decode_ignores_events_for_a_different_icon_id():
    other_icon_id = 2
    raw_lparam = _pack(other_icon_id, WM_LBUTTONUP)
    assert _decode(raw_lparam, TRAY_ID) == "none"


@pytest.mark.skipif(CLI_BINARY is None, reason=_SKIP_REASON)
def test_pre_version_four_style_raw_lparam_would_have_failed_old_code():
    raw_lparam = _pack(TRAY_ID, WM_LBUTTONUP)
    assert raw_lparam != WM_LBUTTONUP
    assert _decode(raw_lparam, TRAY_ID) == "activate"
