import subprocess

import pytest


@pytest.mark.parametrize("sequence,expected", [
    ("e1", [1]),
    ("e1,l1", [1, -1]),
    ("e1,l1,e1", [1, -1, 1]),
    ("e1,l1,e1,l1,e1", [1, -1, 1, -1, 1]),
    ("f2", [2]),
    ("f2,b2", [2, -1]),
    ("f2,e1", [2, 1]),
    ("f2,e1,l1", [2, 1, 2]),
    ("e1,l3", [1, 1]),
    ("f2,b3", [2, 2]),
    ("", []),
    ("e0", [0]),
    ("e4", [4]),
    ("e205", [205]),
])
def test_hover_focus_state(dlna_binary, sequence, expected):
    result = subprocess.run(
        [dlna_binary, "--print-hover-focus-state", sequence],
        capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stderr
    assert [int(line) for line in result.stdout.strip().splitlines() if line] == expected
