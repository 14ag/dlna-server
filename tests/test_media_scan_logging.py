import time
from pathlib import Path

import pytest

from tests.conftest import _launch_server, _teardown_server, _free_port


def _debug_log(binary_dir):
    p = Path(binary_dir) / "debug.log"
    if p.exists():
        return p.read_text(encoding="utf-8", errors="replace")
    return ""


def test_local_unsupported_extension_is_silent(dlna_binary, tmp_path):
    (tmp_path / "unsupported.xyz").write_bytes(b"junk")
    (tmp_path / "supported.mp4").write_bytes(b"junk")
    binary_path = Path(dlna_binary)
    port = _free_port()
    proc, connected, old_config, config_ini = _launch_server(
        binary_path, port, str(tmp_path))
    if not connected:
        _teardown_server(proc, old_config, config_ini)
        pytest.fail(f"Server did not listen on port {port} within 15s")
    try:
        time.sleep(2)
        log_text = _debug_log(binary_path.parent)
        assert "[media:reject-extension]" not in log_text
    finally:
        _teardown_server(proc, old_config, config_ini)


@pytest.mark.skip(reason="requires local FTP test server fixture")
def test_remote_unsupported_extension_still_logs(run_server_headless, ftp_test_server):
    pass
