import os
import subprocess

import pytest

CASES = [
    ("0", "0", "0", "[]"),
    ("1", "0", "0", "[starting server...]"),
    ("2", "0", "0", "[Server running]"),
    ("2", "0", "1", "[temporary source]"),
    ("2", "1", "1", "[scanning...]"),
    ("0", "1", "0", "[scanning...]"),
    ("3", "0", "0", "[stopping server...]"),
]


@pytest.mark.parametrize("state,scanning,temporary,expected", CASES)
def test_status_text(dlna_binary, tmp_path, state, scanning, temporary, expected):
    env = dict(os.environ, HOME=str(tmp_path), XDG_CONFIG_HOME=str(tmp_path))
    result = subprocess.run(
        [dlna_binary, "--print-status-text", state, scanning, temporary],
        capture_output=True,
        text=True,
        env=env,
        timeout=30,
    )
    assert result.returncode == 0
    assert result.stdout.strip() == expected
