import subprocess

import pytest

pytestmark = pytest.mark.posix_only


def test_icon_cache_loads_once_and_reuses_bytes(dlna_server_binary):
    result = subprocess.run(
        [str(dlna_server_binary), "--print-icon-cache-lifecycle"],
        capture_output=True, text=True, timeout=10,
    )
    assert result.returncode == 0
    lines = dict(line.split("=", 1) for line in result.stdout.splitlines())
    assert lines["ok-first"] == "1"
    assert lines["ok-second"] == "1"
    assert lines["bytes-equal"] == "1"
    assert int(lines["after-first-load"]) == int(lines["before"]) + 1
    # second load must be served from cache: recompute count unchanged
    assert int(lines["after-second-load"]) == int(lines["after-first-load"])