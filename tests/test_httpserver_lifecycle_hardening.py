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


class TestHttpServerLifecycleHardening:
    """Task 8 regression guards."""

    def test_lifecycle_guard_present_in_start_and_stop_per_platform(self):
        # the compare_exchange_strong guard must wrap both Start and Stop
        # on both platforms so two concurrent callers cannot both pass
        # the old check-then-act m_running guard
        for path in ("src/httpserver.cpp", "src/posix_httpserver.cpp"):
            source = _read(path)
            assert source.count("m_lifecycleBusy.compare_exchange_strong") == 2

    def test_concurrent_start_stop_guard_fires(self, dlna_binary, tmp_path):
        # two threads racing HttpServer Stop Start pairs must produce at
        # least one rejected call proving the lifecycle guard actually
        # fired and must leave the HTTP server healthy not torn
        media_dir = tmp_path / "media"
        media_dir.mkdir()
        (media_dir / "sample.mp4").write_bytes(b"\x00" * 4096)

        result = _run(dlna_binary, "--source", str(media_dir),
                      "--print-httpserver-concurrent-start-stop-safety")
        assert result.returncode == 0, result.stdout + result.stderr
        assert "rejected-count=0" not in result.stdout
        assert "is-healthy=1" in result.stdout