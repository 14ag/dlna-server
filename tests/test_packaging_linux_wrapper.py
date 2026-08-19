import os
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
WRAPPER = ROOT / "packaging" / "linux" / "dlna-server-gui"

pytestmark = [pytest.mark.posix_only]


def test_wrapper_waits_promptly_when_compositor_socket_present(tmp_path):
    if not WRAPPER.is_file():
        pytest.fail("wrapper script not found")

    runtime = Path(tempfile.mkdtemp(prefix="dlna-wrapper-", dir="/tmp"))
    sock_path = runtime / "wayland-0"
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.bind(str(sock_path))

    dummy_gui = tmp_path / "native-gui"
    dummy_gui.write_text("#!/bin/sh\nexit 0\n")
    dummy_gui.chmod(0o755)

    env = dict(os.environ)
    env.pop("WAYLAND_DISPLAY", None)
    env.pop("DISPLAY", None)
    env["XDG_RUNTIME_DIR"] = str(runtime)
    env["DLNA_SERVER_GUI_BIN"] = str(dummy_gui)
    env["DLNA_SERVER_BIN"] = str(dummy_gui)

    try:
        start = time.monotonic()
        proc = subprocess.run(
            ["sh", str(WRAPPER)], env=env, capture_output=True, text=True, timeout=30
        )
        elapsed = time.monotonic() - start
    finally:
        sock.close()
        shutil.rmtree(runtime, ignore_errors=True)

    assert proc.returncode == 0, proc.stderr
    # the full wait loop would be 20 * 0.25s = 5s
    # a present socket must short-circuit so the wrapper exits promptly
    assert elapsed < 2.0, f"wrapper took {elapsed:.2f}s, expected prompt exit"