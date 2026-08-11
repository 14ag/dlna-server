import os
import socket
import subprocess
import time
import tempfile
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


def _pick_existing_binary(paths):
    for path in paths:
        if path.is_file():
            if os.name == "nt" or os.access(path, os.X_OK):
                return str(path)
    return None


def _configure_binary_envs():
    root = Path(__file__).resolve().parent
    repo_root = root.parent

    if os.name == "nt":
        server = repo_root / "output" / "winx64" / "DLNA Server.exe"
        if server.is_file():
            server_path = str(server)
            os.environ.setdefault("DLNA_SERVER", server_path)
            os.environ.setdefault("DLNA_CLI_BINARY", server_path)
            os.environ.setdefault("DLNA_GUI_BINARY", server_path)
        return

    build_root = Path(os.environ.get("DLNA_BUILD_ROOT", Path.home() / "dlna-server-build"))
    server = _pick_existing_binary([
        repo_root / "build-release-linux-stage" / "install" / "bin" / "dlna-server",
        build_root / "build-release-linux-stage" / "install" / "bin" / "dlna-server",
        Path("/usr/bin/dlna-server"),
        Path("/usr/local/bin/dlna-server"),
    ])
    gui = _pick_existing_binary([
        repo_root / "build-release-linux-stage" / "install" / "bin" / "dlna-server-gui-bin",
        build_root / "build-release-linux-stage" / "install" / "bin" / "dlna-server-gui-bin",
        Path("/usr/bin/dlna-server-gui-bin"),
        Path("/usr/local/bin/dlna-server-gui-bin"),
    ])
    if gui is None:
        gui = server
    if server is None:
        server = gui
    if gui:
        os.environ.setdefault("DLNA_GUI_BINARY", gui)
    if server:
        os.environ.setdefault("DLNA_SERVER", server)
        os.environ.setdefault("DLNA_CLI_BINARY", server)


_configure_binary_envs()


def pytest_collection_modifyitems(config, items):
    deselected = []
    remaining = []
    for item in items:
        if os.name == "nt" and item.get_closest_marker("posix_only"):
            deselected.append(item)
            continue
        if os.name != "nt" and item.get_closest_marker("windows_only"):
            deselected.append(item)
            continue
        remaining.append(item)
    if deselected:
        config.hook.pytest_deselected(items=deselected)
        items[:] = remaining


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
        # explicit socket timeout so a stalled server can never block a
        # worker thread forever see concurrent-suite-hang-report md the
        # urlopen default has no timeout and shutdown wait hangs on it
        with urllib.request.urlopen(req, timeout=30) as resp:
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
        if os.name == "nt":
            config_dir = binary_path.parent
        else:
            config_dir = Path(tempfile.mkdtemp(prefix="dlna-config-"))
    else:
        config_dir = Path(config_dir)

    # Determine config file path based on platform
    if os.name != "nt":
        # POSIX: server reads from XDG_CONFIG_HOME/dlna-server/config.ini
        config_subdir = config_dir / "dlna-server"
        config_subdir.mkdir(parents=True, exist_ok=True)
        config_ini = config_subdir / "config.ini"
    else:
        # Windows: server reads config.ini from its own directory.
        # Tests still pass a temp config_dir for POSIX symmetry, but the
        # binary resolves next to its executable, so keep the file there.
        config_dir = binary_path.parent
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
        env["HOME"] = str(config_dir)
        env["XDG_RUNTIME_DIR"] = tempfile.mkdtemp(prefix="dlna-runtime-")
        # Clean up any instance left over from a previous launch that shared
        # this runtime dir before starting a fresh one. Best-effort: exits
        # non-zero when no instance is listening (fresh dir), which is fine.
        try:
            subprocess.run(
                [str(binary_path), "--kill-server"],
                env=env,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=10,
                check=False,
            )
        except (OSError, subprocess.SubprocessError):
            pass

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


def server_config_root(binary_path, config_root=None):
    binary_path = Path(binary_path)
    if os.name == "nt":
        return binary_path.parent
    if config_root is None:
        return Path(tempfile.mkdtemp(prefix="dlna-config-"))
    return Path(config_root)


