import subprocess

import pytest


@pytest.mark.parametrize("candidate_is_link_local,any_non_link_local,expected", [
    ("1", "1", "1"),  # dropped when a better endpoint exists
    ("1", "0", "0"),  # kept when it is the only endpoint
    ("0", "1", "0"),
    ("0", "0", "0"),
])
def test_should_drop_link_local_endpoint(dlna_binary, candidate_is_link_local, any_non_link_local, expected):
    result = subprocess.run(
        [dlna_binary, "--print-should-drop-link-local-endpoint",
         candidate_is_link_local, any_non_link_local],
        capture_output=True, text=True, timeout=10)
    assert result.returncode == 0
    assert result.stdout.strip() == expected
