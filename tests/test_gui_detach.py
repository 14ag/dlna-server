import os
import subprocess
import time

import pytest

pytestmark = pytest.mark.posix_only


def test_detach_returns_before_child_finishes(dlna_binary, tmp_path):
    marker = tmp_path / "marker.txt"
    env = dict(os.environ, HOME=str(tmp_path), XDG_CONFIG_HOME=str(tmp_path))
    result = subprocess.run(
        [dlna_binary, "--print-detach-marker", str(marker)],
        stdout=subprocess.PIPE,
        text=True,
        env=env,
        timeout=30,
        # stderr must not be a pipe the detached child keeps a non tty
        # stderr open so a pipe here would block until the child exits
        stderr=subprocess.DEVNULL,
    )
    assert result.returncode == 0
    assert result.stdout == ""
    assert not marker.exists()
    deadline = time.time() + 5
    while not marker.exists() and time.time() < deadline:
        time.sleep(0.1)
    assert marker.exists()
