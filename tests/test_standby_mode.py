import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def run_hook(binary, *args):
    env = dict(os.environ)
    env["DLNA_SERVER_SKIP_FIREWALL"] = "1"
    return subprocess.run(
        [binary, *args],
        capture_output=True,
        text=True,
        timeout=60,
        env=env,
    ).stdout


CASES = [
    (["--print-start-mode", "0"], "standby"),
    (["--print-start-mode", "2"], "normal"),
    (["--print-network-mode-event", "0", "0"], "enter-standby"),
    (["--print-network-mode-event", "0", "2"], "none"),
    (["--print-network-mode-event", "1", "0"], "none"),
    (["--print-network-mode-event", "1", "3"], "leave-standby"),
    (["--print-status-text", "2", "0", "0", "1"], "[Standby (waiting for network)]"),
    (["--print-status-text", "2", "0", "1", "1"], "[temporary source (standby)]"),
    (["--print-status-text", "2", "1", "0", "1"], "[scanning...]"),
    (["--print-status-text", "2", "0", "0", "0"], "[Server running]"),
]


@pytest.mark.parametrize("args,expected", CASES)
def test_standby_decisions(dlna_binary, args, expected):
    assert run_hook(dlna_binary, *args).strip() == expected


def test_rejoin_increases_boot_id(dlna_binary, tmp_path):
    count = int(run_hook(dlna_binary, "--print-network-endpoint-count", "8200").strip())
    if count == 0:
        pytest.skip("no usable network endpoint on this machine")
    media_dir = tmp_path / "media"
    media_dir.mkdir()
    (media_dir / "sample.mp4").write_bytes(b"\x00" * 4096)
    out = run_hook(dlna_binary, "--source", str(media_dir), "--print-ssdp-rejoin-bootid")
    assert "restart-ok=1" in out
    first, second = [int(x.split("=")[1]) for x in out.splitlines()[-1].split()]
    assert second > first


def parse(text):
    return dict(line.split("=", 1) for line in text.split() if "=" in line)


def test_lifecycle_matches_real_endpoint_count(dlna_binary, tmp_path):
    count = int(run_hook(dlna_binary, "--print-network-endpoint-count", "8200").strip())
    values = parse(run_hook(dlna_binary, "--source", str(tmp_path), "--print-standby-mode-lifecycle"))
    assert values["start-ok"] == "1"
    assert values["standby"] == ("1" if count == 0 else "0")
    assert values["healthy"] == "1"
    assert values["after-stop-standby"] == "0"


def _binary_path():
    env_path = os.environ.get("DLNA_SERVER")
    if env_path:
        return env_path
    if os.name == "nt":
        return str(ROOT / "output" / "winx64" / "dlna-server.exe")
    return str(ROOT / "output" / "linux" / "dlna-server")


@pytest.mark.posix_only
@pytest.mark.skipif(shutil.which("unshare") is None, reason="unshare missing")
def test_start_without_any_network_stays_healthy(tmp_path):
    probe = subprocess.run(["unshare", "-rn", "true"], capture_output=True)
    if probe.returncode != 0:
        pytest.skip("user and network namespaces not allowed")
    BINARY = _binary_path()
    result = subprocess.run(["unshare", "-rn", BINARY, "--source", str(tmp_path),
                             "--print-standby-mode-lifecycle"],
                            capture_output=True, text=True, timeout=60)
    values = parse(result.stdout)
    assert values["start-ok"] == "1"
    assert values["standby"] == "1"
    assert values["healthy"] == "1"


def read(name):
    with open(os.path.join(ROOT, "src", name), encoding="utf-8") as handle:
        return handle.read()


def test_health_check_ignores_ssdp_in_standby():
    text = read("server_common.cpp")
    body = text.split("bool Server::IsHealthy() const {", 1)[1].split("}\n", 1)[0]
    assert "NetworkMode::Standby" in body


def test_both_start_functions_enter_standby_without_endpoints():
    for name in ("server.cpp", "posix_server.cpp"):
        text = read(name)
        assert "startMode == NetworkMode::Normal && !SSDP::Get().Start" in text
        assert "starting in standby" in text


def test_restart_guard_is_after_equality_check():
    text = read("server_common.cpp")
    body = text.split("void Server::RestartSsdpForNetworkChange() {", 1)[1].split("void Server::StopNetworkChangeWatcher()", 1)[0]
    assert body.index("NetworkEndpointSetsEqual") < body.index("less than 2s ago")
    assert body.index("DecideNetworkModeEvent") < body.index("less than 2s ago")


def test_http_start_has_no_mode_flag():
    for name in ("server.cpp", "posix_server.cpp"):
        text = read(name)
        assert "HttpServer::Get().Start(cfg.port)" in text


def test_http_listen_socket_binds_any_address():
    for name in ("httpserver.cpp", "posix_httpserver.cpp"):
        text = read(name)
        assert "INADDR_ANY" in text
        assert "in6addr_any" in text