def server_config_ini_path(binary_path, config_root=None):
    root = server_config_root(binary_path, config_root)
    if os.name == "nt":
        return root / "config.ini"
    return root / "dlna-server" / "config.ini"


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
def dlna_server_gui_binary():
    # DLNA_GUI_BINARY env var overrides all auto-detection
    env_path = os.environ.get("DLNA_GUI_BINARY")
    if env_path:
        p = Path(env_path)
        if p.exists():
            return str(p)
        pytest.skip(f"DLNA_GUI_BINARY={env_path} set but binary not found")

    root = Path(__file__).resolve().parent
    candidates = [
        root.parent / "output" / "linux" / "dlna-server-gui-bin",
        root.parent / "build" / "dlna-server-gui-bin",
        root.parent / "build-test" / "dlna-server-gui-bin",
        root.parent / "build-release-linux-stage" / "usr" / "bin" / "dlna-server-gui-bin",
    ]
    for path in candidates:
        if path.exists():
            return str(path)
    pytest.skip("GTK4 GUI binary not found (set DLNA_GUI_BINARY env var to path)")


@pytest.fixture
def xvfb():
    """Start a private Xvfb on the X authority so GUI tests get a live DISPLAY.

    This hands back the environment the parent should pass to Popen(env=...),
    and shuts the X server down on teardown. Tests must pass this env into any
    subprocess they spawn (xvfb-run is avoided because these tests hold the
    child open with Popen and need a persistent DISPLAY).
    """
    import shutil
    import socket
    if shutil.which("Xvfb") is None:
        pytest.skip("Xvfb not installed")
    display = f":{100 + (os.getpid() % 4000)}"
    proc = subprocess.Popen(
        ["Xvfb", display, "-screen", "0", "1280x800x24", "-nolisten", "tcp"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        deadline = time.time() + 5
        ok = False
        while time.time() < deadline:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                sock.connect(f"/tmp/.X11-unix/X{display.lstrip(':')}")
                ok = True
                break
            except (OSError, socket.error):
                time.sleep(0.1)
            finally:
                sock.close()
        if not ok:
            pytest.fail("Xvfb did not become ready in time")
        yield dict(os.environ, DISPLAY=display)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3)


def _candidate_runtime_dirs():
    """Return every XDG_RUNTIME_DIR a leftover daemon may be bound to.

    The single-instance socket lives at <XDG_RUNTIME_DIR>/dlna-server.sock (or
    the /tmp/dlna-server-<uid> fallback when unset), so a daemon survives its
    hard-killed parent bound to whatever runtime dir it was launched with.
    _launch_server() gives each launch a fresh /tmp/dlna-runtime-* dir, so a
    daemon from a prior run can sit under any of those plus the default fallback.
    Collect all of them so the guard can send --kill-server to each.
    """
    dirs = set()

    uid_fallback = ""
    if os.name != "nt":
        try:
            uid_fallback = f"/tmp/dlna-server-{os.getuid()}"
        except (AttributeError, OSError):
            pass
    if uid_fallback:
        dirs.add(uid_fallback)

    scan_roots = [Path("/tmp")] if os.name != "nt" else []
    for root in scan_roots:
        if root.is_dir():
            for child in root.iterdir():
                name = child.name
                if name.startswith("dlna-runtime-") and child.is_dir():
                    dirs.add(str(child))

    return sorted(dirs)


def _kill_all_runtime_instances():
    """Send --kill-server for every runtime dir a leftover daemon may hold.

    A daemonized/non-debug server survives its parent, so a stale instance from
    a prior test or a hard-killed run can still hold the single-instance lock or
    an SSDP port. Offload each one with the app's own --kill-server flag (once
    per distinct candidate runtime dir) before running anything. Silently
    no-ops when no binary is known.
    """
    binary = os.environ.get("DLNA_SERVER") or os.environ.get("DLNA_CLI_BINARY")
    if not binary:
        return

    base_env = os.environ.copy()
    for runtime_dir in _candidate_runtime_dirs():
        env = dict(base_env)
        env["XDG_RUNTIME_DIR"] = runtime_dir
        try:
            subprocess.run(
                [binary, "--kill-server"],
                env=env,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=10,
                check=False,
            )
        except (OSError, subprocess.SubprocessError):
            pass


@pytest.fixture(autouse=True)
def _kill_previously_running_instance() -> None:
    _kill_all_runtime_instances()


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
