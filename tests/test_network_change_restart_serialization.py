import os
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]


def _read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def _run(binary_path, *args, timeout=120):
    env = os.environ.copy()
    env["DLNA_SERVER_SKIP_FIREWALL"] = "1"
    return subprocess.run(
        [str(binary_path), *args],
        capture_output=True, text=True, timeout=timeout, env=env,
    )


class TestNetworkChangeRestartSerialization:
    """Task 5 regression guards."""

    def test_powerbroadcast_case_no_longer_calls_ssdp_directly(self):
        # the WM_POWERBROADCAST handler must route through
        # Server::RestartSsdpForNetworkChange so it cannot race the
        # network change watcher thread on the same SSDP Stop Start pair
        mainwindow = _read("src/mainwindow.cpp")
        assert "SSDP::Get().Stop()" not in mainwindow
        assert "RestartSsdpForNetworkChange()" in mainwindow

    def test_restart_ssdp_for_network_change_defined_once_per_platform(self):
        for path in ("src/server.cpp", "src/posix_server.cpp"):
            source = _read(path)
            assert source.count("void Server::RestartSsdpForNetworkChange()") == 1

    def test_concurrent_restart_calls_leave_server_running_and_healthy(self, dlna_binary, tmp_path):
        # two threads calling RestartSsdpForNetworkChange within
        # milliseconds of each other must serialize and coalesce and the
        # server must end up running and healthy
        media_dir = tmp_path / "media"
        media_dir.mkdir()
        (media_dir / "sample.mp4").write_bytes(b"\x00" * 4096)

        result = _run(dlna_binary, "--source", str(media_dir),
                      "--print-network-change-restart-coalescing")
        assert result.returncode == 0, result.stdout + result.stderr
        assert "is-running=1" in result.stdout
        assert "is-healthy=1" in result.stdout