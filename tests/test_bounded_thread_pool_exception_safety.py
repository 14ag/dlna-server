import subprocess


def test_thread_pool_survives_task_exception(dlna_binary):
    result = subprocess.run(
        [dlna_binary, "--print-thread-pool-exception-resilience"],
        capture_output=True, text=True, timeout=15,
    )
    assert result.returncode == 0, result.stderr
    assert "pool-survived-exception=1" in result.stdout
