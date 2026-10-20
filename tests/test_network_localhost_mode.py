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


@pytest.mark.parametrize("mode,count,announced,expected", [
    ("0", "1", "0", "none"),
    ("0", "0", "0", "lost"),
    ("0", "0", "1", "lost"),
    ("1", "0", "0", "none"),
    ("1", "1", "0", "restored"),
    ("1", "1", "1", "none"),
    ("1", "3", "0", "restored"),
])
def test_network_mode_event_truth_table(dlna_binary, mode, count, announced, expected):
    assert run_hook(dlna_binary, "--print-network-mode-event", mode, count, announced).strip() == expected


@pytest.mark.parametrize("count,expected", [("0", "localhost"), ("1", "normal"), ("4", "normal")])
def test_start_mode(dlna_binary, count, expected):
    assert run_hook(dlna_binary, "--print-start-mode", count).strip() == expected


def test_status_text_localhost_variants(dlna_binary):
    assert run_hook(dlna_binary, "--print-status-text", "2", "0", "0").strip() == "[Server running]"
    assert run_hook(dlna_binary, "--print-status-text", "2", "0", "0", "1").strip() == "[Server running (localhost mode)]"
    assert run_hook(dlna_binary, "--print-status-text", "2", "0", "1", "1").strip() == "[temporary source (localhost mode)]"
    assert run_hook(dlna_binary, "--print-status-text", "2", "1", "0", "1").strip() == "[scanning...]"
    assert run_hook(dlna_binary, "--print-status-text", "0", "0", "0", "1").strip() == "[]"


def parse(text):
    return dict(line.split("=", 1) for line in text.split() if "=" in line)


def test_lifecycle_matches_real_endpoint_count(dlna_binary, tmp_path):
    count = int(run_hook(dlna_binary, "--print-network-endpoint-count", "8200").strip())
    values = parse(run_hook(dlna_binary, "--source", str(tmp_path), "--print-localhost-mode-lifecycle"))
    assert values["start-ok"] == "1"
    assert values["localhost"] == ("1" if count == 0 else "0")
    assert values["healthy"] == "1"
    assert values["after-stop-localhost"] == "0"


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
                             "--print-localhost-mode-lifecycle"],
                            capture_output=True, text=True, timeout=60)
    values = parse(result.stdout)
    assert values["start-ok"] == "1"
    assert values["localhost"] == "1"
    assert values["healthy"] == "1"


def read(name):
    with open(os.path.join(ROOT, "src", name), encoding="utf-8") as handle:
        return handle.read()


def test_health_check_ignores_ssdp_in_localhost_mode():
    text = read("server_common.cpp")
    body = text.split("bool Server::IsHealthy() const {", 1)[1].split("}\n", 1)[0]
    assert "NetworkMode::Localhost" in body


def test_both_start_functions_skip_ssdp_in_localhost_mode():
    for name in ("server.cpp", "posix_server.cpp"):
        text = read(name)
        assert "startMode == NetworkMode::Normal && !SSDP::Get().Start" in text
        assert "No active network endpoints found" not in text


def test_restart_guard_is_after_equality_check():
    text = read("server_common.cpp")
    body = text.split("void Server::RestartSsdpForNetworkChange() {", 1)[1].split("void Server::StopNetworkChangeWatcher()", 1)[0]
    assert body.index("NetworkEndpointSetsEqual") < body.index("less than 2s ago")
    assert body.index("DecideNetworkModeEvent") < body.index("less than 2s ago")
