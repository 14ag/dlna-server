import os
import subprocess

import pytest


@pytest.mark.parametrize("state_value,expected", [
    (0, "1"),
    (1, "0"),
    (2, "1"),
    (3, "0"),
])
def test_worker_is_joined_only_for_final_results(dlna_binary, tmp_path, state_value, expected):
    env = dict(os.environ, HOME=str(tmp_path), XDG_CONFIG_HOME=str(tmp_path))
    result = subprocess.run(
        [dlna_binary, "--print-should-join-worker-for-result", str(state_value)],
        capture_output=True,
        text=True,
        env=env,
        timeout=30,
    )
    assert result.returncode == 0
    assert result.stdout.strip() == expected
