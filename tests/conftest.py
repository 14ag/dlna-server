import os
import signal
import socket
import subprocess
import sys
import time
import shutil
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
    import ctypes.wintypes


# Windows constants for _win_kill_by_window
_WM_KILL_SERVER = None  # resolved lazily from the binary if needed
# WM_APP + 22 is the value defined in mainwindow.h as WM_KILL_SERVER.
# We hard-code it here so the test helper does not need to parse headers.
_WM_KILL_SERVER_VALUE = 0x8016  # WM_APP (0x8000) + 22


def _win_kill_by_window():
    """On Windows: find every dlna-server_Main HWND and post WM_KILL_SERVER.

    The server always registers class 'dlna-server_Main' even when started with
    --headless (it uses WS_EX_TOOLWINDOW to hide from the taskbar). PostMessage
    is fire-and-forget but that is fine: we wait for the Popen handle afterward.
    """
    if os.name != "nt":
        return 0
    user32 = ctypes.windll.user32
    found = []
    EnumWindowsProc = ctypes.WINFUNCTYPE(
        ctypes.c_bool, ctypes.wintypes.HWND, ctypes.wintypes.LPARAM)
    buf = ctypes.create_unicode_buffer(256)
    def _cb(h, _):
        n = user32.GetClassNameW(h, buf, 256)
        if n > 0 and buf.value == "dlna-server_Main":
            found.append(int(h))
        return True
    user32.EnumWindows(EnumWindowsProc(_cb), 0)
    for h in found:
        user32.PostMessageW(ctypes.wintypes.HWND(h), _WM_KILL_SERVER_VALUE, 0, 0)
    return len(found)


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

    server = _pick_existing_binary([
        repo_root / "output" / "linux" / "dlna-server",
        Path("/usr/bin/dlna-server"),
        Path("/usr/local/bin/dlna-server"),
    ])
    gui = _pick_existing_binary([
        repo_root / "output" / "linux" / "dlna-server-gui-bin",
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
# Ensure no test invocation of the binary ever blocks on the interactive
# Windows firewall dialog.  Tests that specifically exercise the empty-var
# path construct their own env dict, so this default does not interfere.
os.environ.setdefault("DLNA_SERVER_SKIP_FIREWALL", "1")


def _repo_tmp_root():
    root = Path(__file__).resolve().parent.parent
    d = root / "tmp"
    d.mkdir(parents=True, exist_ok=True)
    return d


def _repo_tmp_dir(prefix):
    return Path(tempfile.mkdtemp(prefix=prefix, dir=str(_repo_tmp_root())))


# Redirect every bare tempfile call site (mkdtemp, TemporaryDirectory, ...) so
# tests never write outside the repository folder.  pytest_sessionfinish wipes
# the whole tmp/ tree when the session ends.
tempfile.tempdir = str(_repo_tmp_root())


def pytest_sessionfinish(session, exitstatus):
    shutil.rmtree(str(_repo_tmp_root()), ignore_errors=True)


def pytest_collection_modifyitems(config, items):
    deselected = []
    remaining = []
    for item in items:
        if os.name == "nt" and (
            item.get_closest_marker("posix_only")
            or item.get_closest_marker("gui_only")
        ):
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
            config_dir = _repo_tmp_dir("dlna-config-")
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
        env["XDG_RUNTIME_DIR"] = str(_repo_tmp_dir("dlna-runtime-"))
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

    # stdout/stderr are DEVNULL (not PIPE) because the server logs SSDP
    # notifications continuously while running; a captured pipe that is never
    # drained deadlocks the server once the 64 KiB buffer fills, which makes
    # concurrent SOAP/HTTP requests hang. No fixture consumer reads this
    # process's stdout, so discarding it is safe and matches the other
    # long-running launch sites in this file.
    proc = subprocess.Popen(
        [str(binary_path), "--headless"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
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
        return _repo_tmp_dir("dlna-config-")
    return Path(config_root)


def server_config_ini_path(binary_path, config_root=None):
    root = server_config_root(binary_path, config_root)
    if os.name == "nt":
        return root / "config.ini"
    return root / "dlna-server" / "config.ini"


def _teardown_server(proc, old_config, config_ini):
    if os.name == "nt":
        # Ask the server to shut down gracefully via its message pump so it
        # releases the single-instance mutex before we terminate it.
        # PostMessage is fire-and-forget; we then wait up to 5 s for the
        # process to exit on its own before falling back to terminate().
        _win_kill_by_window()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=3)
    else:
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
def repo_root():
    from pathlib import Path as _Path
    return _Path(__file__).resolve().parent.parent


@pytest.fixture
def dlna_binary():
    # DLNA_SERVER env var overrides all auto-detection
    env_path = os.environ.get("DLNA_SERVER")
    if env_path:
        p = Path(env_path)
        if p.exists():
            return str(p)
        pytest.fail(f"DLNA_SERVER={env_path} set but binary not found")

    root = Path(__file__).resolve().parent
    candidates = []

    if os.name == "nt":
        candidates = [
            root.parent / "output" / "winx64" / "DLNA Server.exe",
            root.parent / "output" / "winx86" / "DLNA Server.exe",
        ]
    else:
        # POSIX (Linux/macOS) release assets
        candidates = [
            root.parent / "output" / "linux" / "dlna-server",
        ]

    for path in candidates:
        if path.exists():
            return str(path)
    pytest.fail("DLNA Server executable not found" +
                (" (set DLNA_SERVER env var to path)" if os.name != "nt" else ""))

@pytest.fixture
def dlna_server_binary(dlna_binary):
    """Alias for dlna_binary — provided for tests that use that name."""
    return dlna_binary


@pytest.fixture
def dlna_server_gui_binary():
    # DLNA_GUI_BINARY env var overrides all auto-detection
    env_path = os.environ.get("DLNA_GUI_BINARY")
    if env_path:
        p = Path(env_path)
        if p.exists():
            return str(p)
        pytest.fail(f"DLNA_GUI_BINARY={env_path} set but binary not found")

    root = Path(__file__).resolve().parent
    candidates = [
        root.parent / "output" / "linux" / "dlna-server-gui-bin",
    ]
    for path in candidates:
        if path.exists():
            return str(path)
    pytest.fail("GTK4 GUI binary not found (set DLNA_GUI_BINARY env var to path)")


@pytest.fixture
def xvfb():
    """Start a private Xvfb so GUI tests get a live DISPLAY.

    This hands back the environment the parent should pass to Popen(env=...),
    and shuts the X server down on teardown. Tests must pass this env into any
    subprocess they spawn.
    """
    import shutil
    import socket
    if shutil.which("Xvfb") is None:
        pytest.fail("Xvfb not installed")
    display = f":{100 + (os.getpid() % 4000)}"
    # On some WSL environments the X Unix socket dir (/tmp/.X11-unix) is
    # root-owned and Xvfb cannot create its socket there. Fall back to
    # TCP-only transport which still works for local connections.
    args = ["Xvfb", display, "-screen", "0", "1280x800x24", "-listen", "tcp", "-ac"]
    proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        deadline = time.time() + 5
        ok = False
        while time.time() < deadline:
            if proc.poll() is not None:
                break
            # Try Unix socket first, then TCP as fallback for WSL
            try:
                sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                sock.settimeout(0.2)
                sock.connect(f"/tmp/.X11-unix/X{display.lstrip(':')}")
                ok = True
                sock.close()
                break
            except (OSError, socket.error):
                pass
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(0.2)
                sock.connect(("127.0.0.1", 6000 + int(display.lstrip(":"))))
                ok = True
                sock.close()
                break
            except (OSError, socket.error):
                pass
            time.sleep(0.1)
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
    scan_roots.append(_repo_tmp_root())
    for root in scan_roots:
        if root.is_dir():
            for child in root.iterdir():
                name = child.name
                if name.startswith("dlna-runtime-") and child.is_dir():
                    dirs.add(str(child))

    return sorted(dirs)


def _kill_all_runtime_instances():
    """Kill any leftover daemon instances before each test.

    The POSIX single-instance lock + socket live at a fixed
    /tmp/dlna-server-<uid> location (independent of XDG_RUNTIME_DIR), so one
    --kill-server call always reaches whatever instance is running. A
    process-level backstop then hard-stops any daemonized child that ignored
    the IPC kill, guaranteeing no more than one instance can carry over into
    the next test.
    On Windows: post WM_KILL_SERVER to every dlna-server_Main window found,
    then wait briefly for them to exit.
    """
    if os.name == "nt":
        count = _win_kill_by_window()
        if count:
            time.sleep(0.5)  # give the processes a moment to exit
        return

    binaries = []
    for key in ("DLNA_SERVER", "DLNA_CLI_BINARY", "DLNA_GUI_BINARY"):
        value = os.environ.get(key)
        if value:
            binaries.append(value)

    base_env = os.environ.copy()
    for binary in binaries:
        try:
            subprocess.run(
                [binary, "--kill-server"],
                env=base_env,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=10,
                check=False,
            )
        except (OSError, subprocess.SubprocessError):
            pass
    time.sleep(0.5)  # give a just-killed instance time to release its lock
    _hard_kill_leftover_processes()


def _hard_kill_leftover_processes():
    """Hard-stop any dlna-server / dlna-server-gui-bin process still alive.

    Covers daemonized children that detach and never drain the IPC kill, or
    instances stuck in a state where the socket no longer exists. Scans
    /proc comm names so it is independent of stale socket/lock files.
    """
    if not sys.platform.startswith("linux"):
        return
    targets = {"dlna-server", "dlna-server-gui-bin"}
    proc_root = Path("/proc")
    if not proc_root.is_dir():
        return
    for entry in proc_root.iterdir():
        if not entry.name.isdigit():
            continue
        try:
            comm = (entry / "comm").read_text().strip()
        except OSError:
            continue
        if comm not in targets:
            continue
        try:
            os.kill(int(entry.name), signal.SIGKILL)
        except OSError:
            pass


def _win_clean_stale_config():
    """Delete any config.ini left next to the Windows binary.

    On Windows the server reads config.ini from the directory containing the
    executable.  A test that calls _launch_server() writes a port-specific
    config.ini there and restores it on teardown, but if a prior run was
    hard-killed or crashed the file stays behind.  The stale file can carry
    DebugLog=1 and a port that causes Server::Start() to show a blocking
    firewall MessageBox (when DLNA_SERVER_SKIP_FIREWALL is not set by the
    caller) and therefore hang indefinitely.
    """
    if os.name != "nt":
        return
    binary = os.environ.get("DLNA_SERVER") or os.environ.get("DLNA_CLI_BINARY")
    if not binary:
        return
    config_ini = Path(binary).parent / "config.ini"
    if config_ini.exists():
        try:
            config_ini.unlink()
        except OSError:
            pass


@pytest.fixture(scope="session", autouse=True)
def _kill_previously_running_instance() -> None:
    _kill_all_runtime_instances()
    _win_clean_stale_config()


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


def _get_lan_ip():
    """Return the machine's primary LAN IPv4 address.

    Opens a UDP socket toward a public address (no packets are sent; connect()
    on a UDP socket just selects the route) and reads the local side.  Falls
    back to 127.0.0.1 only if no routable interface is available, which keeps
    the fixture working in fully offline environments at the cost of not
    exercising real multicast routing.
    """
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("8.8.8.8", 80))
            return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"


def _resolve_dlna_binary():
    env_path = os.environ.get("DLNA_SERVER") or os.environ.get("DLNA_CLI_BINARY")
    if env_path and Path(env_path).exists():
        return str(Path(env_path))
    root = Path(__file__).resolve().parent
    if os.name == "nt":
        candidates = [
            root.parent / "output" / "winx64" / "DLNA Server.exe",
            root.parent / "output" / "winx86" / "DLNA Server.exe",
        ]
    else:
        candidates = [
            root.parent / "output" / "linux" / "dlna-server",
        ]
    for p in candidates:
        if p.exists():
            return str(p)
    return None


@pytest.fixture(scope="module")
def dlna_server_endpoint(tmp_path_factory):
    """Launch the built dlna-server binary and yield its LAN IP:port endpoint.

    Falls back to DLNA_SERVER_ENDPOINT env var if set, which lets CI or the
    user point at an already-running instance without launching a new one.
    Skips the module if neither the binary nor the env var is available.
    """
    override = os.environ.get("DLNA_SERVER_ENDPOINT")
    if override:
        yield override
        return

    binary = _resolve_dlna_binary()
    if not binary:
        pytest.fail(
            "dlna-server binary not found and DLNA_SERVER_ENDPOINT not set. "
            "Build the project or set DLNA_SERVER_ENDPOINT=host:port."
        )

    media_dir = tmp_path_factory.mktemp("dlna-vlc-media")
    (media_dir / "sample.mp3").write_bytes(b"\x00" * 1024)

    port = _free_port()
    proc, connected, old_config, config_ini = _launch_server(
        binary, port, str(media_dir))

    if not connected:
        _teardown_server(proc, old_config, config_ini)
        pytest.fail(f"dlna-server did not open HTTP port {port} within 15s")

    lan_ip = _get_lan_ip()
    yield f"{lan_ip}:{port}"

    _teardown_server(proc, old_config, config_ini)

