import time
from pathlib import Path

import pytest

from tests.conftest import _launch_server, _teardown_server, _free_port


def _debug_log(binary_dir):
    p = Path(binary_dir) / "debug.log"
    if p.exists():
        return p.read_text(encoding="utf-8", errors="replace")
    return ""


def test_no_fileserverport_deprecation_notice(dlna_binary, tmp_path):
    binary_path = Path(dlna_binary)
    port = _free_port()
    proc, connected, old_config, config_ini = _launch_server(
        binary_path, port, str(tmp_path))
    if not connected:
        _teardown_server(proc, old_config, config_ini)
        pytest.fail(f"Server did not listen on port {port} within 15s")
    try:
        time.sleep(1)
        log_text = _debug_log(binary_path.parent)
        assert "FileServerPort" not in log_text
        assert "deprecated" not in log_text.lower()
    finally:
        _teardown_server(proc, old_config, config_ini)
