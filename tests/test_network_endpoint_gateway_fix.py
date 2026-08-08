import subprocess
import sys

import pytest


@pytest.mark.parametrize(
    "is_virtual,has_gateway,expected",
    [
        ("0", "0", "1"),  # not virtual-named -> always used
        ("0", "1", "1"),  # not virtual-named -> always used
        ("1", "0", "0"),  # virtual-named, no gateway -> excluded (Default Switch case)
        ("1", "1", "1"),  # virtual-named, has gateway -> included (External Switch case)
    ],
)
def test_should_use_unlisted_interface(dlna_binary, is_virtual, has_gateway, expected):
    """Deterministic coverage of ShouldUseUnlistedInterface via the
    --print-should-use-unlisted-interface CLI hook. No live network or
    real adapters are involved. This is the regression guard for F-01/F-02."""
    result = subprocess.run(
        [dlna_binary, "--print-should-use-unlisted-interface", is_virtual, has_gateway],
        capture_output=True,
        text=True,
        timeout=10,
    )
    assert result.returncode == 0
    assert result.stdout.strip() == expected


def test_network_endpoint_count_does_not_hang_or_crash(dlna_binary):
    """--print-network-endpoint-count exercises the real
    EnumerateNetworkEndpoints() path end to end (Windows:
    GAA_FLAG_INCLUDE_GATEWAYS; POSIX: DetectDefaultRouteSourceAddress).
    This call must complete promptly and print a non-negative integer
    regardless of what adapters exist on the machine running this test."""
    result = subprocess.run(
        [dlna_binary, "--print-network-endpoint-count", "8200"],
        capture_output=True,
        text=True,
        timeout=10,
    )
    assert result.returncode == 0
    count = int(result.stdout.strip())
    assert count >= 0


@pytest.mark.skipif(
    sys.platform not in ("win32", "linux", "darwin"),
    reason="endpoint-count-nonzero assumes a conventional OS network stack",
)
def test_network_endpoint_count_nonzero_when_host_has_network(dlna_binary):
    """Best-effort regression check for the reported bug class: on any
    machine that has at least one adapter with a real default gateway
    (including a Hyper-V External Switch vEthernet adapter, a bridged
    Linux interface, or an ordinary physical/Wi-Fi adapter), the fixed
    EnumerateNetworkEndpoints() must return at least one endpoint. This
    assertion is environment-dependent: it only proves something on a
    machine that is actually connected to a network. Do not treat a
    failure here as conclusive without first confirming the test host
    has network connectivity at all (e.g. via `socket.create_connection`
    to a known-reachable host before asserting)."""
    result = subprocess.run(
        [dlna_binary, "--print-network-endpoint-count", "8200"],
        capture_output=True,
        text=True,
        timeout=10,
    )
    count = int(result.stdout.strip())
    assert count >= 1
