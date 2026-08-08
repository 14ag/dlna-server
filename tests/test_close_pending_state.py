import subprocess

import pytest


def _run(dlna_binary, *args):
    result = subprocess.run(
        [dlna_binary, *args],
        capture_output=True,
        text=True,
        timeout=10,
    )
    assert result.returncode == 0
    return [line for line in result.stdout.splitlines() if line.strip()]


def test_close_pending_lifecycle(dlna_binary):
    lines = _run(dlna_binary, "--print-close-pending-lifecycle")
    expected = [
        "initial-pending=0",
        "after-request-pending=1",
        "stop-completes-close-now=1",
        "after-close-pending=0",
        "restart-reaches-running-stop-again=1",
        "restart-still-pending=1",
        "second-stop-completes-close-now=1",
        "never-requested-pending=0",
    ]
    assert lines == expected
