import subprocess


def test_cache_recomputes_after_invalidation(dlna_binary):
    result = subprocess.run(
        [dlna_binary, "--print-routable-host-cache-invalidation"],
        capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stderr
    counts = {}
    for line in result.stdout.splitlines():
        if "=" in line:
            key, value = line.strip().split("=", 1)
            counts[key] = int(value)
    assert counts.get("before") == 0
    assert counts.get("after-first-call") == 1
    assert counts.get("after-second-call-same-port") == 1
    assert counts.get("after-invalidate-then-call") == 2
