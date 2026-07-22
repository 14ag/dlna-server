import subprocess
import pytest


@pytest.mark.parametrize("before,after,expected", [
    ("0000000", "0000000", "0"),
    ("0000000", "1000000", "1"),
    ("0000000", "0100000", "1"),
    ("0000000", "0010000", "1"),
    ("0000000", "0001000", "1"),
    ("0000000", "0000100", "1"),
    ("0000000", "0000010", "1"),
    ("0000000", "0000001", "1"),
])
def test_media_browsing_setting_requires_restart(dlna_binary, before, after, expected):
    result = subprocess.run(
        [dlna_binary, "--print-media-browsing-restart-required", before, after],
        capture_output=True, text=True, timeout=10)
    assert result.returncode == 0
    assert result.stdout.strip() == expected


def test_debug_log_restart_hook_unaffected(dlna_binary):
    result = subprocess.run(
        [dlna_binary, "--print-debug-log-requires-restart", "0", "1"],
        capture_output=True, text=True, timeout=10)
    assert result.returncode == 0
    assert result.stdout.strip() == "1"
