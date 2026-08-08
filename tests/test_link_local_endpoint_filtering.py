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
        REPO_ROOT / "build" / "dlna-server",
        REPO_ROOT / "build-linux" / "dlna-server",
    ):
        if candidate.is_file():
            return candidate
    return None


CLI_BINARY = _find_cli_binary()
_SKIP_REASON = "dlna-server cli binary not found; set DLNA_CLI_BINARY"


def _should_drop(candidate_is_link_local, any_non_link_local_exists):
    result = subprocess.run(
        [
            str(CLI_BINARY),
            "--print-should-drop-link-local-endpoint",
            "1" if candidate_is_link_local else "0",
            "1" if any_non_link_local_exists else "0",
        ],
        capture_output=True,
        text=True,
        timeout=10,
    )
    assert result.returncode == 0
    return result.stdout.strip() == "1"


@pytest.mark.skipif(CLI_BINARY is None, reason=_SKIP_REASON)
def test_link_local_dropped_when_a_better_endpoint_exists():
    assert _should_drop(True, True) is True


@pytest.mark.skipif(CLI_BINARY is None, reason=_SKIP_REASON)
def test_link_local_kept_when_it_is_the_only_endpoint():
    assert _should_drop(True, False) is False


@pytest.mark.skipif(CLI_BINARY is None, reason=_SKIP_REASON)
def test_non_link_local_never_dropped_when_alternative_exists():
    assert _should_drop(False, True) is False


@pytest.mark.skipif(CLI_BINARY is None, reason=_SKIP_REASON)
def test_non_link_local_never_dropped_when_it_is_the_only_endpoint():
    assert _should_drop(False, False) is False
