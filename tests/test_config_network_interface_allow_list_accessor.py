import subprocess


def test_network_interface_allow_list_round_trips_through_accessor(dlna_binary):
    result = subprocess.run(
        [dlna_binary, "--print-network-interface-allow-list-accessor"],
        capture_output=True, text=True, timeout=15,
    )
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == "eth0,wlan0"