"""
pytest: Server::Stop() must cancel an in-progress media scan instead of
blocking until the scan completes on its own.

Run BEFORE implementing the fix to confirm reproduction (expect FAIL /
timeout on test_stop_cancels_inflight_scan). Run AFTER implementing the
fix to confirm resolution (expect PASS).
"""
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

import pytest
import requests

sys.path.insert(0, str(Path(__file__).parent))
from fixtures.slow_playlist_server import SlowPlaylistServer  # noqa: E402

DLNA_SERVER_BINARY = Path(
    os.environ.get("DLNA_SERVER_BINARY", "build/dlna-server")
).resolve()

STOP_TIMEOUT_SECONDS = 5
SCAN_START_GRACE_SECONDS = 3


def _isolated_binary(tmp_path: Path) -> Path:
    assert DLNA_SERVER_BINARY.exists(), (
        f"Build the POSIX binary first: {DLNA_SERVER_BINARY} not found. "
        "Set DLNA_SERVER_BINARY env var if it lives elsewhere."
    )
    dest = tmp_path / DLNA_SERVER_BINARY.name
    shutil.copy2(DLNA_SERVER_BINARY, dest)
    dest.chmod(0o755)
    return dest


def _wait_for_http(port: int, timeout: float):
    deadline = time.time() + timeout
    last_err = None
    while time.time() < deadline:
        try:
            r = requests.get(f"http://127.0.0.1:{port}/description.xml", timeout=1)
            if r.status_code == 200:
                return
        except requests.RequestException as e:
            last_err = e
        time.sleep(0.2)
    raise AssertionError(f"server never became reachable on port {port}: {last_err}")


def _free_port() -> int:
    import socket
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class TestScanCancellationLifecycleHook:
    def test_print_scan_cancellation_lifecycle(self, tmp_path):
        binary = _isolated_binary(tmp_path)
        result = subprocess.run(
            [str(binary), "--print-scan-cancellation-lifecycle"],
            capture_output=True, text=True, timeout=5,
        )
        assert result.returncode == 0
        lines = [ln.strip() for ln in result.stdout.strip().splitlines()]
        assert lines == ["0", "1", "0"], (
            f"expected BeginScan=0, RequestCancel=1, BeginScan=0; got {lines}"
        )


class TestStopCancelsInFlightScan:
    def test_stop_cancels_inflight_scan(self, tmp_path):
        binary = _isolated_binary(tmp_path)
        port = _free_port()

        with SlowPlaylistServer() as slow_server:
            proc = subprocess.Popen(
                [
                    str(binary),
                    "--port", str(port),
                    "--source", slow_server.playlist_url,
                ],
                cwd=tmp_path,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            try:
                _wait_for_http(port, timeout=10)
                time.sleep(SCAN_START_GRACE_SECONDS)

                proc.send_signal(signal.SIGTERM)
                try:
                    exit_code = proc.wait(timeout=STOP_TIMEOUT_SECONDS)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=5)
                    pytest.fail(
                        f"dlna-server did not exit within {STOP_TIMEOUT_SECONDS}s "
                        f"of SIGTERM while a slow scan was in progress -- "
                        f"this is the reported 'stopping server' hang."
                    )
                assert exit_code == 0
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait(timeout=5)


class TestCancelledScanDoesNotPruneCache:
    def test_stop_does_not_prune_cache_on_cancelled_scan(self, tmp_path):
        binary = _isolated_binary(tmp_path)

        local_media_dir = tmp_path / "media"
        local_media_dir.mkdir()
        (local_media_dir / "track.mp3").write_bytes(b"\x00" * 1024)

        port1 = _free_port()
        seed_proc = subprocess.Popen(
            [str(binary), "--port", str(port1), "--source", str(local_media_dir)],
            cwd=tmp_path,
        )
        try:
            _wait_for_http(port1, timeout=10)
            time.sleep(2)
        finally:
            seed_proc.send_signal(signal.SIGTERM)
            seed_proc.wait(timeout=STOP_TIMEOUT_SECONDS)

        cache_path = tmp_path / "media-cache.tsv"
        assert cache_path.exists(), "seed scan should have written media-cache.tsv"
        seeded_cache_text = cache_path.read_text()
        assert "track.mp3" not in seeded_cache_text
        seeded_record_count = sum(
            1 for ln in seeded_cache_text.splitlines() if ln and not ln.startswith("#")
        )
        assert seeded_record_count >= 1

        port2 = _free_port()
        with SlowPlaylistServer() as slow_server:
            proc = subprocess.Popen(
                [str(binary), "--port", str(port2), "--source", slow_server.playlist_url],
                cwd=tmp_path,
            )
            try:
                _wait_for_http(port2, timeout=10)
                time.sleep(SCAN_START_GRACE_SECONDS)
                proc.send_signal(signal.SIGTERM)
                proc.wait(timeout=STOP_TIMEOUT_SECONDS)
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait(timeout=5)

        post_cancel_cache_text = cache_path.read_text()
        assert post_cancel_cache_text == seeded_cache_text, (
            "media-cache.tsv changed after a CANCELLED scan -- "
            "PruneUntouched()/Save() must be skipped when "
            "ScanCancellation::IsCancelled() is true (see Task 4)."
        )
