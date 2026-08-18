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


class TestSsdpLifecycleHardening:
    """Tasks 2, 3, 4 regression guards."""

    def test_lifecycle_guard_present_in_start_and_stop_per_platform(self):
        # the compare_exchange_strong guard must wrap both Start and Stop
        # on both platforms so two concurrent callers cannot both pass
        # the old check-then-act m_running.load guard
        for path in ("src/ssdp.cpp", "src/posix_ssdp.cpp"):
            source = _read(path)
            assert source.count("m_lifecycleBusy.compare_exchange_strong") == 2

    def test_win32_endpoint_family_recheck_matches_posix(self):
        # Task 3: the Win32 HandleSearchRequest must re-check the
        # endpoint address family exactly like posix_ssdp.cpp already
        # does so SelectBestEndpoint's wrong-family fallback cannot
        # produce a search response on the wrong socket family
        win32 = _read("src/ssdp.cpp")
        assert "endpoint->family != remoteAddr->sa_family" in win32

    def test_concurrent_start_stop_guard_fires(self, dlna_binary, tmp_path):
        # two threads racing SSDP Stop Start pairs must produce at least
        # one rejected call proving the lifecycle guard actually fired
        # and must leave the server running not torn
        media_dir = tmp_path / "media"
        media_dir.mkdir()
        (media_dir / "sample.mp4").write_bytes(b"\x00" * 4096)

        result = _run(dlna_binary, "--source", str(media_dir),
                      "--print-ssdp-concurrent-start-stop-safety")
        assert result.returncode == 0, result.stdout + result.stderr
        assert "rejected-count=0" not in result.stdout
        assert "final-running=1" in result.stdout

    def test_bootid_persists_across_restart(self, dlna_binary, tmp_path):
        # BOOTID.UPNP.ORG must not change on a restart that is not a
        # genuine device reboot per UDA 1.1 section 1.2
        media_dir = tmp_path / "media"
        media_dir.mkdir()
        (media_dir / "sample.mp4").write_bytes(b"\x00" * 4096)

        result = _run(dlna_binary, "--source", str(media_dir),
                      "--print-ssdp-bootid-persistence")
        assert result.returncode == 0, result.stdout + result.stderr
        first = second = None
        for line in result.stdout.splitlines():
            if line.startswith("first="):
                parts = line.split(" ")
                first = parts[0].split("=")[1]
                second = parts[1].split("=")[1]
        assert first is not None and second is not None
        assert first == second