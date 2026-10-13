"""Repro for transient debug.log read race (POSIX Errno 61 ENODATA)."""
from pathlib import Path
from unittest.mock import patch

from tests.test_vlc_detection_contract import _read_ssdp_log


def test_transient_enodata_retries_then_returns_content(tmp_path):
    log = tmp_path / "debug.log"
    log.write_text("hello")
    calls = {"n": 0}
    real = Path.read_text

    def flaky(self, *a, **k):
        if str(self) == str(log) and calls["n"] == 0:
            calls["n"] += 1
            raise OSError(61, "No data available")
        return real(self, *a, **k)

    with patch.object(Path, "read_text", flaky):
        assert _read_ssdp_log(str(tmp_path)) == "hello"


def test_persistent_read_error_returns_empty(tmp_path):
    (tmp_path / "debug.log").write_text("x")
    with patch.object(Path, "read_text", side_effect=OSError(61, "No data available")):
        assert _read_ssdp_log(str(tmp_path)) == ""
