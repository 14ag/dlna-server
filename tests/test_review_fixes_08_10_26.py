import http.client
import os
import re
import socket
import subprocess
import time
from pathlib import Path

import pytest

from tests.conftest import _free_port
from tests.fixtures.soap_client import build_search_envelope


def _isolated_env(tmp_path, **extra):
    env = dict(os.environ, HOME=str(tmp_path), XDG_CONFIG_HOME=str(tmp_path),
               XDG_RUNTIME_DIR=str(tmp_path), DLNA_SERVER_SKIP_FIREWALL="1")
    env.pop("DISPLAY", None)
    env.pop("WAYLAND_DISPLAY", None)
    env.update(extra)
    return env


def _run(args, tmp_path, timeout=30, **env_extra):
    return subprocess.run(args, capture_output=True, text=True, timeout=timeout,
                          env=_isolated_env(tmp_path, **env_extra))


@pytest.mark.posix_only
def test_cli_help_uses_user_facing_name(dlna_binary, tmp_path):
    proc = _run([dlna_binary, "--help"], tmp_path)
    assert proc.returncode == 0
    assert proc.stdout.startswith("Usage:\n  dlna-server [OPTION…]\n")
    assert "com.github" not in proc.stdout + proc.stderr
    assert "--kill-server, -k" in proc.stdout
    assert "smb://" not in proc.stdout


@pytest.mark.posix_only
def test_gui_help_uses_user_facing_name(dlna_server_gui_binary, tmp_path):
    assert Path(dlna_server_gui_binary).name == "dlna-server-gui-bin"
    proc = _run([dlna_server_gui_binary, "--help"], tmp_path)
    assert proc.returncode == 0
    assert proc.stdout.startswith("Usage:\n  dlna-server-gui [OPTION…]\n")
    assert "com.github" not in proc.stdout + proc.stderr


@pytest.mark.posix_only
def test_gui_wrapper_help_does_not_wait_for_compositor(dlna_server_gui_binary, tmp_path):
    wrapper = Path(dlna_server_gui_binary).parent / "dlna-server-gui"
    assert wrapper.is_file(), "launcher script missing next to dlna-server-gui-bin"
    start = time.time()
    proc = _run([str(wrapper), "--help"], tmp_path, timeout=15)
    assert time.time() - start < 10
    assert proc.stdout.startswith("Usage:\n  dlna-server-gui [OPTION…]\n")


@pytest.mark.posix_only
def test_gui_gdk_environment_disables_gl(dlna_server_gui_binary, tmp_path):
    proc = _run([dlna_server_gui_binary, "--print-gdk-environment"], tmp_path)
    assert proc.returncode == 0
    lines = dict(line.split("=", 1) for line in proc.stdout.strip().splitlines())
    assert "gl-disable" in lines["GDK_DEBUG"].split(",") or "gl" in lines["GDK_DISABLE"].split(",")
    assert lines["GSK_RENDERER"] == "cairo"
    kept = _run([dlna_server_gui_binary, "--print-gdk-environment"], tmp_path,
                GDK_DEBUG="vulkan-disable")
    assert "vulkan-disable" in kept.stdout


@pytest.mark.windows_only
def test_windows_exe_has_user_facing_name(dlna_binary):
    assert Path(dlna_binary).name == "dlna-server.exe"


@pytest.mark.posix_only
def test_failed_start_clears_initial_scan_flag(dlna_binary, tmp_path):
    media = tmp_path / "media"
    media.mkdir()
    port = _free_port()
    # dual-stack wildcard holder: makes both the IPv4 and IPv6 server binds fail
    holder = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
    try:
        holder.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
        holder.bind(("::", port))
    except OSError:
        holder.close()
        holder = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        holder.bind(("0.0.0.0", port))
    holder.listen(1)
    try:
        proc = _run([dlna_binary, "--port", str(port), "--source", str(media),
                     "--print-initial-scan-flag-after-failed-start"], tmp_path)
    finally:
        holder.close()
    assert "start-ok=0" in proc.stdout
    assert "scan-in-progress=0" in proc.stdout


@pytest.mark.posix_only
def test_httpserver_start_stop_cycle_is_stable(dlna_binary, tmp_path):
    media = tmp_path / "media"
    media.mkdir()
    for _ in range(10):
        proc = _run([dlna_binary, "--port", str(_free_port()), "--source", str(media),
                     "--print-httpserver-concurrent-start-stop-safety"], tmp_path, timeout=60)
        assert proc.returncode == 0
        assert "is-healthy=1" in proc.stdout


@pytest.fixture
def empty_media(media_source_dir):
    # Populate BEFORE the running_server fixture launches the binary, so
    # the initial scan finds the file. Requesting empty_media before
    # running_server in the test signature guarantees this runs first.
    # A post-start write would rely on the inotify watcher, which does not
    # fire on DrvFS mounts.
    (media_source_dir / "empty.mp3").write_bytes(b"")
    return media_source_dir


@pytest.mark.posix_only
def test_empty_local_file_sends_content_length_zero(empty_media, running_server):
    media_id = None
    deadline = time.time() + 20
    while time.time() < deadline and media_id is None:
        xml = running_server._soap(build_search_envelope(
            container_id="0", search_criteria="", filter="*",
            starting_index=0, requested_count=100), "Search")
        found = re.search(r"/media/(\d+)\.mp3", xml)
        if found:
            media_id = found.group(1)
        else:
            time.sleep(0.5)
    assert media_id is not None
    host, port = running_server.base_url.replace("http://", "").split(":")
    conn = http.client.HTTPConnection(host, int(port), timeout=10)
    conn.request("GET", f"/media/{media_id}.mp3")
    response = conn.getresponse()
    assert response.getheader("Content-Length") == "0"
    conn.close()
