import subprocess

import pytest

MAX = 2147483646


@pytest.mark.parametrize("total,expected", [
    (1000, [1000]),
    (MAX, [MAX]),
    (MAX + 1, [MAX, 1]),
    (MAX * 2 + 500, [MAX, MAX, 500]),
    (0, []),
])
def test_transmitfile_chunk_plan(dlna_binary, total, expected):
    result = subprocess.run(
        [dlna_binary, "--print-transmitfile-chunk-plan", str(total)],
        capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stderr
    chunks = [int(line) for line in result.stdout.splitlines() if line.strip()]
    assert chunks == expected
    assert sum(chunks) == total
