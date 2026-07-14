import math
import subprocess

import pytest


def _closed_form(n):
    """clamp(1 + floor(19*ln(n)/ln(200)), 1, 20) for n>1, else 1."""
    if n <= 1:
        return 1
    raw = 1.0 + math.floor(19.0 * math.log(float(n)) / math.log(200.0))
    return max(1, min(20, int(raw)))


class TestPlaylistScanConcurrency:
    @pytest.mark.parametrize("n", [0, 1, 2, 10, 50, 200, 5000])
    def test_matches_closed_form_at_knots(self, dlna_binary, n):
        result = subprocess.run(
            [dlna_binary, "--print-scan-concurrency", str(n)],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, (
            f"Binary exited with code {result.returncode}: "
            f"{result.stderr.strip()}")
        actual = int(result.stdout.strip())
        expected = _closed_form(n)
        assert actual == expected, (
            f"N={n}: expected {expected}, got {actual}")
