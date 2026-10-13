import subprocess


def test_should_evict_before_cache_insert(dlna_binary):
    below_capacity = subprocess.run(
        [dlna_binary, "--print-should-evict-before-cache-insert", "5", "10"],
        capture_output=True, text=True, timeout=15,
    )
    assert below_capacity.returncode == 0, below_capacity.stderr
    assert below_capacity.stdout.strip() == "0"

    at_capacity = subprocess.run(
        [dlna_binary, "--print-should-evict-before-cache-insert", "10", "10"],
        capture_output=True, text=True, timeout=15,
    )
    assert at_capacity.returncode == 0, at_capacity.stderr
    assert at_capacity.stdout.strip() == "1"

    over_capacity = subprocess.run(
        [dlna_binary, "--print-should-evict-before-cache-insert", "11", "10"],
        capture_output=True, text=True, timeout=15,
    )
    assert over_capacity.returncode == 0, over_capacity.stderr
    assert over_capacity.stdout.strip() == "1"