import subprocess


def test_source_scan_pool_worker_count_is_bounded(dlna_server_binary):
    result = subprocess.run(
        [str(dlna_server_binary), "--print-source-scan-pool-worker-count"],
        capture_output=True, text=True, timeout=10,
    )
    assert result.returncode == 0
    worker_count = int(result.stdout.strip())
    assert worker_count >= 1
    # kMaxClientThreads is 64 in http_common.h; SourceScanPool must never
    # exceed it regardless of core count on the machine running this test
    assert worker_count <= 64