import subprocess


def test_ssdp_alive_interval_within_new_bounds(dlna_server_binary):
    result = subprocess.run(
        [str(dlna_server_binary), "--print-ssdp-alive-interval-bounds"],
        capture_output=True, text=True, timeout=10,
    )
    assert result.returncode == 0
    lines = dict(line.split("=", 1) for line in result.stdout.splitlines())
    min_ms = int(lines["min-ms"])
    max_ms = int(lines["max-ms"])
    assert min_ms >= 4 * 60 * 1000
    assert max_ms <= 6 * 60 * 1000
    # must stay strictly under the UDA ceiling of half of max-age=1800s
    assert max_ms < 15 * 60 * 1000