import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]


def _read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def _run(binary_path, *args, timeout=120):
    import os

    env = os.environ.copy()
    env["DLNA_SERVER_SKIP_FIREWALL"] = "1"
    return subprocess.run(
        [str(binary_path), *args],
        capture_output=True, text=True, timeout=timeout, env=env,
    )


# (isRunning, isHealthy, consecutiveUnhealthyPolls, expected)
PREDICATE_ROWS = [
    (1, 1, 0, 0),
    (1, 0, 1, 0),
    (1, 0, 3, 0),
    (1, 0, 4, 1),
    (1, 0, 10, 1),
    (0, 0, 10, 0),
]


class TestHealthCheckGracePeriod:
    """Task 7 regression guards."""

    @pytest.mark.parametrize(
        "is_running,is_healthy,consecutive,expected", PREDICATE_ROWS
    )
    def test_should_treat_server_unhealthy_predicate(
        self, dlna_binary, is_running, is_healthy, consecutive, expected
    ):
        # a single unhealthy poll during an in-flight SSDP restart must
        # stay inside the grace period; only kMinConsecutiveUnhealthyPolls
        # consecutive unhealthy polls may kill the server, and a stopped
        # server must never be treated as unhealthy
        result = _run(
            dlna_binary,
            "--print-should-treat-server-unhealthy",
            str(is_running), str(is_healthy), str(consecutive),
        )
        assert result.returncode == 0, result.stdout + result.stderr
        assert result.stdout.strip() == str(expected)

    def test_all_front_ends_call_grace_policy(self):
        # every front end that polls health must route through the shared
        # grace-period predicate instead of acting on a single sample
        for path in ("src/mainwindow.cpp", "src/posix_main.cpp",
                     "src/gtk4_gui_main.cpp"):
            source = _read(path)
            assert "ShouldTreatServerAsUnhealthy(" in source