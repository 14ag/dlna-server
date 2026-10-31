import os
import socket
import subprocess
import time
import urllib.error
import urllib.request

import pytest


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_for_description(port, proc, timeout=20):
    url = f"http://127.0.0.1:{port}/description.xml"
    deadline = time.time() + timeout
    while time.time() < deadline:
        if proc.poll() is not None:
            raise AssertionError(f"server exited with {proc.returncode}")
        try:
            with urllib.request.urlopen(url, timeout=1) as response:
                if response.status == 200:
                    return response.read()
        except (urllib.error.URLError, OSError):
            time.sleep(0.2)
    raise AssertionError(f"description.xml did not become ready on port {port}")


@pytest.mark.posix_only
def test_help_lists_foreground(dlna_server_binary):
    result = subprocess.run(
        [dlna_server_binary, "--help"],
        capture_output=True, text=True, timeout=10,
    )
    assert result.returncode == 0, result.stderr
    assert "--foreground" in result.stdout + result.stderr


@pytest.mark.posix_only
def test_foreground_flag_keeps_process_attached(dlna_server_binary, tmp_path):
    source = tmp_path / "media"
    source.mkdir()
    port = free_port()
    env = os.environ.copy()
    env["HOME"] = str(tmp_path)
    env["XDG_CONFIG_HOME"] = str(tmp_path)
    proc = subprocess.Popen(
        [
            dlna_server_binary,
            "--foreground",
            "--no-debug",
            "--port", str(port),
            "--source", str(source),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=env,
    )
    try:
        body = wait_for_description(port, proc)
        assert b"<root" in body
        time.sleep(0.5)
        assert proc.poll() is None
    finally:
        if proc.poll() is None:
            proc.terminate()
        try:
            stdout, stderr = proc.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            stdout, stderr = proc.communicate()
    assert proc.returncode == 0, stderr
    assert "server is up" not in (stdout or "")
