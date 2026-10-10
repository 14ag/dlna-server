import time
from pathlib import Path

import pytest

from tests.conftest import _free_port, _launch_server, _teardown_server


@pytest.mark.posix_only
def test_missing_source_is_not_registered_with_inotify(dlna_binary, tmp_path):
    real = tmp_path / "media"
    real.mkdir()
    missing = tmp_path / "gone" / "missing.mkv"
    port = _free_port()
    proc, ok, old, ini = _launch_server(
        Path(dlna_binary), port, f'"{real}","{missing}"', config_dir=tmp_path)
    try:
        assert ok, "server did not listen"
        log = ini.parent / "debug.log"
        deadline = time.time() + 15
        text = ""
        while time.time() < deadline:
            text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
            if "DLNA server running" in text and "Skipping missing source" in text:
                break
            time.sleep(0.25)
        assert "DLNA server running" in text
        assert "Skipping missing source" in text
        assert "inotify watch registration failed" not in text
    finally:
        _teardown_server(proc, old, ini)
