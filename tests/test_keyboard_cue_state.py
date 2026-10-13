import subprocess

import pytest


@pytest.mark.parametrize("seq,expected", [
    ("x", ["1,1"]),
    ("k", ["0,0"]),
    ("m", ["1,1"]),
    ("km", ["0,0", "1,1"]),
    ("mk", ["1,1", "0,0"]),
])
def test_cue_state(dlna_binary, seq, expected):
    result = subprocess.run(
        [dlna_binary, "--print-cue-state", seq],
        capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip().splitlines() == expected
