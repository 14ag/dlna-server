import subprocess

import pytest


@pytest.mark.parametrize("vk,running,busy,scanning,expected", [
    ("112", "0", "0", "0", "show-help"),
    ("112", "1", "1", "1", "show-help"),
    ("116", "1", "0", "0", "rescan"),
    ("116", "0", "0", "0", "refresh-source-list"),
    ("116", "1", "1", "0", "none"),
    ("116", "1", "0", "1", "none"),
    ("65", "0", "0", "0", "none"),
])
def test_function_key_action(dlna_binary, vk, running, busy, scanning, expected):
    result = subprocess.run(
        [dlna_binary, "--print-function-key-action", vk, running, busy, scanning],
        capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == expected
