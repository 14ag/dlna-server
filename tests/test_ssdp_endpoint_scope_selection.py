import subprocess

import pytest

pytestmark = pytest.mark.posix_only


def test_select_best_endpoint_picks_scoped_interface(dlna_binary):
    result = subprocess.run(
        [dlna_binary, "--print-select-best-endpoint-scope-match"],
        capture_output=True, text=True, timeout=10,
    )
    assert result.returncode == 0
    assert "picked-interface-index=5" in result.stdout