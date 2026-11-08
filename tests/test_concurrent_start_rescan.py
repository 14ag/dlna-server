import subprocess
from pathlib import Path

import pytest

pytestmark = pytest.mark.windows_only

CONCURRENT_TEST_MEDIA = Path(__file__).resolve().parent / "test media" / "concurrent-test"
EXPECTED_LEAF_COUNT = 3  # file1.mp4, file2.mp4, file3.mp4


@pytest.fixture
def media_folder():
    assert CONCURRENT_TEST_MEDIA.is_dir(), (
        f"test media folder not found: {CONCURRENT_TEST_MEDIA}"
    )
    return str(CONCURRENT_TEST_MEDIA)


def _run_concurrent_test(dlna_binary, media_folder):
    return subprocess.run(
        [dlna_binary, "--source", media_folder,
         "--print-concurrent-start-rescan-safety"],
        capture_output=True, text=True, timeout=60,
    )


def test_concurrent_start_rescan_does_not_crash(dlna_binary, media_folder):
    """Process must exit 0; a joinable-thread overwrite terminates with 0xC0000409."""
    result = _run_concurrent_test(dlna_binary, media_folder)
    assert result.returncode == 0, (
        f"process crashed or exited abnormally: returncode={result.returncode:#x}\n"
        f"stderr={result.stderr}"
    )


def test_concurrent_start_rescan_subtest_a_reports_ok(dlna_binary, media_folder):
    result = _run_concurrent_test(dlna_binary, media_folder)
    assert result.returncode == 0, f"returncode={result.returncode:#x}"
    assert "subtest-a-start-ok=1" in result.stdout, result.stdout


def test_concurrent_start_rescan_subtest_a_leaf_count_correct(dlna_binary, media_folder):
    """Sub-test A is deterministic: Start completes before Rescan runs.
    Leaf count must be at least the number of source files (may be higher
    if a watch-mode rescan fires, but must never be zero)."""
    result = _run_concurrent_test(dlna_binary, media_folder)
    assert result.returncode == 0, f"returncode={result.returncode:#x}"
    assert "subtest-a-leaf-media-items=" in result.stdout, result.stdout
    # Extract reported count and verify it is >= expected (catalog populated)
    for line in result.stdout.splitlines():
        if line.startswith("subtest-a-leaf-media-items="):
            reported = int(line.split("=", 1)[1])
            assert reported >= EXPECTED_LEAF_COUNT, (
                f"expected >= {EXPECTED_LEAF_COUNT} leaf items, got {reported}"
            )
            break


def test_concurrent_start_rescan_subtest_b_does_not_crash(dlna_binary, media_folder):
    """Sub-test B (racy) must not crash regardless of start-ok value."""
    result = _run_concurrent_test(dlna_binary, media_folder)
    assert result.returncode == 0, (
        f"process crashed or exited abnormally: returncode={result.returncode:#x}\n"
        f"stderr={result.stderr}"
    )
    assert "subtest-b-leaf-media-items=" in result.stdout, result.stdout

