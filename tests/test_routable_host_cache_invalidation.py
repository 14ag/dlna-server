import os
import subprocess
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent


def _find_cli_binary():
    env_path = os.environ.get("DLNA_CLI_BINARY")
    if env_path and Path(env_path).is_file():
        return Path(env_path)
    server_path = os.environ.get("DLNA_SERVER")
    if server_path and Path(server_path).is_file():
        return Path(server_path)
    for candidate in (
        REPO_ROOT / "output" / "linux" / "dlna-server",
    ):
        if candidate.is_file():
            return candidate
    return None


CLI_BINARY = _find_cli_binary()
_SKIP_REASON = "dlna-server cli binary not found; set DLNA_CLI_BINARY"


def _read_invalidation_counts():
    result = subprocess.run(
        [str(CLI_BINARY), "--print-routable-host-cache-invalidation"],
        capture_output=True,
        text=True,
        timeout=10,
    )
    assert result.returncode == 0, result.stderr
    counts = {}
    for line in result.stdout.splitlines():
        line = line.strip()
        if "=" in line:
            key, value = line.split("=", 1)
            counts[key] = int(value)
    return counts


@pytest.mark.skipif(CLI_BINARY is None, reason=_SKIP_REASON)
def test_cache_recomputes_after_invalidation():
    counts = _read_invalidation_counts()
    assert counts.get("before") == 0
    assert counts.get("after-first-call") == 1
    assert counts.get("after-second-call-same-port") == 1
    assert counts.get("after-invalidate-then-call") == 2
