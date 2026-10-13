import subprocess

import pytest


# Expected values traced from AssignOneMnemonic in src/access_keys.cpp: first
# non-space, non-punctuation character whose uppercase is unused; '' when none.
@pytest.mark.parametrize("labels,expected", [
    ("Add,Delete,Start,Settings", "A,D,S,e"),
    ("Logs,Help", "L,H"),
    ("OK", "O"),
    ("AB,AB,AB", "A,B,"),
    ("Browse...,Browse...", "B,r"),
    ("Show Window,Stop Server,Exit", "S,t,E"),
    ("Show Window,Start Server,Exit", "S,t,E"),
    ("&A,&&B&&C,Settings", "A,B,S"),
    ("&Add", "A"),
    ("A&&B", "A"),
])
def test_mnemonics(dlna_binary, labels, expected):
    result = subprocess.run(
        [dlna_binary, "--print-mnemonics", labels],
        capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == expected
