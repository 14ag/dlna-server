import subprocess

import pytest


@pytest.mark.parametrize("reported,capacity,expected", [
    ("28", "128", "1"),
    ("4096", "128", "0"),
    ("0", "128", "0"),
    ("-1", "128", "0"),
])
def test_sockaddr_length_safety(dlna_binary, reported, capacity, expected):
    result = subprocess.run(
        [dlna_binary, "--print-sockaddr-length-safety", reported, capacity],
        capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == expected
