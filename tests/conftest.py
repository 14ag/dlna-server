import os
import socket
import subprocess
import time
import urllib.error
import urllib.request
from pathlib import Path

import pytest

from tests.fixtures.soap_client import (
    build_browse_envelope,
    build_search_envelope,
    build_system_update_id_envelope,
    parse_browse_response,
    parse_system_update_id_response,
)

if os.name == "nt":
    import ctypes


class ServerClient:
    def __init__(self, base_url, binary_dir):
        self.base_url = base_url
        self.binary_dir = Path(binary_dir)

    def _soap(self, envelope, action):
        url = f"{self.base_url}/upnp/control/content_directory"
        headers = {
            "Content-Type": 'text/xml; charset="utf-8"',
            "SOAPACTION":
                f'"urn:schemas-upnp-org:service:ContentDirectory:1#{action}"',
        }
        req = urllib.request.Request(
            url, data=envelope.encode("utf-8"), headers=headers,
            method="POST")
        with urllib.request.urlopen(req) as resp:
            return resp.read().decode("utf-8")

    def soap_browse(self, object_id="0",
                    browse_flag="BrowseDirectChildren", filter="*",
                    starting_index=0, requested_count=100):
        env = build_browse_envelope(
            object_id=str(object_id), browse_flag=browse_flag,
            filter=filter, starting_index=starting_index,
            requested_count=requested_count)
        xml = self._soap(env, "Browse")
        return parse_browse_response(xml)

    def soap_search(self, container_id="0", search_criteria="",
                    filter="*", starting_index=0, requested_count=100):
        env = build_search_envelope(
            container_id=str(container_id), search_criteria=search_criteria,
            filter=filter, starting_index=starting_index,
            requested_count=requested_count)
        xml = self._soap(env, "Search")
        return parse_browse_response(xml)

    def soap_get_system_update_id(self):
        env = build_system_update_id_envelope()
        xml = self._soap(env, "GetSystemUpdateID")
        return parse_system_update_id_response(xml)

    def get(self, path):
        url = f"{self.base_url}{path}"
        req = urllib.request.Request(url)
        try:
            with urllib.request.urlopen(req) as resp:
                return (resp.status, resp.read().decode("utf-8"),
                        dict(resp.headers))
        except urllib.error.HTTPError as e:
            return (e.code, e.read().decode("utf-8"), dict(e.headers))


def _free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _launch_server(binary_path, port, media_source_dir, config_dir=None):
    binary_path = Path(binary_path)
    if config_dir is None:
        config_dir = binary_path.parent
    else:
        config_dir = Path(config_dir)

    # Determine config file path based on platform
    if os.name != "nt":
        # POSIX: server reads from XDG_CONFIG_HOME/dlna-server/config.ini
        config_subdir = config_dir / "dlna-server"
        config_subdir.mkdir(parents=True, exist_ok=True)
        config_ini = config_subdir / "config.ini"
    else:
        # Windows: server reads config.ini from its own directory
        config_ini = config_dir / "config.ini"

    old_config = None
    if config_ini.exists():
        old_config = config_ini.read_text(encoding="utf-8-sig")

    config_ini.write_text(
        "[Settings]\n"
        f"Port={port}\n"
        f"MediaSources={media_source_dir}\n"
        f"DebugLog=1\n",
        encoding="utf-8-sig",
    )

    env = os.environ.copy()
    env["DLNA_SERVER_SKIP_FIREWALL"] = "1"
    if os.name != "nt":
        env["XDG_CONFIG_HOME"] = str(config_dir)

    proc = subprocess.Popen(
        [str(binary_path), "--headless"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
    )

    deadline = time.time() + 15
    connected = False
    while time.time() < deadline:
        try:
            with socket.create_connection(
                    ("127.0.0.1", port), timeout=0.5):
                connected = True
                break
        except (ConnectionRefusedError, OSError, socket.timeout):
            time.sleep(0.1)

    return proc, connected, old_config, config_ini


def _teardown_server(proc, old_config, config_ini):
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=3)

    if old_config is not None:
        config_ini.write_text(old_config, encoding="utf-8-sig")
    elif config_ini.exists():
        config_ini.unlink()


@pytest.fixture
def dlna_binary():
    # DLNA_SERVER env var overrides all auto-detection
    env_path = os.environ.get("DLNA_SERVER")
    if env_path:
        p = Path(env_path)
        if p.exists():
            return str(p)
        pytest.skip(f"DLNA_SERVER={env_path} set but binary not found")

    root = Path(__file__).resolve().parent
    candidates = []

    if os.name == "nt":
        candidates = [
            root.parent / "output" / "winx64" / "DLNA Server.exe",
            root.parent / "build_winx64" / "Release" / "DLNA Server.exe",
            root.parent / "build_winx64" / "Debug" / "DLNA Server.exe",
            root / "build_winx64" / "Release" / "DLNA Server.exe",
            root / "build_winx64" / "Debug" / "DLNA Server.exe",
            root.parent / "output" / "winx64" / "build" / "Release" / "DLNA Server.exe",
        ]
    else:
        # POSIX (Linux/macOS) fallback paths
        candidates = [
            root.parent / "output" / "linux" / "dlna-server",
            root.parent / "build" / "dlna-server",
            root.parent / "build-test" / "dlna-server",
            root.parent / "build-release-linux-stage" / "usr" / "bin" / "dlna-server",
        ]

    for path in candidates:
        if path.exists():
            return str(path)
    pytest.skip("DLNA Server executable not found" +
                (" (set DLNA_SERVER env var to path)" if os.name != "nt" else ""))


@pytest.fixture
def media_source_dir(tmp_path):
    d = tmp_path / "media"
    d.mkdir()
    return d


@pytest.fixture
def running_server(dlna_binary, media_source_dir):
    binary_path = Path(dlna_binary)
    port = _free_port()

    proc, connected, old_config, config_ini = _launch_server(
        binary_path, port, media_source_dir,
        config_dir=media_source_dir.parent)

    if not connected:
        _teardown_server(proc, old_config, config_ini)
        pytest.fail(f"Server did not listen on port {port} within 15s")

    client = ServerClient(f"http://127.0.0.1:{port}", binary_path.parent)
    yield client

    _teardown_server(proc, old_config, config_ini)


@pytest.fixture
def slow_playlist_source(media_source_dir):
    from tests.fixtures.make_nested_playlist_tree import (
        make_nested_playlist_tree)
    result = make_nested_playlist_tree(
        root_dir=media_source_dir, num_nested=20, delay_ms=100)
    with result["serve"]():
        yield result
